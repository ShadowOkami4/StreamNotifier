#include "clang-cl-compat.h"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/bmem.h>

#include <curl/curl.h>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QVector>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_ID, "en-US")

namespace {

constexpr int COMPONENTS_V2_FLAG = 1 << 15;
constexpr int MAX_COMPONENTS_V2_COMPONENTS = 40;
constexpr long CONNECT_TIMEOUT_SECONDS = 10;
constexpr long REQUEST_TIMEOUT_SECONDS = 20;
constexpr const char *DOCK_ID = "stream-notifier";
constexpr int TRIGGER_STREAM_STARTED = 1 << 0;
constexpr int TRIGGER_STREAM_STOPPED = 1 << 1;
constexpr int TRIGGER_RECORDING_STARTED = 1 << 2;
constexpr int TRIGGER_RECORDING_STOPPED = 1 << 3;
constexpr int DEFAULT_MESSAGE_TRIGGERS = TRIGGER_STREAM_STARTED;

struct DiscordMessage {
	QString name;
	QString payloadJson;
	int triggers = DEFAULT_MESSAGE_TRIGGERS;
};

struct DiscordTemplate {
	QString name;
	QString payloadJson;
};

struct StreamNotifierSettings {
	bool enabled = true;
	QString webhookUrl;
	std::vector<DiscordMessage> messages;
	std::vector<DiscordTemplate> templates;
};

std::mutex g_settings_mutex;
StreamNotifierSettings g_settings;

std::mutex g_workers_mutex;
std::vector<std::thread> g_workers;
std::atomic_bool g_unloading{false};

class StreamNotifierSettingsWindow;
class StreamNotifierDockPanel;
StreamNotifierSettingsWindow *g_settings_window = nullptr;
StreamNotifierDockPanel *g_dock = nullptr;

void openSettingsWindow();
void syncDockFromSettings();

std::string toStd(const QString &value)
{
	return value.toUtf8().constData();
}

QString settingsPath()
{
	char *path = obs_module_config_path("settings.ini");
	QString filePath;

	if (path) {
		filePath = QString::fromUtf8(path);
		bfree(path);
	} else {
		filePath = QDir::homePath() + "/stream-notifier.ini";
	}

	const QFileInfo fileInfo(filePath);
	QDir().mkpath(fileInfo.absolutePath());
	return filePath;
}

bool isValidWebhookUrl(const QString &urlText)
{
	const QUrl url(urlText.trimmed());
	if (!url.isValid() || url.scheme() != "https")
		return false;

	const QString host = url.host().toLower();
	return (host == "discord.com" || host == "discordapp.com") &&
	       url.path().startsWith("/api/webhooks/");
}

QString webhookExecutionUrl(const QString &urlText)
{
	QUrl url(urlText.trimmed());
	QUrlQuery query(url);
	query.removeAllQueryItems("with_components");
	query.addQueryItem("with_components", "true");
	url.setQuery(query);
	return url.toString(QUrl::FullyEncoded);
}

QString prettyJson(const QJsonObject &object)
{
	return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
}

QJsonObject defaultComponentsV2Payload()
{
	QJsonArray containerChildren;
	containerChildren.append(QJsonObject{{"type", 10},
					     {"content", "# Stream started\nJoin the stream now."}});
	containerChildren.append(QJsonObject{{"type", 14}, {"divider", true}, {"spacing", 1}});
	containerChildren.append(QJsonObject{
		{"type", 1},
		{"components",
		 QJsonArray{QJsonObject{{"type", 2},
					{"style", 5},
					{"label", "Watch"},
					{"url", "https://example.com"}}}},
	});

	return QJsonObject{
		{"flags", COMPONENTS_V2_FLAG},
		{"components",
		 QJsonArray{QJsonObject{{"type", 17},
					{"accent_color", 0x5865F2},
					{"components", containerChildren}}}},
		{"allowed_mentions", QJsonObject{{"parse", QJsonArray{}}}},
	};
}

QJsonObject migrateOldEmbedPayload(QSettings &store)
{
	const QString content = store.value("message/content").toString().trimmed();
	const QString title = store.value("embed/title").toString().trimmed();
	const QString description = store.value("embed/description").toString().trimmed();
	const QString bodyUrl = store.value("embed/bodyUrl").toString().trimmed();
	const uint32_t color = store.value("embed/color", 0x5865F2).toUInt() & 0xFFFFFF;
	const QString imageUrl = store.value("embed/imageUrl").toString().trimmed();
	const QString thumbnailUrl = store.value("embed/thumbnailUrl").toString().trimmed();
	const QString footer = store.value("embed/footer").toString().trimmed();

	QJsonArray children;
	QString header;
	if (!title.isEmpty())
		header += "# " + title + "\n";
	if (!content.isEmpty())
		header += content + "\n";
	if (!description.isEmpty())
		header += description;
	if (!header.trimmed().isEmpty())
		children.append(QJsonObject{{"type", 10}, {"content", header.trimmed()}});

	QJsonArray galleryItems;
	if (!imageUrl.isEmpty())
		galleryItems.append(QJsonObject{{"media", QJsonObject{{"url", imageUrl}}}});
	if (!thumbnailUrl.isEmpty())
		galleryItems.append(QJsonObject{{"media", QJsonObject{{"url", thumbnailUrl}}}});
	if (!galleryItems.isEmpty())
		children.append(QJsonObject{{"type", 12}, {"items", galleryItems}});

	const int fieldCount = store.beginReadArray("fields");
	for (int i = 0; i < fieldCount; ++i) {
		store.setArrayIndex(i);
		if (!store.value("enabled", false).toBool())
			continue;

		const QString name = store.value("name").toString().trimmed();
		const QString value = store.value("value").toString().trimmed();
		if (name.isEmpty() && value.isEmpty())
			continue;

		children.append(QJsonObject{{"type", 14}, {"divider", true}, {"spacing", 1}});
		children.append(QJsonObject{{"type", 10},
					    {"content", QString("## %1\n%2").arg(name, value).trimmed()}});
	}
	store.endArray();

	if (!bodyUrl.isEmpty()) {
		children.append(QJsonObject{{"type", 14}, {"divider", true}, {"spacing", 1}});
		children.append(QJsonObject{
			{"type", 1},
			{"components",
			 QJsonArray{QJsonObject{{"type", 2},
						{"style", 5},
						{"label", "Open Link"},
						{"url", bodyUrl}}}},
		});
	}

	if (!footer.isEmpty())
		children.append(QJsonObject{{"type", 10}, {"content", "-# " + footer}});

	if (children.isEmpty())
		return defaultComponentsV2Payload();

	return QJsonObject{
		{"flags", COMPONENTS_V2_FLAG},
		{"components",
		 QJsonArray{QJsonObject{{"type", 17},
					{"accent_color", static_cast<int>(color)},
					{"components", children}}}},
		{"allowed_mentions", QJsonObject{{"parse", QJsonArray{}}}},
	};
}

int countComponents(const QJsonValue &value)
{
	if (value.isArray()) {
		int count = 0;
		for (const QJsonValue child : value.toArray())
			count += countComponents(child);
		return count;
	}

	if (!value.isObject())
		return 0;

	const QJsonObject object = value.toObject();
	int count = object.contains("type") ? 1 : 0;
	for (const QJsonValue child : object)
		count += countComponents(child);
	return count;
}

bool normalizePayloadJson(const QString &rawJson, QByteArray &payload, QString &error)
{
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(rawJson.toUtf8(), &parseError);
	if (parseError.error != QJsonParseError::NoError) {
		error = QString("JSON parse error at offset %1: %2")
				.arg(parseError.offset)
				.arg(parseError.errorString());
		return false;
	}

	if (!document.isObject()) {
		error = "Message payload must be a JSON object.";
		return false;
	}

	QJsonObject object = document.object();
	const QJsonValue componentsValue = object.value("components");
	if (!componentsValue.isArray() || componentsValue.toArray().isEmpty()) {
		error = "Components V2 payloads need a non-empty top-level components array.";
		return false;
	}

	const int componentCount = countComponents(componentsValue);
	if (componentCount > MAX_COMPONENTS_V2_COMPONENTS) {
		error = QString("Discord allows up to %1 total components per V2 message; this has %2.")
				.arg(MAX_COMPONENTS_V2_COMPONENTS)
				.arg(componentCount);
		return false;
	}

	object.insert("flags", object.value("flags").toInt() | COMPONENTS_V2_FLAG);
	object.remove("content");
	object.remove("embeds");
	object.remove("poll");
	object.remove("stickers");
	object.remove("tts");

	payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
	return true;
}

void loadSettings()
{
	QSettings store(settingsPath(), QSettings::IniFormat);
	StreamNotifierSettings settings;

	settings.enabled = store.value("general/enabled", true).toBool();
	settings.webhookUrl = store.value("general/webhookUrl").toString().trimmed();

	const int messageCount = store.beginReadArray("messages");
	for (int i = 0; i < messageCount; ++i) {
		store.setArrayIndex(i);
		DiscordMessage message;
		message.name = store.value("name", QString("Message %1").arg(i + 1)).toString();
		message.payloadJson = store.value("payloadJson").toString();
		message.triggers = store.value("triggers", DEFAULT_MESSAGE_TRIGGERS).toInt();
		if (!message.payloadJson.trimmed().isEmpty())
			settings.messages.push_back(std::move(message));
	}
	store.endArray();

	if (settings.messages.empty()) {
		DiscordMessage message;
		message.name = "Stream Started";
		message.payloadJson = prettyJson(migrateOldEmbedPayload(store));
		message.triggers = DEFAULT_MESSAGE_TRIGGERS;
		settings.messages.push_back(std::move(message));
	}

	const int templateCount = store.beginReadArray("templates");
	for (int i = 0; i < templateCount; ++i) {
		store.setArrayIndex(i);
		DiscordTemplate messageTemplate;
		messageTemplate.name = store.value("name", QString("Template %1").arg(i + 1)).toString();
		messageTemplate.payloadJson = store.value("payloadJson").toString();
		if (!messageTemplate.payloadJson.trimmed().isEmpty())
			settings.templates.push_back(std::move(messageTemplate));
	}
	store.endArray();

	std::lock_guard<std::mutex> lock(g_settings_mutex);
	g_settings = std::move(settings);
}

void saveSettings(const StreamNotifierSettings &settings)
{
	QSettings store(settingsPath(), QSettings::IniFormat);

	store.setValue("general/enabled", settings.enabled);
	store.setValue("general/webhookUrl", settings.webhookUrl);

	store.beginWriteArray("messages", static_cast<int>(settings.messages.size()));
	for (int i = 0; i < static_cast<int>(settings.messages.size()); ++i) {
		store.setArrayIndex(i);
		store.setValue("name", settings.messages[i].name);
		store.setValue("payloadJson", settings.messages[i].payloadJson);
		store.setValue("triggers", settings.messages[i].triggers);
	}
	store.endArray();

	store.beginWriteArray("templates", static_cast<int>(settings.templates.size()));
	for (int i = 0; i < static_cast<int>(settings.templates.size()); ++i) {
		store.setArrayIndex(i);
		store.setValue("name", settings.templates[i].name);
		store.setValue("payloadJson", settings.templates[i].payloadJson);
	}
	store.endArray();
	store.sync();

	std::lock_guard<std::mutex> lock(g_settings_mutex);
	g_settings = settings;
}

StreamNotifierSettings currentSettings()
{
	std::lock_guard<std::mutex> lock(g_settings_mutex);
	return g_settings;
}

size_t appendResponse(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	auto *response = static_cast<std::string *>(userdata);
	const size_t byteCount = size * nmemb;
	response->append(ptr, byteCount);
	return byteCount;
}

bool sendSingleWebhook(const QString &webhookUrl, const DiscordMessage &message)
{
	QString validationError;
	QByteArray payload;
	if (!normalizePayloadJson(message.payloadJson, payload, validationError)) {
		blog(LOG_WARNING, "StreamNotifier skipped '%s': %s",
		     toStd(message.name).c_str(), toStd(validationError).c_str());
		return false;
	}

	CURL *curl = curl_easy_init();
	if (!curl) {
		blog(LOG_ERROR, "StreamNotifier could not initialize libcurl");
		return false;
	}

	struct curl_slist *headers = nullptr;
	headers = curl_slist_append(headers, "Content-Type: application/json");

	std::string response;
	const std::string url = toStd(webhookExecutionUrl(webhookUrl));
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.constData());
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload.size());
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT_SECONDS);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, REQUEST_TIMEOUT_SECONDS);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "StreamNotifier OBS Plugin/" PLUGIN_VERSION);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendResponse);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

	const CURLcode result = curl_easy_perform(curl);
	long responseCode = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if (result != CURLE_OK) {
		blog(LOG_WARNING, "StreamNotifier webhook '%s' failed: %s",
		     toStd(message.name).c_str(), curl_easy_strerror(result));
		return false;
	}

	if (responseCode < 200 || responseCode >= 300) {
		const std::string shortResponse = response.substr(0, 800);
		blog(LOG_WARNING, "StreamNotifier webhook '%s' returned HTTP %ld: %s",
		     toStd(message.name).c_str(), responseCode, shortResponse.c_str());
		return false;
	}

	blog(LOG_INFO, "StreamNotifier sent Components V2 webhook '%s'",
	     toStd(message.name).c_str());
	return true;
}

