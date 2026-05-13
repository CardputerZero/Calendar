# Cardputer Zero Calendar

Calendar is a 320 x 170 LVGL APPLaunch application for Cardputer Zero.

## Features

- Split layout with the month grid on the left and selected-day details on the right.
- Default local data plus multiple online ICS calendar subscriptions.
- Built-in optional feeds for China holidays, almanac, and Shenzhen weather.
- Calendar manager for enabling or disabling sources, lunar display, sync, and language override.
- UI strings in English, Simplified Chinese, and Japanese.
- Language defaults to the system locale and can be overridden in the manager.
- Long detail text wraps and auto-scrolls on the 320 x 170 display.

## Controls

- Arrow keys move the selected day. F/Z/X/C also map to Up/Left/Down/Right.
- Page Up and Page Down move by month.
- Ctrl + < and Ctrl + > move by month; Alt + < and Alt + > move by year.
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

Deploy to a reachable Cardputer Zero:

```bash
./deploy_cardputerzero.sh <device-ip> [ssh-user]
```

APPLaunch package staging can be generated with the Cardputer Zero application skill helper after the binary exists.
