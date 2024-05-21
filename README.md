# StreamNotifier (formerly OBScord)

StreamNotifier is a Python script that seamlessly connects OBS (Open Broadcaster Software) with a Discord webhook. Whenever someone starts streaming in OBS, this script sends a customizable message to a specified Discord channel via a webhook.

## 🎬 Features

- **Real-time Notifications**: Automatically sends a message to your Discord channel when the stream starts.
- **Customizable Messages**: Personalize the notification message to suit your needs.
- **Easy Setup**: Simple configuration to get you up and running quickly.

## 🛠️ Installation

1. **Clone the Repository**:
    ```bash
    git clone https://github.com/ShadowOkami/StreamNotifier.git
    cd StreamNotifier
    ```

2. **Install Required Dependencies**:
    Make sure you have Python installed. Then, install the required Python packages:
    ```bash
    pip install discord_webhook
    ```

## 🚀 Usage

1. **Add the Script to OBS**:
    - Open OBS and go to `Tools` > `Scripts`.
    - Click the `+` button and add the `stream_notifier.py` script.
    - Select the script from the list to see its properties.

2. **Configure Script Properties in OBS**:
    - **Webhook URL**: Enter your Discord webhook URL.
    - **Content/Embed**: Enter the message or create a Embed you want to send to the Discord channel when the stream starts.

3. **Start Streaming**:
    - When you start streaming in OBS, the script will automatically send a notification to your Discord channel.