void sendWebhookMessages(StreamNotifierSettings settings, int triggerMask = 0)
{
	if (!settings.enabled)
		return;

	if (!isValidWebhookUrl(settings.webhookUrl)) {
		blog(LOG_WARNING, "StreamNotifier webhook URL is missing or invalid");
		return;
	}

	if (settings.messages.empty()) {
		blog(LOG_WARNING, "StreamNotifier skipped webhook because no messages are configured");
		return;
	}

	int sentCount = 0;
	for (const DiscordMessage &message : settings.messages) {
		if (triggerMask != 0 && (message.triggers & triggerMask) == 0)
			continue;
		sendSingleWebhook(settings.webhookUrl, message);
		++sentCount;
	}

	if (triggerMask != 0 && sentCount == 0)
		blog(LOG_INFO, "StreamNotifier skipped event because no messages match its trigger");
}

void enqueueWebhook(StreamNotifierSettings settings, int triggerMask = 0)
{
	if (g_unloading.load())
		return;

	std::lock_guard<std::mutex> lock(g_workers_mutex);
	g_workers.emplace_back([settings = std::move(settings), triggerMask]() mutable {
		sendWebhookMessages(std::move(settings), triggerMask);
	});
}

void enqueueSingleMessage(QString webhookUrl, DiscordMessage message)
{
	if (g_unloading.load())
		return;

	std::lock_guard<std::mutex> lock(g_workers_mutex);
	g_workers.emplace_back([webhookUrl = std::move(webhookUrl),
				message = std::move(message)]() mutable {
		if (!isValidWebhookUrl(webhookUrl)) {
			blog(LOG_WARNING, "StreamNotifier webhook URL is missing or invalid");
			return;
		}
		sendSingleWebhook(webhookUrl, message);
	});
}

void joinWorkers()
{
	std::vector<std::thread> workers;
	{
		std::lock_guard<std::mutex> lock(g_workers_mutex);
		workers.swap(g_workers);
	}

	for (std::thread &worker : workers) {
		if (worker.joinable())
			worker.join();
	}
}

class StreamNotifierSettingsWindow : public QWidget {
public:
	explicit StreamNotifierSettingsWindow(QWidget *parent = nullptr) : QWidget(parent, Qt::Window)
	{
		setWindowTitle(obs_module_text("ToolsMenu.Configure"));
		resize(1180, 720);
		setMinimumSize(1040, 660);
		settings_ = currentSettings();
		buildUi();
		loadToUi();
	}

	void reloadFromSettings()
	{
		settings_ = currentSettings();
		loadToUi();
	}

	void openWindow()
	{
		show();
		raise();
		activateWindow();
	}

private:
	StreamNotifierSettings settings_;
	QCheckBox *enabled_ = nullptr;
	QLineEdit *webhookUrl_ = nullptr;
	QListWidget *messageList_ = nullptr;
	QLineEdit *messageName_ = nullptr;
	QCheckBox *triggerStreamStarted_ = nullptr;
	QCheckBox *triggerStreamStopped_ = nullptr;
	QCheckBox *triggerRecordingStarted_ = nullptr;
	QCheckBox *triggerRecordingStopped_ = nullptr;
	QComboBox *templateList_ = nullptr;
	QTabWidget *editorTabs_ = nullptr;
	QWidget *builderTab_ = nullptr;
	QWidget *jsonTab_ = nullptr;
	QTreeWidget *componentTree_ = nullptr;
	QComboBox *componentType_ = nullptr;
	QWidget *propertiesPanel_ = nullptr;
	QVBoxLayout *propertiesLayout_ = nullptr;
	QPlainTextEdit *payloadEditor_ = nullptr;
	QLabel *status_ = nullptr;
	bool loading_ = false;

	static QString shortText(QString text)
	{
		text.replace('\n', ' ');
		text = text.simplified();
		if (text.length() > 48)
			text = text.left(45) + "...";
		return text;
	}

	static QString pathToString(const QVector<int> &path)
	{
		QStringList parts;
		for (const int index : path)
			parts.push_back(QString::number(index));
		return parts.join('/');
	}

	static QVector<int> pathFromString(const QString &text)
	{
		QVector<int> path;
		if (text.isEmpty())
			return path;

		const QStringList parts = text.split('/', Qt::SkipEmptyParts);
		for (const QString &part : parts) {
			bool ok = false;
			const int index = part.toInt(&ok);
			if (ok)
				path.push_back(index);
		}
		return path;
	}

	static QVector<int> prefixPath(const QVector<int> &path, int length)
	{
		QVector<int> prefix;
		for (int i = 0; i < length && i < path.size(); ++i)
			prefix.push_back(path[i]);
		return prefix;
	}

	static int componentType(const QJsonObject &component)
	{
		return component.value("type").toInt(-1);
	}

	static QString hexColor(int color)
	{
		return QString("%1").arg(color & 0xFFFFFF, 6, 16, QChar('0')).toUpper();
	}

	static int parseHexColor(QString value, int fallback, bool *ok = nullptr)
	{
		value = value.trimmed();
		if (value.startsWith('#'))
			value.remove(0, 1);

		bool parsed = false;
		const int color = value.toInt(&parsed, 16);
		if (ok)
			*ok = parsed && color >= 0 && color <= 0xFFFFFF;
		return (parsed && color >= 0 && color <= 0xFFFFFF) ? color : fallback;
	}

	static QJsonObject createContainer()
	{
		return QJsonObject{{"type", 17}, {"accent_color", 0x5865F2}, {"components", QJsonArray{}}};
	}

	static QJsonObject createTextBlock(const QString &content = "New text block")
	{
		return QJsonObject{{"type", 10}, {"content", content}};
	}

	static QJsonObject createSeparator()
	{
		return QJsonObject{{"type", 14}, {"divider", true}, {"spacing", 1}};
	}

