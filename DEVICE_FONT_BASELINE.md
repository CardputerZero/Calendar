# CardputerZero device font baseline

This document records the fonts present on the physical CardputerZero before a
system reflash. It is intended as a reproducible baseline for Calendar font
regression checks.

## Capture environment

- Captured at: `2026-08-12T14:06:05+08:00`
- Device address at capture time: `192.168.20.229`
- Hostname: `cp0`
- OS: Debian GNU/Linux 13 (trixie)
- Kernel: `Linux 6.18.34+rpt-rpi-v8 aarch64 GNU/Linux`
- Display used for the Calendar check: `/dev/fb0`, 320 x 170, RGB565

The image has two independent font locations:

1. Debian/fontconfig fonts under `/usr/share/fonts`.
2. APPLaunch shared fonts under `/usr/share/APPLaunch/share/font`.

The APPLaunch directory is not registered with fontconfig on this image.
Applications such as Calendar must load those files directly by path.

## APPLaunch shared fonts

These files were already present on the image before Calendar was deployed.
The Calendar deployment did not replace `NotoSansSC-Regular.ttf` because the
device file was byte-for-byte identical to the project copy.

| Font | Bytes | SHA-256 | Path |
| --- | ---: | --- | --- |
| Alibaba PuHuiTi 3.0 35 Thin | 8,548,544 | `585e94adb79dca7fe448825e328a5ddac4226de487e6770b0812799865e14e4f` | `/usr/share/APPLaunch/share/font/AlibabaPuHuiTi-3-35-Thin.ttf` |
| Alibaba PuHuiTi 3.0 55 Regular | 8,532,824 | `be33bc8d45ced30fa5c542f6f056565baf85003d4e079522110d76016f8a04e2` | `/usr/share/APPLaunch/share/font/AlibabaPuHuiTi-3-55-Regular.ttf` |
| Liberation Mono Regular | 319,508 | `6b3809450cf6253b36d157198dc15004a5fbade9abad5543c377feb7bb29139c` | `/usr/share/APPLaunch/share/font/LiberationMono-Regular.ttf` |
| Montserrat Bold | 29,560 | `9cb7dc18ee6175ab86bea008eb7aff1992ea7b06933964d5e2e864090206c20a` | `/usr/share/APPLaunch/share/font/Montserrat-Bold.ttf` |
| Noto Sans SC Regular | 10,560,380 | `ae82f4e2a55e1316a55bcc1d05e9555ce08d8bda07e893b486896b626fd852ff` | `/usr/share/APPLaunch/share/font/NotoSansSC-Regular.ttf` |
| APPLaunch icon font (`svgfont`) | 3,288 | `223046b653523d24bdca24120d9c7f7fb69ff62cc777c3310f42de814dd4e5b0` | `/usr/share/APPLaunch/share/font/svgfont.ttf` |

Compatibility link used by Calendar:

```text
/usr/share/APPLaunch/fonts/NotoSansSC-Regular.ttf
  -> ../share/font/NotoSansSC-Regular.ttf
```

## Key Debian/fontconfig fonts

Installed package versions:

| Package | Version |
| --- | --- |
| `fonts-dejavu-core` | `2.37-8` |
| `fonts-droid-fallback` | `1:8.1.0r7-1~1.gbp36536b` |
| `fonts-freefont-ttf` | `20211204+svn4273-2` |
| `fonts-liberation` | `1:2.1.5-3` |
| `fonts-noto-mono` | `20201225-2` |

| Font | Bytes | SHA-256 | Path |
| --- | ---: | --- | --- |
| Droid Sans Fallback | 4,033,420 | `acb6440a713d880a13a21b468ba7cd43f5a2b2934972e51be791c880730777b8` | `/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf` |
| DejaVu Sans | 759,720 | `57f73e11f51999432bf7ab22ce55b6f945d5eca1bf824404cfa9ec2e3718c84e` | `/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf` |
| FreeSans | 1,844,796 | `ad5b4b55537a7ef413aa7cbae8e6e9104030f4f1588db66d8907e3724298cb85` | `/usr/share/fonts/truetype/freefont/FreeSans.ttf` |
| FreeSerif | 3,686,360 | `c1dd2270ff624d66a7f1cd23075b5ce05a662f560be06a70a9599065ce008355` | `/usr/share/fonts/truetype/freefont/FreeSerif.ttf` |
| Noto Sans Mono | 510,612 | `6b692c4b6d15ccf59f1c1fe8d11cb8a92f51960f3e9f1f523781755a3af7e29f` | `/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf` |

## Measured Unicode coverage

Counts below were calculated from each font's `fc-query` charset. `CJK` is the
Basic CJK Unified Ideographs block (`U+4E00-U+9FFF`), not all CJK extensions.

| Font | Total code points | Hiragana | Katakana | Basic CJK | Hangul syllables |
| --- | ---: | ---: | ---: | ---: | ---: |
| Noto Sans SC Regular | 30,890 | 93 / 96 | 96 / 96 | 20,976 / 20,992 | 0 / 11,184 |
| Alibaba PuHuiTi 3.0 | 29,195 | 90 / 96 | 93 / 96 | 20,976 / 20,992 | 0 / 11,184 |
| Droid Sans Fallback | 28,600 | 90 / 96 | 91 / 96 | 20,902 / 20,992 | 3 / 11,184 |
| FreeSerif Regular | 10,305 | 0 / 96 | 0 / 96 | 0 / 20,992 | 0 / 11,184 |
| FreeSans Regular | 6,917 | 0 / 96 | 0 / 96 | 0 / 20,992 | 0 / 11,184 |
| DejaVu Sans Book | 5,918 | 0 / 96 | 0 / 96 | 0 / 20,992 | 0 / 11,184 |

