# Chinese fallback font

## Readable firmware fonts (development)

`pet_font_22.c`, `pet_font_28.c` and `pet_font_30.c` derive from Noto Sans SC Medium, copyright © 2014-2021 Adobe. Source revision: `notofonts/noto-cjk` commit `f8d157532fbfaeda587e826d4cd5b21a49186f7c`, `Sans/SubsetOTF/SC/NotoSansSC-Medium.otf`, SHA-256 `7633f5a016d4dd95e685a69633d818aabc4644c4b08e26bd35b1b30c45ed5dda`. These are 2bpp grayscale fonts under **OFL-1.1**, separately from application code; retain [NotoSansSC-OFL-1.1.txt](../../../../../docs/third-party/NotoSansSC-OFL-1.1.txt).

The 28px set covers GB2312's 6763 Han ideographs, ASCII and common punctuation. The 30px set covers the 3755 level-1 Han ideographs and the same punctuation; the remaining 3008 Han fall back to28px grayscale, keeping the existing application slot. The 22px set covers the controlled UI copy. `pet_font_rare16.c` contains the original Unifont characters not present in the new main set, preserving the original union of characters without duplicate bitmap storage. Rare characters retain the existing 16px pixel style. The original `muse_font_cjk_16.c` remains as the host coverage reference; the UI integration removes its runtime linkage.

`provenance.json` records the exact conversion results, pixel metrics, bitmap sizes, charset and generated-source hashes. Regenerate using verified source fonts and a local `lv_font_conv@1.5.3`:

```sh
node scripts/generate-pet-fonts.mjs --noto /path/NotoSansSC-Medium.otf --unifont /path/unifont-16.0.04.otf --converter /path/lv_font_conv.js
```

The script does not fetch fonts or install tools. It validates source hashes and converter version before writing outputs. `chat-characters.txt` is the Unicode Han set decoded from GB2312; `reading-characters.txt` is its level-1 set; `ui-characters.txt` is controlled interface copy. Regenerate the latter when adding new interface text. Real LVGL host tests check grayscale rendering and compare new fallback coverage with the original font. Production enables compressed-font decoding and uses one software draw unit because pinned LVGL9.5.0 keeps shared RLE decoder state. Compile-time guards reject decoder-off or multiworker configurations; tests also check the production default and compile an unsafe configuration to prove rejection. Final target image size and physical-screen acceptance are separate checks.

## Original full reference

`muse_font_cjk_16.c` contains 16 px, 1 bpp GNU Unifont 16.0.04 glyphs for
U+3000–30FF, U+4E00–9FFF and U+FF00–FFEF, converted to LVGL C format.
This full conversion is a host-test reference. The production UI links only
the deduplicated rare16 fallback; the legacy Muse `CONFIG_MUSE_CJK_FONT`
setting is not required.

Copyright © 1998-2025 Roman Czyborra, Paul Hardy, Qianqian Fang, Andrew Miller,
Johnnie Weaver, David Corbett, Nils Moskopp, Rebecca Bettencourt, Ho-Seok Ee, et al.

The original glyphs are an unaltered subset; this repository distributes the
font conversion under **SIL Open Font License 1.1**, separately from the
application's Apache-2.0 license. Preserve the copyright and full license:
[OFL-1.1](../../../../../docs/third-party/Unifont-OFL-1.1.txt),
[upstream COPYING](../../../../../docs/third-party/Unifont-16.0.04-COPYING.txt).

Upstream: <https://unifoundry.com/pub/unifont/unifont-16.0.04/font-builds/>.
The source OTF SHA-256 is
`0e3981ab552231b5a2a870f2b61741903a4bf25c23ef5aeb05fdced1b3c7af4d`.

To regenerate, obtain that exact upstream OTF and an LVGL-compatible
`lv_font_conv`, then use the conversion arguments already recorded in the C
file header (size 16, bpp 1, no compression, the three ranges, and
`--lv-font-name muse_font_cjk_16`). Preserve the notice and OFL after conversion.
No external private SDK or missing local generation script is required.
