# Cardputer Zero Calendar

Calendar is a 320 x 170 LVGL APPLaunch application for Cardputer Zero.

## Features

- Split layout with the month grid on the left and selected-day details on the right.
- Default local data plus multiple online ICS calendar subscriptions.
- Calendar manager for enabling or disabling sources, lunar display, sync, and language override.
- UI strings in English, Simplified Chinese, and Japanese.
- Language defaults to the system locale and can be overridden in the manager.
- Weather can be added as an ICS feed from a weather-calendar service.

## Controls

- Arrow keys move the selected day.
- Page Up and Page Down move by month.
- Tab cycles source filters.
- Enter opens or activates the manager.
- Esc returns from manager/input screens; long Esc exits.

## Configuration

Runtime configuration is stored at:

```text
~/.config/cardputerzero-calendar/config.txt
```

ICS downloads are cached under:

```text
~/.cache/cardputerzero-calendar/
```

The app accepts `webcal://`, `https://`, and `http://` ICS URLs. Apple and Google calendar subscription URLs work when they expose an ICS feed.

## Build

Local SDL build:

```bash
scons -j1
```

Cardputer Zero cross build:

```bash
CardputerZero=y scons -j1
```

APPLaunch package staging can be generated with the Cardputer Zero application skill helper after the binary exists.