	static QJsonObject createMediaGallery()
	{
		return QJsonObject{{"type", 12},
				   {"items",
				    QJsonArray{QJsonObject{{"media",
							    QJsonObject{{"url",
									  "https://example.com/image.png"}}}}}}};
	}

	static QJsonObject createLinkButton(const QString &label = "Open Link",
					    const QString &url = "https://example.com")
	{
		return QJsonObject{{"type", 2}, {"style", 5}, {"label", label}, {"url", url}};
	}

	static QJsonObject createActionRow()
	{
		return QJsonObject{{"type", 1}, {"components", QJsonArray{createLinkButton()}}};
	}

	static QJsonObject createSection()
	{
		return QJsonObject{
			{"type", 9},
			{"components", QJsonArray{createTextBlock("New section text")}},
			{"accessory", createLinkButton()},
		};
	}

	static QJsonObject getComponent(const QJsonObject &root, const QVector<int> &path)
	{
		QJsonArray array = root.value("components").toArray();
		QJsonObject component;

		for (int depth = 0; depth < path.size(); ++depth) {
			const int index = path[depth];
			if (index < 0 || index >= array.size())
				return QJsonObject();

			component = array.at(index).toObject();
			if (depth + 1 < path.size())
				array = component.value("components").toArray();
		}

		return component;
	}

	static bool setComponentInArray(QJsonArray &array, const QVector<int> &path, int depth,
					const QJsonObject &updated)
	{
		if (depth >= path.size())
			return false;

		const int index = path[depth];
		if (index < 0 || index >= array.size())
			return false;

		if (depth + 1 == path.size()) {
			array.replace(index, updated);
			return true;
		}

		QJsonObject component = array.at(index).toObject();
		QJsonArray children = component.value("components").toArray();
		if (!setComponentInArray(children, path, depth + 1, updated))
			return false;

		component.insert("components", children);
		array.replace(index, component);
		return true;
	}

	static bool setComponent(QJsonObject &root, const QVector<int> &path, const QJsonObject &updated)
	{
		if (path.isEmpty())
			return false;

		QJsonArray components = root.value("components").toArray();
		if (!setComponentInArray(components, path, 0, updated))
			return false;

		root.insert("components", components);
		return true;
	}

	static bool getComponentArrayAt(const QJsonObject &root, const QVector<int> &parentPath,
					QJsonArray &array)
	{
		if (parentPath.isEmpty()) {
			array = root.value("components").toArray();
			return true;
		}

		const QJsonObject parent = getComponent(root, parentPath);
		if (parent.isEmpty())
			return false;

		array = parent.value("components").toArray();
		return true;
	}

	static bool setComponentArrayAt(QJsonObject &root, const QVector<int> &parentPath,
					const QJsonArray &array)
	{
		if (parentPath.isEmpty()) {
			root.insert("components", array);
			return true;
		}

		QJsonObject parent = getComponent(root, parentPath);
		if (parent.isEmpty())
			return false;

		parent.insert("components", array);
		return setComponent(root, parentPath, parent);
	}

	static QString sectionText(const QJsonObject &section)
	{
		const QJsonArray children = section.value("components").toArray();
		for (const QJsonValue child : children) {
			const QJsonObject childObject = child.toObject();
			if (componentType(childObject) == 10)
				return childObject.value("content").toString();
		}
		return QString();
	}

	static void setSectionText(QJsonObject &section, const QString &content)
	{
		QJsonArray children = section.value("components").toArray();
		for (int i = 0; i < children.size(); ++i) {
			QJsonObject child = children.at(i).toObject();
			if (componentType(child) == 10) {
				child.insert("content", content);
				children.replace(i, child);
				section.insert("components", children);
				return;
			}
		}

		children.prepend(createTextBlock(content));
		section.insert("components", children);
	}

	static QString componentTitle(const QJsonObject &component)
	{
		switch (componentType(component)) {
		case 17:
			return QString("Container (%1 items)")
				.arg(component.value("components").toArray().size());
		case 10:
			return "Text: " + shortText(component.value("content").toString());
		case 14:
			return "Separator";
		case 12:
			return QString("Media Gallery (%1 items)")
				.arg(component.value("items").toArray().size());
		case 1:
			return QString("Action Row (%1 buttons)")
				.arg(component.value("components").toArray().size());
		case 2:
			return "Link Button: " + shortText(component.value("label").toString());
		case 9:
			return "Section: " + shortText(sectionText(component));
		default:
			return QString("Component type %1").arg(componentType(component));
		}
	}

