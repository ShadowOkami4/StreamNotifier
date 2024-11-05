<p align="center">
<img src="https://github.com/user-attachments/assets/b52f99a6-c6b4-465e-b077-464387885271" alt="Logo" width="400"/>
</p>

<img src="https://github.com/user-attachments/assets/42a287e2-5860-4461-bccb-d2ad3a38fd6e" alt="Logo" width="1000"/>



Welcome to **StreamNotifier**, an OBS Python Script that sends a Discord Webhook Message when you start your stream. This script allows you to customize the message content, author details, and embed various fields and media. We hope you find it useful!

## Features

- **Send Discord Webhook Messages**: Automatically send messages to a specified Discord channel when you start streaming.
- **Customizable Messages**: Personalize your messages with custom content, author information, titles, descriptions, and URLs.
- **Embed Support**: Add rich embeds to your messages, including images, thumbnails, and footers.
- **Dynamic Fields**: New in version 3.0, dynamically add and configure up to 10 custom fields that can be included in your embeds.

## Installation

1. **Install Python 3.11**: StreamNotifier requires Python 3.11 or higher. Install it from the official Python website: [Python Downloads](https://www.python.org/downloads/).
2. **Install discord_webhook**: Open a terminal or command prompt and run `pip install discord_webhook`.
3. **Add Script to OBS**: In OBS, go to `Tools` > `Scripts`, and then click the `+` button to add the downloaded script.
4. **Select Python Directory**: In OBS, go to `Tools` > `Scripts` >`Python`, and set the Python directory to the location of your Python installation where you installed `discord_webhook`.

## Usage

### Script Settings

#### Important
- **Webhook URL**: The URL of your Discord webhook.
#### Normal Message
- **Message**: The content of the message to send.
#### Embed 
- **Author**: The name of the message author.
- **Author URL**: A URL for the author (optional).
- **Author Icon URL**: An icon URL for the author (optional).
- **Title**: The title of the embed.
- **Description**: The description of the embed.
- **Body URL**: A URL for the embed (optional).
- **Color**: The color of the embed (in hex).
- **Image URL**: A URL for an image to include in the embed (optional).
- **Thumbnail URL**: A URL for a thumbnail to include in the embed (optional).
- **Footer**: The footer text for the embed.

### Dynamic Fields

You can enable and configure up to 10 custom fields

Only the next field's checkbox will appear if the current one is enabled, keeping the interface clean and manageable.

## Changelog

### Version 3.0

- **Renamed**: OBScord is now StreamNotifier.
- **Dynamic Fields**: Added support for up to 10 dynamic fields. Each checkbox for enabling fields appears only if the previous one is active, creating a cleaner and more user-friendly interface.

## License

This project is licensed under the MIT License. See the LICENSE file for details.

---

We hope StreamNotifier enhances your streaming experience by keeping your community engaged with timely and informative Discord notifications. Happy streaming!

