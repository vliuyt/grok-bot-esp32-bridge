# Readability prototype font

`xiaotang-ui-preview.woff` is a renamed, text-only subset of **Noto Sans SC Medium** for the static readability design prototype. It contains 400 glyphs and is not a general-purpose Chinese font or a device firmware asset. The fixed example's 🙂 character uses the browser's emoji fallback.

Upstream: <https://github.com/notofonts/noto-cjk>, commit `f8d157532fbfaeda587e826d4cd5b21a49186f7c`, `Sans/SubsetOTF/SC/NotoSansSC-Medium.otf`.

The source font copyright and license name records are retained in the derivative. Full source and derivative hashes, scope and changes are in [provenance.json](provenance.json). The derivative family and CFF names are `Xiaotang UI Preview` / `XiaotangUIPreview-Medium`.

Original font copyright: © 2014-2021 Adobe (http://www.adobe.com/).

The font is distributed under **SIL Open Font License 1.1**, separately from the application's Apache-2.0 license. Preserve [OFL.txt](OFL.txt) and the font's copyright name records with any copy.

Generation used FontTools 4.66.1: subset the exact source with ASCII, the Unicode characters in `readability.html` and `readability.mjs`, and all labels/modes in `../states.json`'s `captionCases`; preserve all name/license records, rename family/style/unique/PostScript records and CFF font names, and save in a WOFF container. The source OTF is development input only and is not distributed here. Text added to the prototype may require updating this subset.

Regeneration must also update `provenance.json` and the exact approved font digest in `scripts/check-publication.py`; the publication guard requires the matching OFL file in the publication file set. Other binary fonts remain refused.

The firmware still uses its existing Unifont conversion. A production replacement needs actual LVGL font conversion, appropriate Chinese coverage, image-size checks, and physical-screen acceptance; a browser WOFF does not establish any of those results.