	void buildUi()
	{
		auto *root = new QVBoxLayout(this);
		root->setContentsMargins(10, 10, 10, 10);
		root->setSpacing(8);

		enabled_ = new QCheckBox("Send on stream start");
		webhookUrl_ = new QLineEdit();
		webhookUrl_->setEchoMode(QLineEdit::PasswordEchoOnEdit);
		webhookUrl_->setPlaceholderText("https://discord.com/api/webhooks/...");

		auto *connectionGroup = new QGroupBox("Connection");
		auto *connectionLayout = new QHBoxLayout(connectionGroup);
		connectionLayout->addWidget(enabled_);
		connectionLayout->addWidget(new QLabel("Webhook"));
		connectionLayout->addWidget(webhookUrl_, 1);
		addButton(connectionLayout, "Save", [this]() { saveFromUi(); });
		addButton(connectionLayout, "Send All", [this]() { sendAll(); });
		root->addWidget(connectionGroup);

		auto *templateGroup = new QGroupBox("Templates");
		auto *templateLayout = new QHBoxLayout(templateGroup);
		templateList_ = new QComboBox();
		templateLayout->addWidget(templateList_, 1);
		addButton(templateLayout, "Load", [this]() { loadSelectedTemplate(); });
		addButton(templateLayout, "Save Current", [this]() { saveCurrentAsTemplate(); });
		addButton(templateLayout, "Update", [this]() { updateSelectedTemplate(); });
		addButton(templateLayout, "Delete", [this]() { deleteSelectedTemplate(); });
		root->addWidget(templateGroup);

		auto *splitter = new QSplitter(Qt::Horizontal);
		splitter->setChildrenCollapsible(false);

		auto *messagePanel = new QWidget();
		messagePanel->setMinimumWidth(220);
		auto *messageLayout = new QVBoxLayout(messagePanel);
		messageLayout->setContentsMargins(0, 0, 6, 0);
		messageLayout->setSpacing(6);
		auto *messageTitle = new QLabel("Messages");
		messageTitle->setStyleSheet("font-weight: 600;");
		messageLayout->addWidget(messageTitle);

		messageList_ = new QListWidget();
		messageList_->setSelectionMode(QAbstractItemView::SingleSelection);
		messageLayout->addWidget(messageList_, 1);

		auto *messageButtonsA = new QHBoxLayout();
		addButton(messageButtonsA, "Add", [this]() { addMessage(); });
		addButton(messageButtonsA, "Duplicate", [this]() { duplicateMessage(); });
		addButton(messageButtonsA, "Delete", [this]() { deleteMessage(); });
		messageLayout->addLayout(messageButtonsA);

		auto *messageButtonsB = new QHBoxLayout();
		addButton(messageButtonsB, "Move Up", [this]() { moveMessage(-1); });
		addButton(messageButtonsB, "Move Down", [this]() { moveMessage(1); });
		messageLayout->addLayout(messageButtonsB);
		splitter->addWidget(messagePanel);

		auto *editorPanel = new QWidget();
		editorPanel->setMinimumWidth(740);
		auto *editorLayout = new QVBoxLayout(editorPanel);
		editorLayout->setContentsMargins(6, 0, 0, 0);
		editorLayout->setSpacing(6);

		auto *messageGroup = new QGroupBox("Selected Message");
		auto *messageForm = new QHBoxLayout(messageGroup);

		messageName_ = new QLineEdit();
		messageName_->setPlaceholderText("Message name");
		messageForm->addWidget(new QLabel("Name"));
		messageForm->addWidget(messageName_, 1);
		addButton(messageForm, "Validate", [this]() { validateCurrent(true); });
		addButton(messageForm, "Format JSON", [this]() { formatCurrent(); });
		addButton(messageForm, "Open in Discohook", [this]() { openDiscohookPreview(); });
		addButton(messageForm, "Send Selected", [this]() { sendSelected(); });
		editorLayout->addWidget(messageGroup);

		auto *triggerGroup = new QGroupBox("Send This Message On");
		auto *triggerLayout = new QHBoxLayout(triggerGroup);
		triggerStreamStarted_ = new QCheckBox("Stream start");
		triggerStreamStopped_ = new QCheckBox("Stream stop");
		triggerRecordingStarted_ = new QCheckBox("Recording start");
		triggerRecordingStopped_ = new QCheckBox("Recording stop");
		triggerLayout->addWidget(triggerStreamStarted_);
		triggerLayout->addWidget(triggerStreamStopped_);
		triggerLayout->addWidget(triggerRecordingStarted_);
		triggerLayout->addWidget(triggerRecordingStopped_);
		triggerLayout->addStretch(1);
		editorLayout->addWidget(triggerGroup);

		editorTabs_ = new QTabWidget();
		builderTab_ = new QWidget();
		auto *builderLayout = new QVBoxLayout(builderTab_);
		builderLayout->setContentsMargins(0, 0, 0, 0);

		auto *builderSplitter = new QSplitter(Qt::Horizontal);
		builderSplitter->setChildrenCollapsible(false);
		auto *componentPanel = new QWidget();
		componentPanel->setMinimumWidth(300);
		auto *componentLayout = new QVBoxLayout(componentPanel);
		componentLayout->setContentsMargins(0, 0, 6, 0);
		componentLayout->setSpacing(6);

		auto *componentTitle = new QLabel("Components");
		componentTitle->setStyleSheet("font-weight: 600;");
		componentLayout->addWidget(componentTitle);

		componentTree_ = new QTreeWidget();
		componentTree_->setHeaderLabel("Components");
		componentTree_->setSelectionMode(QAbstractItemView::SingleSelection);
		componentLayout->addWidget(componentTree_, 1);

		auto *addComponentLayout = new QHBoxLayout();
		componentType_ = new QComboBox();
		componentType_->addItem("Container", 17);
		componentType_->addItem("Text Block", 10);
		componentType_->addItem("Separator", 14);
		componentType_->addItem("Media Gallery", 12);
		componentType_->addItem("Link Button Row", 1);
		componentType_->addItem("Section", 9);
		componentType_->addItem("Link Button", 2);
		addComponentLayout->addWidget(componentType_, 1);
		addButton(addComponentLayout, "Add", [this]() { addSelectedComponent(); });
		componentLayout->addLayout(addComponentLayout);

		auto *componentButtons = new QHBoxLayout();
		addButton(componentButtons, "Duplicate", [this]() { duplicateComponent(); });
		addButton(componentButtons, "Delete", [this]() { deleteComponent(); });
		addButton(componentButtons, "Up", [this]() { moveComponent(-1); });
		addButton(componentButtons, "Down", [this]() { moveComponent(1); });
		componentLayout->addLayout(componentButtons);

		builderSplitter->addWidget(componentPanel);

		auto *scrollArea = new QScrollArea();
		scrollArea->setWidgetResizable(true);
		scrollArea->setMinimumWidth(420);
		propertiesPanel_ = new QWidget();
		propertiesLayout_ = new QVBoxLayout(propertiesPanel_);
		propertiesLayout_->setContentsMargins(10, 0, 0, 0);
		scrollArea->setWidget(propertiesPanel_);
		builderSplitter->addWidget(scrollArea);
		builderSplitter->setStretchFactor(0, 2);
		builderSplitter->setStretchFactor(1, 3);
		builderSplitter->setSizes({360, 540});
		builderLayout->addWidget(builderSplitter, 1);

		jsonTab_ = new QWidget();
		auto *jsonLayout = new QVBoxLayout(jsonTab_);
		jsonLayout->setContentsMargins(0, 0, 0, 0);
		payloadEditor_ = new QPlainTextEdit();
		payloadEditor_->setPlaceholderText("Components V2 webhook JSON");
		payloadEditor_->setLineWrapMode(QPlainTextEdit::NoWrap);
		payloadEditor_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
		payloadEditor_->setMinimumHeight(360);
		jsonLayout->addWidget(payloadEditor_, 1);

		editorTabs_->addTab(builderTab_, "Builder");
		editorTabs_->addTab(jsonTab_, "Advanced JSON");
		editorLayout->addWidget(editorTabs_, 1);

		splitter->addWidget(editorPanel);
		splitter->setStretchFactor(0, 1);
		splitter->setStretchFactor(1, 3);
		splitter->setSizes({260, 900});
		root->addWidget(splitter, 1);

		status_ = new QLabel();
		status_->setWordWrap(true);
		status_->setStyleSheet("padding: 4px;");
		root->addWidget(status_);

		QObject::connect(messageList_, &QListWidget::currentRowChanged,
				 [this](int row) { selectMessage(row); });
		QObject::connect(componentTree_, &QTreeWidget::currentItemChanged,
				 [this](QTreeWidgetItem *, QTreeWidgetItem *) {
					 if (!loading_)
						 showSelectedComponentProperties();
				 });
		QObject::connect(editorTabs_, &QTabWidget::currentChanged, [this](int index) {
			if (loading_)
				return;
			if (editorTabs_->widget(index) == builderTab_) {
				storeEditorPayload();
				refreshComponentTree();
			} else {
				syncPayloadEditor();
			}
		});
		QObject::connect(enabled_, &QCheckBox::toggled, [this](bool value) {
			if (!loading_)
				settings_.enabled = value;
		});
		QObject::connect(webhookUrl_, &QLineEdit::textChanged, [this](const QString &value) {
			if (!loading_)
				settings_.webhookUrl = value.trimmed();
		});
		QObject::connect(messageName_, &QLineEdit::textChanged, [this](const QString &value) {
			if (loading_)
				return;
			const int row = messageList_->currentRow();
			if (row < 0 || row >= static_cast<int>(settings_.messages.size()))
				return;
			settings_.messages[row].name = value.trimmed().isEmpty()
							       ? QString("Message %1").arg(row + 1)
							       : value.trimmed();
			messageList_->item(row)->setText(settings_.messages[row].name);
		});
		auto triggerChanged = [this](bool) {
			if (!loading_)
				updateCurrentMessageTriggers();
		};
		QObject::connect(triggerStreamStarted_, &QCheckBox::toggled, triggerChanged);
		QObject::connect(triggerStreamStopped_, &QCheckBox::toggled, triggerChanged);
		QObject::connect(triggerRecordingStarted_, &QCheckBox::toggled, triggerChanged);
		QObject::connect(triggerRecordingStopped_, &QCheckBox::toggled, triggerChanged);
		QObject::connect(payloadEditor_, &QPlainTextEdit::textChanged, [this]() {
			if (!loading_)
				storeEditorPayload();
		});
	}

	template<typename Callback>
	void addButton(QHBoxLayout *layout, const QString &text, Callback callback)
	{
		auto *button = new QPushButton(text);
		layout->addWidget(button);
		QObject::connect(button, &QPushButton::clicked, callback);
	}

	void loadToUi()
	{
		loading_ = true;
		enabled_->setChecked(settings_.enabled);
		webhookUrl_->setText(settings_.webhookUrl);
		messageList_->clear();
		for (const DiscordMessage &message : settings_.messages)
			messageList_->addItem(message.name);
		if (!settings_.messages.empty())
			messageList_->setCurrentRow(0);
		else
			selectMessage(-1);
		refreshTemplateList();
		loading_ = false;
		status_->setText("Ready.");
		refreshComponentTree();
	}

	void selectMessage(int row)
	{
		loading_ = true;
		if (row < 0 || row >= static_cast<int>(settings_.messages.size())) {
			messageName_->clear();
			payloadEditor_->clear();
			componentTree_->clear();
			clearProperties("No message selected.");
			messageName_->setEnabled(false);
			setTriggerControlsEnabled(false);
			editorTabs_->setEnabled(false);
			loading_ = false;
			return;
		}

		messageName_->setEnabled(true);
		setTriggerControlsEnabled(true);
		editorTabs_->setEnabled(true);
		messageName_->setText(settings_.messages[row].name);
		loadTriggersToUi(settings_.messages[row].triggers);
		payloadEditor_->setPlainText(settings_.messages[row].payloadJson);
		loading_ = false;
		refreshComponentTree();
	}

	void storeEditorPayload()
	{
		const int row = messageList_->currentRow();
		if (row < 0 || row >= static_cast<int>(settings_.messages.size()))
			return;
		settings_.messages[row].payloadJson = payloadEditor_->toPlainText();
	}

	void loadTriggersToUi(int triggers)
	{
		triggerStreamStarted_->setChecked((triggers & TRIGGER_STREAM_STARTED) != 0);
		triggerStreamStopped_->setChecked((triggers & TRIGGER_STREAM_STOPPED) != 0);
		triggerRecordingStarted_->setChecked((triggers & TRIGGER_RECORDING_STARTED) != 0);
		triggerRecordingStopped_->setChecked((triggers & TRIGGER_RECORDING_STOPPED) != 0);
	}

	void setTriggerControlsEnabled(bool enabled)
	{
		triggerStreamStarted_->setEnabled(enabled);
		triggerStreamStopped_->setEnabled(enabled);
		triggerRecordingStarted_->setEnabled(enabled);
		triggerRecordingStopped_->setEnabled(enabled);
	}

	void updateCurrentMessageTriggers()
	{
		const int row = messageList_->currentRow();
		if (row < 0 || row >= static_cast<int>(settings_.messages.size()))
			return;

		int triggers = 0;
		if (triggerStreamStarted_->isChecked())
			triggers |= TRIGGER_STREAM_STARTED;
		if (triggerStreamStopped_->isChecked())
			triggers |= TRIGGER_STREAM_STOPPED;
		if (triggerRecordingStarted_->isChecked())
			triggers |= TRIGGER_RECORDING_STARTED;
		if (triggerRecordingStopped_->isChecked())
			triggers |= TRIGGER_RECORDING_STOPPED;

		settings_.messages[row].triggers = triggers;
	}

	void refreshTemplateList(int selectedIndex = -1)
	{
		if (!templateList_)
			return;

		const bool previousLoading = loading_;
		loading_ = true;
		templateList_->clear();
		for (const DiscordTemplate &messageTemplate : settings_.templates)
			templateList_->addItem(messageTemplate.name);
		if (selectedIndex >= 0 && selectedIndex < static_cast<int>(settings_.templates.size()))
			templateList_->setCurrentIndex(selectedIndex);
		loading_ = previousLoading;
	}

	void syncPayloadEditor()
	{
		const int row = messageList_->currentRow();
		if (row < 0 || row >= static_cast<int>(settings_.messages.size()))
			return;

		const bool previousLoading = loading_;
		loading_ = true;
		payloadEditor_->setPlainText(settings_.messages[row].payloadJson);
		loading_ = previousLoading;
	}

