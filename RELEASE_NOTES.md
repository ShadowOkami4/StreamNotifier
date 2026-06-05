# StreamNotifier 4.0.0

Version 4.0 is the first native OBS Studio plugin release of StreamNotifier.
The old Python script remains in the repository as a legacy reference, but the
maintained version is now the C++/Qt plugin.

## Highlights

- Native C++/Qt OBS frontend plugin.
- Compact StreamNotifier dock for quick status, enable/disable, settings, and
  manual sends.
- Full settings editor under `Tools > StreamNotifier Settings`.
- Discord Components V2 message builder.
- Multiple saved messages.
- Per-message OBS triggers:
  - stream start
  - stream stop
  - recording start
  - recording stop
- Local reusable templates.
- Discohook preview/export for the selected message.
- Advanced JSON editor for manual Components V2 payload changes.
- ClangCL Windows build support through the OBS plugin template build system.

## Upgrade Notes

- Version 4.0 no longer needs Python or the `discord_webhook` Python package.
- Existing old embed settings are migrated into a default Components V2 message
  when possible.
- Configure the plugin through `Tools > StreamNotifier Settings`.

## Install

Copy the release files into your OBS plugin folder, then restart OBS.

Portable OBS layout:

```text
OBS-Studio\
  obs-plugins\64bit\stream-notifier.dll
  data\obs-plugins\stream-notifier\locale\en-US.ini
```

Source repository:

https://github.com/ShadowOkami4/StreamNotifier