Important conclusions from this image:

- `NotoSansSC-Regular.ttf` is the best available Simplified Chinese UI font.
- Alibaba PuHuiTi 3.0 does not improve Japanese or Korean coverage over Noto
  Sans SC. It covers the same number of basic CJK ideographs, slightly fewer
  Japanese kana, and no Hangul syllable block.
- There is no dedicated Noto Sans JP or Noto Sans KR font.
- There is no color Emoji font. DejaVu Sans supplies only a limited set of
  monochrome emoji/symbol glyphs.

## Fontconfig sample fallback results

These results apply only to fonts registered with fontconfig. They do not
include the APPLaunch shared directory.

| Sample code point | Fontconfig match |
| --- | --- |
| Chinese `U+4E2D` | Droid Sans Fallback |
| Hiragana `U+3042` | Droid Sans Fallback |
| Hangul `U+AC00` | Droid Sans Fallback, but the font has almost no Hangul coverage |
| Arabic `U+0627` | DejaVu Sans |
| Hebrew `U+05D0` | DejaVu Sans |
| Devanagari `U+0915` | FreeSans |
| Thai `U+0E01` | FreeSerif |
| Emoji `U+1F600` | DejaVu Sans, monochrome |

## Calendar behavior captured on this image

Calendar `0.1.3` was tested on the real framebuffer with `LANG=zh_CN.UTF-8`.
The runtime log reported:

```text
Calendar font loaded (zh): /usr/share/APPLaunch/bin/../fonts/NotoSansSC-Regular.ttf
```

That path resolves through the compatibility link to the APPLaunch shared Noto
Sans SC file listed above. Calendar creates 13 px and 15 px LVGL Tiny TTF fonts
from it. Chinese month and manager screens rendered without boxes, missing
glyphs, or mojibake.

The Calendar build tested during this baseline used these language choices:

- Chinese: runtime Noto Sans SC, then built-in Source Han Sans SC fallback.
- Japanese: tries Noto Sans JP/Noto Sans CJK first; neither exists on this
  image, so it falls back to the shared Noto Sans SC file.
- English: built-in LVGL Montserrat 10 px body and 14 px title fonts (older
  baseline binary; the current build uses 13 px body and 15 px title fonts).

The Calendar `0.1.3` Debian package does not contain a font payload. Full
runtime Noto Sans SC coverage therefore depends on the system image's shared
font. If the shared font is absent after a reflash, Calendar should still use
its built-in Source Han Sans SC fallback, but the rendering and coverage may
differ.

## Source font policy after this baseline

The Calendar source was subsequently aligned with the CardputerZero system
font contract. This changes the next build, not the already captured `0.1.3`
binary described above:

- General UI text loads DejaVu Sans Regular at 13 px; headings use 15 px.
- ICS URLs and other technical text load JetBrains Mono Regular at 13 px.
- Simplified Chinese loads an SC-specific Noto Sans CJK face.
- Japanese loads a JP-specific Noto Sans CJK face. It no longer falls through
  to Noto Sans SC.
- Generic Noto CJK TTC files are not auto-selected because LVGL Tiny TTF opens
  TTC face index 0 and cannot guarantee the requested regional face.
- Missing required system fonts are reported explicitly instead of silently
  selecting a wrong regional face.

The existing 8 px event counter is retained as a nonessential compact
indicator. Normal interface text remains 13 px or larger and uses Regular
weight.

## Post-reflash comparison commands

Run the following on the device after reflashing:

```sh
cat /etc/os-release
uname -srmo

find /usr/share/APPLaunch/share/font -maxdepth 1 -type f \
  -exec stat -c '%s\t%n' {} \; \
  -exec sha256sum {} \;

dpkg-query -W -f='${Package}\t${Version}\n' \
  fonts-dejavu-core fonts-droid-fallback fonts-freefont-ttf \
  fonts-liberation fonts-noto-mono

fc-match -f '%{family[0]}\t%{style[0]}\t%{file}\n' 'DejaVu Sans:style=Regular'
fc-match -f '%{family[0]}\t%{style[0]}\t%{file}\n' 'JetBrains Mono:style=Regular'
fc-match -f '%{family[0]}\t%{style[0]}\t%{file}\n' 'Noto Sans CJK SC:style=Regular'
fc-match -f '%{family[0]}\t%{style[0]}\t%{file}\n' 'Noto Sans CJK JP:style=Regular'

fc-match -f '%{family[0]}\t%{file}\n' ':charset=4e2d'
fc-match -f '%{family[0]}\t%{file}\n' ':charset=3042'
fc-match -f '%{family[0]}\t%{file}\n' ':charset=ac00'
fc-match -f '%{family[0]}\t%{file}\n' ':charset=1f600'

readlink -f /usr/share/APPLaunch/fonts/NotoSansSC-Regular.ttf
sha256sum /usr/share/APPLaunch/share/font/NotoSansSC-Regular.ttf
```

For the final visual regression check, run Calendar with a clean configuration
and `LANG=zh_CN.UTF-8`, then capture the 320 x 170 framebuffer month and manager
screens.