	bool currentPayloadObject(QJsonObject &object, QString *error = nullptr) const
	{
		const int row = messageList_->currentRow();
		if (row < 0 || row >= static_cast<int>(settings_.messages.size())) {
			if (error)
				*error = "No message selected.";
			return false;
		}

		QJsonParseError parseError;
		const QJsonDocument document =
			QJsonDocument::fromJson(settings_.messages[row].payloadJson.toUtf8(), &parseError);
		if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
			if (error) {
				*error = parseError.error == QJsonParseError::NoError
						 ? "Payload must be a JSON object."
						 : QString("JSON parse error at offset %1: %2")
							   .arg(parseError.offset)
							   .arg(parseError.errorString());
			}
			return false;
		}

		object = document.object();
		return true;
	}

	QJsonObject editablePayloadObject()
	{
		QJsonObject object;
		QString error;
		if (!currentPayloadObject(object, &error)) {
			status_->setText(error + " Reset to a default builder payload.");
			object = defaultComponentsV2Payload();
		}

		if (!object.value("components").isArray())
			object.insert("components", QJsonArray{});
		return object;
	}

	void setCurrentPayloadObject(QJsonObject object, bool refreshTree,
				     const QVector<int> &selectedPath = QVector<int>())
	{
		const int row = messageList_->currentRow();
		if (row < 0 || row >= static_cast<int>(settings_.messages.size()))
			return;

		object.insert("flags", object.value("flags").toInt() | COMPONENTS_V2_FLAG);
		if (!object.contains("allowed_mentions"))
			object.insert("allowed_mentions", QJsonObject{{"parse", QJsonArray{}}});

		settings_.messages[row].payloadJson = prettyJson(object);
		syncPayloadEditor();
		if (refreshTree)
			refreshComponentTree(selectedPath);
	}

	QVector<int> selectedComponentPath() const
	{
		const QTreeWidgetItem *item = componentTree_->currentItem();
		if (!item)
			return QVector<int>();
		return pathFromString(item->data(0, Qt::UserRole).toString());
	}

	void addComponentTreeItem(QTreeWidgetItem *parent, const QJsonObject &component,
				  const QVector<int> &path)
	{
		auto *item = new QTreeWidgetItem();
		item->setText(0, componentTitle(component));
		item->setData(0, Qt::UserRole, pathToString(path));

		if (parent)
			parent->addChild(item);
		else
			componentTree_->addTopLevelItem(item);

		const QJsonArray children = component.value("components").toArray();
		for (int i = 0; i < children.size(); ++i) {
			QVector<int> childPath = path;
			childPath.push_back(i);
			addComponentTreeItem(item, children.at(i).toObject(), childPath);
		}
		item->setExpanded(true);
	}

	QTreeWidgetItem *findTreeItemByPath(const QVector<int> &path) const
	{
		const QString wanted = pathToString(path);
		std::function<QTreeWidgetItem *(QTreeWidgetItem *)> findInItem =
			[&](QTreeWidgetItem *item) -> QTreeWidgetItem * {
			if (item->data(0, Qt::UserRole).toString() == wanted)
				return item;
			for (int i = 0; i < item->childCount(); ++i) {
				if (QTreeWidgetItem *found = findInItem(item->child(i)))
					return found;
			}
			return nullptr;
		};

		for (int i = 0; i < componentTree_->topLevelItemCount(); ++i) {
			if (QTreeWidgetItem *found = findInItem(componentTree_->topLevelItem(i)))
				return found;
		}
		return nullptr;
	}

	void refreshComponentTree(const QVector<int> &selectedPath = QVector<int>())
	{
		if (!componentTree_)
			return;

		QJsonObject object;
		QString error;

		const bool previousLoading = loading_;
		loading_ = true;
		componentTree_->clear();

		if (currentPayloadObject(object, &error)) {
			const QJsonArray components = object.value("components").toArray();
			for (int i = 0; i < components.size(); ++i)
				addComponentTreeItem(nullptr, components.at(i).toObject(), QVector<int>{i});
		}

		loading_ = previousLoading;

		if (!error.isEmpty()) {
			clearProperties(error);
			return;
		}

		QTreeWidgetItem *item = selectedPath.isEmpty() ? nullptr : findTreeItemByPath(selectedPath);
		if (!item && componentTree_->topLevelItemCount() > 0)
			item = componentTree_->topLevelItem(0);
		componentTree_->setCurrentItem(item);
		showSelectedComponentProperties();
	}

	void clearProperties(const QString &text = QString(), bool addStretch = true)
	{
		while (QLayoutItem *item = propertiesLayout_->takeAt(0)) {
			if (QWidget *widget = item->widget())
				widget->deleteLater();
			delete item;
		}

		if (!text.isEmpty()) {
			auto *label = new QLabel(text);
			label->setWordWrap(true);
			propertiesLayout_->addWidget(label);
		}
		if (addStretch)
			propertiesLayout_->addStretch(1);
	}

	template<typename Mutator>
	void mutateComponent(const QVector<int> &path, Mutator mutator, bool refreshTree = false)
	{
		QJsonObject root;
		QString error;
		if (!currentPayloadObject(root, &error)) {
			status_->setText(error);
			return;
		}

		QJsonObject component = getComponent(root, path);
		if (component.isEmpty()) {
			status_->setText("Component selection is no longer valid.");
			return;
		}

		mutator(component);
		if (!setComponent(root, path, component)) {
			status_->setText("Could not update selected component.");
			return;
		}

		setCurrentPayloadObject(root, refreshTree, path);
		if (!refreshTree) {
			if (QTreeWidgetItem *item = componentTree_->currentItem())
				item->setText(0, componentTitle(component));
		}
	}

	QVector<int> nearestPathOfType(const QJsonObject &root, const QVector<int> &path, int type) const
	{
		for (int length = static_cast<int>(path.size()); length > 0; --length) {
			const QVector<int> candidate = prefixPath(path, length);
			if (componentType(getComponent(root, candidate)) == type)
				return candidate;
		}
		return QVector<int>();
	}

	QVector<int> firstTopLevelContainerPath(const QJsonObject &root) const
	{
		const QJsonArray components = root.value("components").toArray();
		const int componentCount = static_cast<int>(components.size());
		for (int i = 0; i < componentCount; ++i) {
			if (componentType(components.at(i).toObject()) == 17)
				return QVector<int>{i};
		}
		return QVector<int>();
	}

	bool appendComponentTo(QJsonObject &root, const QVector<int> &parentPath,
			       const QJsonObject &component, QVector<int> &childPath)
	{
		QJsonArray array;
		if (!getComponentArrayAt(root, parentPath, array))
			return false;

		const int index = static_cast<int>(array.size());
		array.append(component);
		if (!setComponentArrayAt(root, parentPath, array))
			return false;

		childPath = parentPath;
		childPath.push_back(index);
		return true;
	}

	QVector<int> containerTargetPath(QJsonObject &root)
	{
		QVector<int> target = nearestPathOfType(root, selectedComponentPath(), 17);
		if (!target.isEmpty())
			return target;

		target = firstTopLevelContainerPath(root);
		if (!target.isEmpty())
			return target;

		QVector<int> newPath;
		appendComponentTo(root, QVector<int>(), createContainer(), newPath);
		return newPath;
	}

	void addSelectedComponent()
	{
		if (!componentType_)
			return;

		switch (componentType_->currentData().toInt()) {
		case 17:
			addTopLevelContainer();
			break;
		case 10:
			addContainerChild(createTextBlock());
			break;
		case 14:
			addContainerChild(createSeparator());
			break;
		case 12:
			addContainerChild(createMediaGallery());
			break;
		case 1:
			addContainerChild(createActionRow());
			break;
		case 9:
			addContainerChild(createSection());
			break;
		case 2:
			addLinkButton();
			break;
		default:
			status_->setText("Select a component type to add.");
			break;
		}
	}

	void addTopLevelContainer()
	{
		QJsonObject root = editablePayloadObject();
		QVector<int> childPath;
		if (!appendComponentTo(root, QVector<int>(), createContainer(), childPath)) {
			status_->setText("Could not add container.");
			return;
		}
		setCurrentPayloadObject(root, true, childPath);
		status_->setText("Added container.");
	}

	void addContainerChild(const QJsonObject &component)
	{
		QJsonObject root = editablePayloadObject();
		const QVector<int> parentPath = containerTargetPath(root);
		QVector<int> childPath;
		if (!appendComponentTo(root, parentPath, component, childPath)) {
			status_->setText("Could not add component.");
			return;
		}
		setCurrentPayloadObject(root, true, childPath);
		status_->setText("Added component.");
	}

	void addLinkButton()
	{
		QJsonObject root = editablePayloadObject();
		QVector<int> actionRowPath = nearestPathOfType(root, selectedComponentPath(), 1);
		if (actionRowPath.isEmpty()) {
			const QVector<int> containerPath = containerTargetPath(root);
			if (!appendComponentTo(root, containerPath,
					       QJsonObject{{"type", 1}, {"components", QJsonArray{}}},
					       actionRowPath)) {
				status_->setText("Could not add action row.");
				return;
			}
		}

		QJsonArray buttons;
		getComponentArrayAt(root, actionRowPath, buttons);
		if (buttons.size() >= 5) {
			status_->setText("Discord action rows can contain up to five buttons.");
			return;
		}

		QVector<int> buttonPath;
		if (!appendComponentTo(root, actionRowPath, createLinkButton(), buttonPath)) {
			status_->setText("Could not add link button.");
			return;
		}
		setCurrentPayloadObject(root, true, buttonPath);
		status_->setText("Added link button.");
	}

	void duplicateComponent()
	{
		const QVector<int> path = selectedComponentPath();
		if (path.isEmpty()) {
			status_->setText("Select a component to duplicate.");
			return;
		}

		QJsonObject root;
		QString error;
		if (!currentPayloadObject(root, &error)) {
			status_->setText(error);
			return;
		}

		const QVector<int> parentPath = prefixPath(path, static_cast<int>(path.size()) - 1);
		QJsonArray siblings;
		if (!getComponentArrayAt(root, parentPath, siblings))
			return;

		const int index = path.back();
		if (index < 0 || index >= siblings.size())
			return;

		const int insertedIndex = index + 1;
		siblings.insert(insertedIndex, siblings.at(index));
		if (!setComponentArrayAt(root, parentPath, siblings))
			return;

		QVector<int> selected = parentPath;
		selected.push_back(insertedIndex);
		setCurrentPayloadObject(root, true, selected);
		status_->setText("Duplicated component.");
	}

	void deleteComponent()
	{
		const QVector<int> path = selectedComponentPath();
		if (path.isEmpty()) {
			status_->setText("Select a component to delete.");
			return;
		}

		QJsonObject root;
		QString error;
		if (!currentPayloadObject(root, &error)) {
			status_->setText(error);
			return;
		}

		const QVector<int> parentPath = prefixPath(path, static_cast<int>(path.size()) - 1);
		QJsonArray siblings;
		if (!getComponentArrayAt(root, parentPath, siblings))
			return;

		const int index = path.back();
		if (index < 0 || index >= siblings.size())
			return;

		siblings.removeAt(index);
		if (!setComponentArrayAt(root, parentPath, siblings))
			return;

		QVector<int> selected = parentPath;
		if (!siblings.isEmpty())
			selected.push_back(std::min(index, static_cast<int>(siblings.size()) - 1));
		setCurrentPayloadObject(root, true, selected);
		status_->setText("Deleted component.");
	}

	void moveComponent(int direction)
	{
		const QVector<int> path = selectedComponentPath();
		if (path.isEmpty())
			return;

		QJsonObject root;
		QString error;
		if (!currentPayloadObject(root, &error)) {
			status_->setText(error);
			return;
		}

		const QVector<int> parentPath = prefixPath(path, static_cast<int>(path.size()) - 1);
		QJsonArray siblings;
		if (!getComponentArrayAt(root, parentPath, siblings))
			return;

		const int index = path.back();
		const int target = index + direction;
		if (index < 0 || index >= siblings.size() || target < 0 || target >= siblings.size())
			return;

		const QJsonValue moving = siblings.at(index);
		siblings.removeAt(index);
		siblings.insert(target, moving);
		if (!setComponentArrayAt(root, parentPath, siblings))
			return;

		QVector<int> selected = parentPath;
		selected.push_back(target);
		setCurrentPayloadObject(root, true, selected);
		status_->setText("Moved component.");
	}

	void showSelectedComponentProperties()
	{
		const QVector<int> path = selectedComponentPath();
		QJsonObject root;
		QString error;
		if (path.isEmpty() || !currentPayloadObject(root, &error)) {
			clearProperties(error.isEmpty() ? "Select a component." : error);
			return;
		}

		const QJsonObject component = getComponent(root, path);
		if (component.isEmpty()) {
			clearProperties("Select a component.");
			return;
		}

		clearProperties(QString(), false);

		auto *title = new QLabel(componentTitle(component));
		title->setWordWrap(true);
		title->setStyleSheet("font-weight: 600;");
		propertiesLayout_->addWidget(title);

		auto *formWidget = new QWidget();
		auto *form = new QFormLayout(formWidget);
		form->setContentsMargins(0, 4, 0, 0);
		propertiesLayout_->addWidget(formWidget);

		switch (componentType(component)) {
		case 17:
			addContainerProperties(form, path, component);
			break;
		case 10:
			addTextProperties(form, path, component);
			break;
		case 14:
			addSeparatorProperties(form, path, component);
			break;
		case 12:
			addMediaGalleryProperties(form, path, component);
			break;
		case 1:
			addActionRowProperties(form, component);
			break;
		case 2:
			addButtonProperties(form, path, component);
			break;
		case 9:
			addSectionProperties(form, path, component);
			break;
		default:
			addUnsupportedProperties(form);
			break;
		}

		propertiesLayout_->addStretch(1);
	}

	void addContainerProperties(QFormLayout *form, const QVector<int> &path,
				    const QJsonObject &component)
	{
		auto *accent = new QLineEdit(hexColor(component.value("accent_color").toInt(0x5865F2)));
		accent->setPlaceholderText("5865F2");
		form->addRow("Accent color", accent);
		QObject::connect(accent, &QLineEdit::editingFinished, [this, path, accent]() {
			mutateComponent(path, [accent](QJsonObject &component) {
				bool ok = false;
				const int color =
					parseHexColor(accent->text(), component.value("accent_color").toInt(), &ok);
				if (ok)
					component.insert("accent_color", color);
			}, true);
		});
	}

	void addTextProperties(QFormLayout *form, const QVector<int> &path,
			       const QJsonObject &component)
	{
		auto *content = new QPlainTextEdit(component.value("content").toString());
		content->setMinimumHeight(120);
		form->addRow("Content", content);
		QObject::connect(content, &QPlainTextEdit::textChanged, [this, path, content]() {
			if (loading_)
				return;
			mutateComponent(path, [content](QJsonObject &component) {
				component.insert("content", content->toPlainText());
			});
		});
	}

	void addSeparatorProperties(QFormLayout *form, const QVector<int> &path,
				    const QJsonObject &component)
	{
		auto *divider = new QCheckBox();
		divider->setChecked(component.value("divider").toBool(true));
		form->addRow("Divider", divider);

		auto *spacing = new QComboBox();
		spacing->addItem("Small");
		spacing->addItem("Large");
		spacing->setCurrentIndex(component.value("spacing").toInt(1) == 2 ? 1 : 0);
		form->addRow("Spacing", spacing);

		QObject::connect(divider, &QCheckBox::toggled, [this, path](bool checked) {
			if (loading_)
				return;
			mutateComponent(path, [checked](QJsonObject &component) {
				component.insert("divider", checked);
			});
		});
		QObject::connect(spacing, &QComboBox::currentTextChanged, [this, path, spacing]() {
			if (loading_)
				return;
			mutateComponent(path, [spacing](QJsonObject &component) {
				component.insert("spacing", spacing->currentIndex() == 1 ? 2 : 1);
			});
		});
	}

	void addMediaGalleryProperties(QFormLayout *form, const QVector<int> &path,
				       const QJsonObject &component)
	{
		QStringList urls;
		const QJsonArray items = component.value("items").toArray();
		for (const QJsonValue item : items)
			urls.push_back(item.toObject().value("media").toObject().value("url").toString());

		auto *urlEditor = new QPlainTextEdit(urls.join('\n'));
		urlEditor->setMinimumHeight(120);
		form->addRow("Media URLs", urlEditor);
		QObject::connect(urlEditor, &QPlainTextEdit::textChanged, [this, path, urlEditor]() {
			if (loading_)
				return;
			mutateComponent(path, [urlEditor](QJsonObject &component) {
				QJsonArray items;
				const QStringList lines = urlEditor->toPlainText().split('\n');
				for (const QString &line : lines) {
					const QString url = line.trimmed();
					if (!url.isEmpty())
						items.append(QJsonObject{{"media", QJsonObject{{"url", url}}}});
				}
				component.insert("items", items);
			});
		});
	}

	void addActionRowProperties(QFormLayout *form, const QJsonObject &component)
	{
		auto *count = new QLabel(QString::number(component.value("components").toArray().size()));
		form->addRow("Buttons", count);
		auto *hint = new QLabel("Select a button in the tree to edit it.");
		hint->setWordWrap(true);
		form->addRow("", hint);
	}

	void addButtonProperties(QFormLayout *form, const QVector<int> &path,
				 const QJsonObject &component)
	{
		auto *label = new QLineEdit(component.value("label").toString());
		auto *url = new QLineEdit(component.value("url").toString());
		auto *emoji = new QLineEdit(component.value("emoji").toObject().value("name").toString());
		label->setPlaceholderText("Open Link");
		url->setPlaceholderText("https://example.com");
		emoji->setPlaceholderText(":arrow_right:");

		form->addRow("Label", label);
		form->addRow("URL", url);
		form->addRow("Emoji", emoji);

		auto updateButton = [this, path, label, url, emoji]() {
			if (loading_)
				return;
			mutateComponent(path, [label, url, emoji](QJsonObject &component) {
				component.insert("type", 2);
				component.insert("style", 5);
				component.insert("label", label->text().trimmed());
				component.insert("url", url->text().trimmed());

				const QString emojiName = emoji->text().trimmed();
				if (emojiName.isEmpty())
					component.remove("emoji");
				else
					component.insert("emoji", QJsonObject{{"name", emojiName}});
				component.remove("custom_id");
			}, true);
		};

		QObject::connect(label, &QLineEdit::editingFinished, updateButton);
		QObject::connect(url, &QLineEdit::editingFinished, updateButton);
		QObject::connect(emoji, &QLineEdit::editingFinished, updateButton);
	}

	void addSectionProperties(QFormLayout *form, const QVector<int> &path,
				  const QJsonObject &component)
	{
		auto *content = new QPlainTextEdit(sectionText(component));
		content->setMinimumHeight(120);
		form->addRow("Content", content);

		const QJsonObject accessory = component.value("accessory").toObject();
		auto *label = new QLineEdit(accessory.value("label").toString("Open Link"));
		auto *url = new QLineEdit(accessory.value("url").toString("https://example.com"));
		auto *emoji = new QLineEdit(accessory.value("emoji").toObject().value("name").toString());
		form->addRow("Button label", label);
		form->addRow("Button URL", url);
		form->addRow("Button emoji", emoji);

		auto updateSection = [this, path, content, label, url, emoji]() {
			if (loading_)
				return;
			mutateComponent(path, [content, label, url, emoji](QJsonObject &component) {
				setSectionText(component, content->toPlainText());
				QJsonObject button = createLinkButton(label->text().trimmed(), url->text().trimmed());
				const QString emojiName = emoji->text().trimmed();
				if (!emojiName.isEmpty())
					button.insert("emoji", QJsonObject{{"name", emojiName}});
				component.insert("accessory", button);
			});
		};

		QObject::connect(content, &QPlainTextEdit::textChanged, updateSection);
		QObject::connect(label, &QLineEdit::editingFinished, updateSection);
		QObject::connect(url, &QLineEdit::editingFinished, updateSection);
		QObject::connect(emoji, &QLineEdit::editingFinished, updateSection);
	}

	void addUnsupportedProperties(QFormLayout *form)
	{
		auto *label = new QLabel("This component type can be edited in the Advanced JSON tab.");
		label->setWordWrap(true);
		form->addRow("", label);
	}

	bool validateCurrent(bool showSuccess)
	{
		storeEditorPayload();
		const int row = messageList_->currentRow();
		if (row < 0 || row >= static_cast<int>(settings_.messages.size())) {
			status_->setText("No message selected.");
			return false;
		}

		QString error;
		QByteArray payload;
		const bool ok = normalizePayloadJson(settings_.messages[row].payloadJson, payload, error);
		status_->setText(ok ? "Selected message is valid Components V2 payload." : error);
		if (ok && showSuccess)
			QMessageBox::information(this, "StreamNotifier",
						 "Selected message is valid Components V2 payload.");
		return ok;
	}

	void formatCurrent()
	{
		storeEditorPayload();
		const int row = messageList_->currentRow();
		if (row < 0 || row >= static_cast<int>(settings_.messages.size()))
			return;

		QString error;
		QByteArray payload;
		if (!normalizePayloadJson(settings_.messages[row].payloadJson, payload, error)) {
			status_->setText(error);
			return;
		}

		const QJsonDocument document = QJsonDocument::fromJson(payload);
		settings_.messages[row].payloadJson =
			QString::fromUtf8(document.toJson(QJsonDocument::Indented));
		loading_ = true;
		payloadEditor_->setPlainText(settings_.messages[row].payloadJson);
		loading_ = false;
		refreshComponentTree();
		status_->setText("Formatted and normalized selected message.");
	}

	void openDiscohookPreview()
	{
		storeEditorPayload();
		const int row = messageList_->currentRow();
		if (row < 0 || row >= static_cast<int>(settings_.messages.size())) {
			status_->setText("No message selected.");
			return;
		}

		QString error;
		QByteArray payload;
		if (!normalizePayloadJson(settings_.messages[row].payloadJson, payload, error)) {
			status_->setText(error);
			return;
		}

		const QJsonObject messageData = QJsonDocument::fromJson(payload).object();
		const QJsonObject discohookData{
			{"version", "d2"},
			{"messages",
			 QJsonArray{QJsonObject{{"_id", "stream-notifier-preview"}, {"data", messageData}}}},
			{"targets", QJsonArray{}},
		};

		const QByteArray encoded =
			QJsonDocument(discohookData)
				.toJson(QJsonDocument::Compact)
				.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
		const QUrl url("https://discohook.app/?data=" + QString::fromLatin1(encoded));

		if (!QDesktopServices::openUrl(url)) {
			status_->setText("Could not open Discohook in the browser.");
			return;
		}

		status_->setText("Opened selected message in Discohook.");
	}

	bool normalizedSelectedPayload(QByteArray &payload, QString &error)
	{
		storeEditorPayload();
		const int row = messageList_->currentRow();
		if (row < 0 || row >= static_cast<int>(settings_.messages.size())) {
			error = "No message selected.";
			return false;
		}

		return normalizePayloadJson(settings_.messages[row].payloadJson, payload, error);
	}

	void loadSelectedTemplate()
	{
		const int templateIndex = templateList_->currentIndex();
		if (templateIndex < 0 || templateIndex >= static_cast<int>(settings_.templates.size())) {
			status_->setText("No template selected.");
			return;
		}

		storeEditorPayload();
		int row = messageList_->currentRow();
		if (row < 0 || row >= static_cast<int>(settings_.messages.size())) {
			DiscordMessage message;
			message.name = settings_.templates[templateIndex].name;
			message.payloadJson = settings_.templates[templateIndex].payloadJson;
			message.triggers = DEFAULT_MESSAGE_TRIGGERS;
			settings_.messages.push_back(std::move(message));
			refreshList(static_cast<int>(settings_.messages.size()) - 1);
			row = messageList_->currentRow();
		}

		settings_.messages[row].payloadJson = settings_.templates[templateIndex].payloadJson;
		loading_ = true;
		payloadEditor_->setPlainText(settings_.messages[row].payloadJson);
		loading_ = false;
		refreshComponentTree();
		status_->setText("Loaded template into selected message.");
	}

	void saveCurrentAsTemplate()
	{
		const int row = messageList_->currentRow();
		if (row < 0 || row >= static_cast<int>(settings_.messages.size())) {
			status_->setText("No message selected.");
			return;
		}

		QString error;
		QByteArray payload;
		if (!normalizedSelectedPayload(payload, error)) {
			status_->setText(error);
			return;
		}

		bool ok = false;
		const QString name = QInputDialog::getText(
			this, "Save Template", "Template name:", QLineEdit::Normal,
			settings_.messages[row].name, &ok).trimmed();
		if (!ok || name.isEmpty())
			return;

		DiscordTemplate messageTemplate;
		messageTemplate.name = name;
		messageTemplate.payloadJson =
			QString::fromUtf8(QJsonDocument::fromJson(payload).toJson(QJsonDocument::Indented));
		settings_.templates.push_back(std::move(messageTemplate));
		refreshTemplateList(static_cast<int>(settings_.templates.size()) - 1);
		saveSettings(settings_);
		status_->setText("Saved template.");
	}

	void updateSelectedTemplate()
	{
		const int templateIndex = templateList_->currentIndex();
		if (templateIndex < 0 || templateIndex >= static_cast<int>(settings_.templates.size())) {
			saveCurrentAsTemplate();
			return;
		}

		QString error;
		QByteArray payload;
		if (!normalizedSelectedPayload(payload, error)) {
			status_->setText(error);
			return;
		}

		settings_.templates[templateIndex].payloadJson =
			QString::fromUtf8(QJsonDocument::fromJson(payload).toJson(QJsonDocument::Indented));
		saveSettings(settings_);
		status_->setText("Updated selected template.");
	}

	void deleteSelectedTemplate()
	{
		const int templateIndex = templateList_->currentIndex();
		if (templateIndex < 0 || templateIndex >= static_cast<int>(settings_.templates.size())) {
			status_->setText("No template selected.");
			return;
		}

		const QString name = settings_.templates[templateIndex].name;
		if (QMessageBox::question(this, "Delete Template",
					  QString("Delete template '%1'?").arg(name)) != QMessageBox::Yes)
			return;

		settings_.templates.erase(settings_.templates.begin() + templateIndex);
		refreshTemplateList(std::min(templateIndex, static_cast<int>(settings_.templates.size()) - 1));
		saveSettings(settings_);
		status_->setText("Deleted template.");
	}

	bool saveFromUi()
	{
		storeEditorPayload();
		updateCurrentMessageTriggers();
		settings_.enabled = enabled_->isChecked();
		settings_.webhookUrl = webhookUrl_->text().trimmed();

		if (settings_.enabled && !isValidWebhookUrl(settings_.webhookUrl)) {
			QMessageBox::warning(this, "StreamNotifier",
					     "Enter a valid Discord webhook URL before enabling notifications.");
			return false;
		}

		for (const DiscordMessage &message : settings_.messages) {
			QString error;
			QByteArray payload;
			if (!normalizePayloadJson(message.payloadJson, payload, error)) {
				QMessageBox::warning(this, "StreamNotifier",
						     QString("%1: %2").arg(message.name, error));
				return false;
			}
		}

		saveSettings(settings_);
		syncDockFromSettings();
		status_->setText("Saved.");
		return true;
	}

	void addMessage()
	{
		storeEditorPayload();
		DiscordMessage message;
		message.name = QString("Message %1").arg(settings_.messages.size() + 1);
		message.payloadJson = prettyJson(defaultComponentsV2Payload());
		settings_.messages.push_back(std::move(message));
		messageList_->addItem(settings_.messages.back().name);
		messageList_->setCurrentRow(static_cast<int>(settings_.messages.size()) - 1);
		status_->setText("Added message.");
	}

	void duplicateMessage()
	{
		storeEditorPayload();
		const int row = messageList_->currentRow();
		if (row < 0 || row >= static_cast<int>(settings_.messages.size()))
			return;

		DiscordMessage copy = settings_.messages[row];
		copy.name += " Copy";
		settings_.messages.insert(settings_.messages.begin() + row + 1, std::move(copy));
		refreshList(row + 1);
		status_->setText("Duplicated message.");
	}

	void deleteMessage()
	{
		const int row = messageList_->currentRow();
		if (row < 0 || row >= static_cast<int>(settings_.messages.size()))
			return;
		if (settings_.messages.size() == 1) {
			QMessageBox::warning(this, "StreamNotifier", "At least one message is required.");
			return;
		}

		settings_.messages.erase(settings_.messages.begin() + row);
		refreshList(std::min(row, static_cast<int>(settings_.messages.size()) - 1));
		status_->setText("Deleted message.");
	}

	void moveMessage(int direction)
	{
		storeEditorPayload();
		const int row = messageList_->currentRow();
		const int target = row + direction;
		if (row < 0 || target < 0 || target >= static_cast<int>(settings_.messages.size()))
			return;

		std::swap(settings_.messages[row], settings_.messages[target]);
		refreshList(target);
		status_->setText("Moved message.");
	}

	void refreshList(int selectedRow)
	{
		loading_ = true;
		messageList_->clear();
		for (const DiscordMessage &message : settings_.messages)
			messageList_->addItem(message.name);
		loading_ = false;
		messageList_->setCurrentRow(selectedRow);
	}

	void sendSelected()
	{
		if (!validateCurrent(false))
			return;
		if (!saveFromUi())
			return;
		const int row = messageList_->currentRow();
		if (row < 0 || row >= static_cast<int>(settings_.messages.size()))
			return;
		enqueueSingleMessage(settings_.webhookUrl, settings_.messages[row]);
		status_->setText("Queued selected message. Check OBS log for the Discord response.");
	}

	void sendAll()
	{
		if (!saveFromUi())
			return;
		enqueueWebhook(settings_);
		status_->setText("Queued all messages. Check OBS log for the Discord response.");
	}
};

