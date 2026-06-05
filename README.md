# StreamNotifier

Welcome to **StreamNotifier**, a native OBS Studio plugin that sends Discord
Webhook Messages from OBS events. Version 4.0 replaces the old Python script
with a full C++/Qt plugin, Discord Components V2 support, a compact OBS dock,
and a full message builder inside OBS.

We hope you find it useful!

## Features

- **Send Discord Webhook Messages**: Automatically send messages to a specified
  Discord channel when OBS events happen.
- **Multiple OBS Triggers**: Send messages on stream start, stream stop,
  recording start, and recording stop.
- **Multiple Messages**: Create and manage more than one Discord message.
- **Discord Components V2 Support**: Build modern Discord webhook messages with
  containers, text blocks, separators, media galleries, sections, action rows,
  and link buttons.
- **Compact OBS Dock**: Quickly check plugin status, enable or disable
  automatic sends, open settings, or manually send all messages.
- **Full Settings Window**: Open `Tools > StreamNotifier Settings` for the full
  Components V2 message builder.
- **Reusable Templates**: Save your own message templates and load them again
  later.
- **Discohook Preview**: Open the selected message in Discohook to preview and
  fine-tune the generated JSON visually.
- **Advanced JSON Editing**: Edit the generated Components V2 payload directly
  when you need fields that are not exposed in the builder yet.

## Installation

### Plugin Release

1. Download the latest StreamNotifier release from
   [GitHub](https://github.com/ShadowOkami4/StreamNotifier).
2. Copy the plugin files into your OBS plugin folder.
3. Restart OBS Studio.
4. Open `Docks > StreamNotifier` for the compact dock.
5. Open `Tools > StreamNotifier Settings` to configure your webhook and
   messages.

For a portable OBS installation, the plugin files should be placed like this:

```text
OBS-Studio\
  obs-plugins\64bit\stream-notifier.dll
  data\obs-plugins\stream-notifier\locale\en-US.ini
```

### Build From Source

StreamNotifier 4.0 uses the official OBS plugin template build system. On
Windows, use Visual Studio Build Tools with the `ClangCL` toolset.

```powershell
.\scripts\Use-ClangClDevEnv.ps1
cmake --preset windows-clangcl-x64
cmake --build --preset windows-clangcl-x64
```

Install into a portable OBS folder:

```powershell
cmake --install build_x64 --config RelWithDebInfo --prefix "E:\OBS-Studio"
```

If CMake already cached a failed configure attempt, rerun with a fresh cache:

```powershell
cmake --fresh --preset windows-clangcl-x64
```

If CMake cannot find Windows libraries such as `kernel32.lib`, `user32.lib`, or
`oldnames.lib`, make sure the Visual Studio C++ build tools and Windows SDK are
installed, then run `scripts\Use-ClangClDevEnv.ps1` before configuring.

## Usage

### Dock

The `Docks > StreamNotifier` panel is the quick control surface.

- **Status Card**: Shows whether the plugin is ready, paused, or still needs a
  webhook.
- **Enable automatic sends**: Turns OBS event notifications on or off.
- **Settings**: Opens the full configuration window.
- **Send All**: Manually sends all configured messages.

### Settings

Open `Tools > StreamNotifier Settings` to configure the plugin.

#### Connection

- **Webhook**: The Discord webhook URL used for sending messages.
- **Save**: Saves all settings.
- **Send All**: Sends all configured messages manually.

#### Templates

- **Load**: Loads a saved template into the selected message.
- **Save Current**: Saves the selected message as a new template.
- **Update**: Updates the selected template from the current message.
- **Delete**: Removes the selected template.

#### Messages

- **Add**: Creates a new message.
- **Duplicate**: Copies the selected message.
- **Delete**: Removes the selected message.
- **Move Up / Move Down**: Changes message order.

#### Send This Message On

Each message can be sent automatically on one or more OBS events:

- Stream start
- Stream stop
- Recording start
- Recording stop

#### Components V2 Builder

The builder lets you add and edit Discord Components V2 elements:

- Container
- Text Block
- Separator
- Media Gallery
- Link Button Row
- Section
- Link Button

#### Advanced JSON

The Advanced JSON tab shows the generated Discord Components V2 JSON. You can
edit it directly if you need more control.

#### Discohook Preview

Use **Open in Discohook** to open the selected message in
[Discohook](https://discohook.app/) with the current JSON payload loaded.

## Changelog

### Version 4.0

- Rebuilt StreamNotifier as a native OBS Studio C++/Qt plugin.
- Added a compact OBS dock and full Tools menu settings window.
- Replaced the old embed-only system with Discord Components V2.
- Added multiple messages.
- Added per-message triggers for stream and recording events.
- Added reusable message templates.
- Added Discohook preview/export support.
- Kept the old Python script in the repository as a legacy reference.

### Version 3.0

- **Renamed**: OBScord became StreamNotifier.
- **Dynamic Fields**: Added support for up to 10 dynamic fields in the old
  Python script embed system.

## License

This project is licensed under the **GNU General Public License v2.0**. See the
[LICENSE](LICENSE) file for details.

---

We hope StreamNotifier enhances your streaming experience by keeping your
community engaged with timely and informative Discord notifications. Happy
streaming!
