# CardputerZero Calendar

Calendar is a 320 x 170 LVGL APPLaunch application for CardputerZero.

## Features

- Split layout with the month grid on the left and selected-day details on the right.
- Multiple online ICS calendar subscriptions.
- Built-in optional subscriptions for local date data, lunar display, China, Japan, US, UK, Germany, and France holidays, plus almanac. Built-in subscriptions are disabled by default.
- Separate subscription manager for adding, deleting, enabling, language-ordering, and styling subscriptions.
- Subscription styles can mark matching dates with split border colors and horizontally split background colors when multiple subscriptions match the same date.
- Loading screen appears while online subscriptions are fetched after adding, enabling, or syncing a source.
- UI strings in English, Simplified Chinese, and Japanese.
- Language defaults to the system locale and can be overridden in the manager.
- Long detail text wraps and auto-scrolls on the 320 x 170 display.

## Controls

- Arrow keys move the selected day. F/Z/X/C also map to Up/Left/Down/Right.
- Page Up and Page Down move by month.
- Ctrl + < and Ctrl + > move by month; Alt + < and Alt + > move by year.
- Tab cycles source filters.
- Enter opens or activates the manager. In the manager, open Subscriptions to manage calendar sources.
- In Subscriptions: Enter opens the selected source editor, A adds an ICS URL, and D deletes ICS sources, including optional built-in holiday/almanac feeds.
- In the source editor: Enter changes the highlighted field. Use the Border color and Background color rows to cycle colors; enabling a color row also turns that visual style on.
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

### Fonts

Calendar follows the CardputerZero system font contract:

- DejaVu Sans Regular at 13 px for normal UI text and 15 px for headings.
- JetBrains Mono Regular at 13 px for ICS URLs and other technical text.
- Noto Sans CJK SC for Simplified Chinese and Noto Sans CJK JP for Japanese.

Calendar loads explicit regional OTF/TTF files. It does not use a generic Noto
CJK TTC or substitute the Simplified Chinese face for Japanese. Missing fonts
are reported on stderr with the corresponding override variable:

```text
M5_CALENDAR_FONT_LATIN
M5_CALENDAR_FONT_MONO
M5_CALENDAR_FONT_ZH
M5_CALENDAR_FONT_JA
```

`M5_CALENDAR_FONT` remains available as a deprecated explicit CJK override.
Point an override at the exact regional font file required by the selected
language.

## Build

Local SDL build:

```bash
scons -j1
```

CardputerZero cross build:

```bash
CardputerZero=y scons -j1
```

Deploy to a reachable CardputerZero:

```bash
./deploy_cardputerzero.sh <device-ip> [ssh-user]
```

APPLaunch package staging can be generated with the CardputerZero application skill helper after the binary exists.