class StreamNotifierDockPanel : public QWidget {
public:
	explicit StreamNotifierDockPanel(QWidget *parent = nullptr) : QWidget(parent)
	{
		buildUi();
		reloadFromSettings();
	}

	void reloadFromSettings()
	{
		const StreamNotifierSettings settings = currentSettings();
		const bool webhookReady = isValidWebhookUrl(settings.webhookUrl);
		const int messageCount = static_cast<int>(settings.messages.size());
		const int templateCount = static_cast<int>(settings.templates.size());

		loading_ = true;
		enabled_->setChecked(settings.enabled);
		loading_ = false;

		if (!webhookReady) {
			statusTitle_->setText("Setup Needed");
			statusCard_->setStyleSheet(
				"QFrame { border: 1px solid #a15c00; border-radius: 6px; padding: 2px; }");
		} else if (!settings.enabled) {
			statusTitle_->setText("Paused");
			statusCard_->setStyleSheet(
				"QFrame { border: 1px solid palette(mid); border-radius: 6px; padding: 2px; }");
		} else {
			statusTitle_->setText("Ready");
			statusCard_->setStyleSheet(
				"QFrame { border: 1px solid #2d8a4e; border-radius: 6px; padding: 2px; }");
		}

		statusDetail_->setText(QString("%1 | %2")
					       .arg(webhookReady ? "Webhook connected" : "Webhook missing")
					       .arg(pluralized(messageCount, "message")));
		messageSummary_->setText(QString("Messages: %1 configured").arg(messageCount));
		webhookState_->setText(webhookReady ? "Discord webhook is valid."
						    : "Open Settings to add a Discord webhook.");
		triggerSummary_->setText(
			QString("Triggers: stream %1/%2, record %3/%4")
				.arg(countTriggeredMessages(settings, TRIGGER_STREAM_STARTED))
				.arg(countTriggeredMessages(settings, TRIGGER_STREAM_STOPPED))
				.arg(countTriggeredMessages(settings, TRIGGER_RECORDING_STARTED))
				.arg(countTriggeredMessages(settings, TRIGGER_RECORDING_STOPPED)));
		templateSummary_->setText(QString("Templates: %1 saved").arg(templateCount));
	}

private:
	QCheckBox *enabled_ = nullptr;
	QFrame *statusCard_ = nullptr;
	QLabel *statusTitle_ = nullptr;
	QLabel *statusDetail_ = nullptr;
	QLabel *messageSummary_ = nullptr;
	QLabel *webhookState_ = nullptr;
	QLabel *triggerSummary_ = nullptr;
	QLabel *templateSummary_ = nullptr;
	QLabel *status_ = nullptr;
	bool loading_ = false;

