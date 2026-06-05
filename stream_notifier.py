import obspython as obs
from discord_webhook import DiscordWebhook, DiscordEmbed


def script_description():
    return (
        "<div style='text-align: center;'>"
        "<h2>StreamNotifier</h2>"
        "<p>"
        "Welcome to StreamNotifier, an OBS Python Script that sends a Discord Webhook Message "
        "when you start your stream. You can customize the message content, author details, and "
        "embed various fields and media. We hope you find it useful!"
        "</p>"
        "</div>"
    )


def script_properties():
    props = obs.obs_properties_create()
    textline = obs.OBS_TEXT_DEFAULT
    textbox = obs.OBS_TEXT_MULTILINE

    obs.obs_properties_add_text(props, "webhook_url", "Webhook URL", textline)
    obs.obs_properties_add_text(props, "content", "Message", textbox)
    obs.obs_properties_add_text(props, "author", "Author", textline)
    obs.obs_properties_add_text(props, "author_url", "Author URL", textline)
    obs.obs_properties_add_text(props, "author_icon_url", "Author Icon URL", textline)
    obs.obs_properties_add_text(props, "title", "Title", textline)
    obs.obs_properties_add_text(props, "description", "Description", textbox)
    obs.obs_properties_add_text(props, "body_url", "Body URL", textline)
    obs.obs_properties_add_color(props, "color", "Color(Hex):")
    obs.obs_properties_add_text(props, "image_url", "Image URL", textline)
    obs.obs_properties_add_text(props, "thumbnail_url", "Thumbnail URL", textline)
    obs.obs_properties_add_text(props, "footer", "Footer", textline)

    max_fields = 10  # Define a maximum number of fields
    for i in range(1, max_fields + 1):
        enabled = obs.obs_properties_add_bool(props, f"field{i}_enabled", f"Enable Field {i}")
        obs.obs_property_set_modified_callback(enabled, field_enabled_modified)

        name = obs.obs_properties_add_text(props, f"field{i}_name", f"Field {i} Title", textline)
        value = obs.obs_properties_add_text(props, f"field{i}_value", f"Field {i} Description", textbox)

        obs.obs_property_set_visible(name, False)
        obs.obs_property_set_visible(value, False)

        if i > 1:
            obs.obs_property_set_visible(enabled, False)

    return props


def field_enabled_modified(props, prop, settings):
    max_fields = 10  # Must match the number in script_properties
    for i in range(1, max_fields + 1):
        field_enabled = obs.obs_data_get_bool(settings, f"field{i}_enabled")
        obs.obs_property_set_visible(obs.obs_properties_get(props, f"field{i}_name"), field_enabled)
        obs.obs_property_set_visible(obs.obs_properties_get(props, f"field{i}_value"), field_enabled)

        if i < max_fields:
            next_enabled = obs.obs_properties_get(props, f"field{i + 1}_enabled")
            obs.obs_property_set_visible(next_enabled, field_enabled)

    return True


def script_update(settings):
    global webhook_url
    global enabled
    global content
    global author
    global author_url
    global author_icon_url
    global title
    global description
    global body_url
    global color
    global image_url
    global thumbnail_url
    global footer
    global fields

    webhook_url = obs.obs_data_get_string(settings, "webhook_url").strip()
    enabled = validate_webhook_url(webhook_url)
    content = obs.obs_data_get_string(settings, "content")
    author = obs.obs_data_get_string(settings, "author")
    author_url = obs.obs_data_get_string(settings, "author_url").strip()
    author_icon_url = obs.obs_data_get_string(settings, "author_icon_url").strip()
    title = obs.obs_data_get_string(settings, "title")
    description = obs.obs_data_get_string(settings, "description")
    body_url = obs.obs_data_get_string(settings, "body_url").strip()
    color = obs.obs_data_get_int(settings, "color")
    image_url = obs.obs_data_get_string(settings, "image_url").strip()
    thumbnail_url = obs.obs_data_get_string(settings, "thumbnail_url").strip()
    footer = obs.obs_data_get_string(settings, "footer")

    fields = []
    max_fields = 10  # Must match the number in script_properties
    for i in range(1, max_fields + 1):
        if obs.obs_data_get_bool(settings, f"field{i}_enabled"):
            field_name = obs.obs_data_get_string(settings, f"field{i}_name")
            field_value = obs.obs_data_get_string(settings, f"field{i}_value")
            fields.append((field_name, field_value))

    if not enabled:
        print("Invalid Webhook URL : {}".format(webhook_url))


def frontend_event_handler(data):
    if data == obs.OBS_FRONTEND_EVENT_STREAMING_STARTED and enabled:
        webhook = DiscordWebhook(url=webhook_url, content=content)
        embed = DiscordEmbed(title=title, description=description, url=body_url)
        embed.set_color(((color & 0xFF) << 16) | (color & 0xFF00) | ((color & 0xFF0000) >> 16))
        embed.set_author(name=author, url=author_url, icon_url=author_icon_url)

        for field_name, field_value in fields:
            embed.add_embed_field(name=field_name, value=field_value)

        embed.set_image(url=image_url)
        embed.set_thumbnail(url=thumbnail_url)
        embed.set_footer(text=footer)
        webhook.add_embed(embed)
        webhook.execute()


def validate_webhook_url(url):
    return url.startswith("https://discord.com/api/webhooks/")


obs.obs_frontend_add_event_callback(frontend_event_handler)
