# Chinese fallback font

`muse_font_cjk_16.c` contains 16 px, 1 bpp GNU Unifont 16.0.04 glyphs for
U+3000–30FF, U+4E00–9FFF and U+FF00–FFEF, converted to LVGL C format.
The independent pet UI links this fallback directly; the legacy Muse
`CONFIG_MUSE_CJK_FONT` setting is not required.

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