	static int countTriggeredMessages(const StreamNotifierSettings &settings, int trigger)
	{
		int count = 0;
		for (const DiscordMessage &message : settings.messages) {
			if ((message.triggers & trigger) != 0)
				++count;
		}
		return count;
	}

	static QString pluralized(int count, const QString &word)
	{
		return QString("%1 %2%3").arg(count).arg(word).arg(count == 1 ? "" : "s");
	}

	void buildUi()
	{
		auto *root = new QVBoxLayout(this);
		root->setContentsMargins(10, 10, 10, 10);
		root->setSpacing(10);
		setMinimumWidth(260);

		statusCard_ = new QFrame();
		statusCard_->setFrameShape(QFrame::StyledPanel);
		auto *statusLayout = new QVBoxLayout(statusCard_);
		statusLayout->setContentsMargins(10, 8, 10, 8);
		statusLayout->setSpacing(2);

		statusTitle_ = new QLabel();
		statusTitle_->setStyleSheet("font-weight: 700; font-size: 13px;");
		statusDetail_ = new QLabel();
		statusDetail_->setWordWrap(true);
		statusDetail_->setStyleSheet("color: palette(mid);");
		statusLayout->addWidget(statusTitle_);
		statusLayout->addWidget(statusDetail_);
		root->addWidget(statusCard_);

		enabled_ = new QCheckBox("Enable automatic sends");
		root->addWidget(enabled_);

		messageSummary_ = new QLabel();
		webhookState_ = new QLabel();
		triggerSummary_ = new QLabel();
		templateSummary_ = new QLabel();
		root->addWidget(messageSummary_);
		root->addWidget(webhookState_);
		root->addWidget(triggerSummary_);
		root->addWidget(templateSummary_);

		auto *actions = new QHBoxLayout();
		QPushButton *settingsButton = addButton(actions, "Settings", []() { openSettingsWindow(); });
		QPushButton *sendButton = addButton(actions, "Send All", [this]() { sendAllNow(); });
		settingsButton->setDefault(true);
		settingsButton->setMinimumHeight(30);
		sendButton->setMinimumHeight(30);
		root->addLayout(actions);

		status_ = new QLabel("Ready.");
		status_->setWordWrap(true);
		status_->setStyleSheet("color: palette(mid);");
		root->addWidget(status_);
		root->addStretch(1);

		QObject::connect(enabled_, &QCheckBox::toggled, [this](bool enabled) {
			if (loading_)
				return;

			StreamNotifierSettings settings = currentSettings();
			settings.enabled = enabled;
			saveSettings(settings);
			reloadFromSettings();
			status_->setText(enabled ? "Stream start notifications enabled."
						: "Stream start notifications disabled.");
		});
	}

