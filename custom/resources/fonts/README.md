# Camera control typeface

`QGCCameraSans-SemiBold.ttf` is a modified static subset of Noto Sans SC at
weight 600. It is used for Rouke parameter names, input values, option labels
and action buttons. Explanatory text, status reports and units retain the
application font. The font is embedded in QRC and requires no device install
or runtime download.

## Source and license

- Upstream: [Google Fonts / Noto Sans SC](https://github.com/google/fonts/tree/a85815a42757630ce188fdad368c2dfc444d4773/ofl/notosanssc)
- Source: [NotoSansSC[wght].ttf](https://raw.githubusercontent.com/google/fonts/a85815a42757630ce188fdad368c2dfc444d4773/ofl/notosanssc/NotoSansSC%5Bwght%5D.ttf)
- Source SHA-256: `a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da`
- License: SIL Open Font License 1.1; the complete upstream notice is in
  `OFL.txt` and is packaged alongside the font.

The subset retains the available Latin characters in U+0020–U+024F, GB2312
characters and general/CJK/fullwidth punctuation. Other writing systems use
Qt's normal font fallback. The modified family is named **QGC Camera Sans**;
the upstream copyright and licensing records are retained inside the font.

## Regeneration

This is an asset maintenance step, not part of a normal QGC build. Download
the pinned source above, then run from the repository root with `fontTools`
installed (generated using version 4.25.0):

```text
python custom/tools/generate_camera_font.py <NotoSansSC-variable.ttf> custom/resources/fonts/QGCCameraSans-SemiBold.ttf
```

The checked-in static font is 2,355,980 bytes. Its SHA-256 is
`943772d5102829a0278be9203b4be479692db04407a1fcf11a5c7c807cc16671`.
