# Third-party components in Reference Clipper release builds

| Component | License | Source |
|---|---|---|
| Qt 6 (Core, Gui, Widgets, Svg and plugins) | LGPL-3.0 | https://download.qt.io/official_releases/qt/ |
| FFmpeg (BtbN "lgpl-shared" build, with SVT-AV1) | LGPL-2.1-or-later; see FFmpeg-LICENSE.txt | https://github.com/BtbN/FFmpeg-Builds, https://ffmpeg.org |
| SVT-AV1 (inside the FFmpeg libraries) | BSD-3-Clause-Clear | https://gitlab.com/AOMediaCodec/SVT-AV1 |
| Barlow and Barlow Condensed fonts | SIL Open Font License 1.1; see OFL.txt | https://github.com/jpt/barlow |

The Qt and FFmpeg libraries are dynamically linked and shipped as separate
files, so they can be replaced with other builds of the same versions.
On Linux the AppImage may also bundle system libraries (such as libpulse and
the X11/xcb libraries) from Ubuntu 22.04 under their own licenses.