	template<typename Callback>
	QPushButton *addButton(QHBoxLayout *layout, const QString &text, Callback callback)
	{
		auto *button = new QPushButton(text);
		layout->addWidget(button);
		QObject::connect(button, &QPushButton::clicked, callback);
		return button;
	}

	void sendAllNow()
	{
		const StreamNotifierSettings settings = currentSettings();
		if (!isValidWebhookUrl(settings.webhookUrl)) {
			status_->setText("Open Settings and add a valid Discord webhook first.");
			return;
		}

		enqueueWebhook(settings);
		status_->setText("Queued all messages.");
	}
};

void openSettingsWindow()
{
	bool created = false;
	if (!g_settings_window) {
		auto *parent = reinterpret_cast<QWidget *>(obs_frontend_get_main_window());
		g_settings_window = new StreamNotifierSettingsWindow(parent);
		created = true;
	}

	if (created || !g_settings_window->isVisible())
		g_settings_window->reloadFromSettings();
	g_settings_window->openWindow();
}

void syncDockFromSettings()
{
	if (g_dock)
		g_dock->reloadFromSettings();
}

void toolsMenuCallback(void *)
{
	openSettingsWindow();
}

void frontendEvent(enum obs_frontend_event event, void *)
{
	int triggerMask = 0;
	switch (event) {
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		triggerMask = TRIGGER_STREAM_STARTED;
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		triggerMask = TRIGGER_STREAM_STOPPED;
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
		triggerMask = TRIGGER_RECORDING_STARTED;
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		triggerMask = TRIGGER_RECORDING_STOPPED;
		break;
	default:
		return;
	}

	enqueueWebhook(currentSettings(), triggerMask);
}

} // namespace

bool obs_module_load(void)
{
	if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
		blog(LOG_ERROR, "StreamNotifier could not initialize libcurl");
		return false;
	}

	loadSettings();
	obs_frontend_add_event_callback(frontendEvent, nullptr);

	QWidget *parent = reinterpret_cast<QWidget *>(obs_frontend_get_main_window());
	g_dock = new StreamNotifierDockPanel(parent);
	if (!obs_frontend_add_dock_by_id(DOCK_ID, obs_module_text("Dock.Title"), g_dock)) {
		delete g_dock;
		g_dock = nullptr;
		blog(LOG_WARNING, "StreamNotifier could not add dock");
	} else {
		obs_frontend_add_tools_menu_item(obs_module_text("ToolsMenu.Configure"), toolsMenuCallback,
						 nullptr);
	}

	blog(LOG_INFO, "StreamNotifier loaded");
	return true;
}

void obs_module_unload(void)
{
	g_unloading.store(true);
	obs_frontend_remove_event_callback(frontendEvent, nullptr);
	delete g_settings_window;
	g_settings_window = nullptr;
	obs_frontend_remove_dock(DOCK_ID);
	/* OBS owns the wrapper dock and deletes the inserted widget when removed. */
	g_dock = nullptr;
	joinWorkers();
	curl_global_cleanup();
	blog(LOG_INFO, "StreamNotifier unloaded");
}
