# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Reference Clipper is a Linux desktop app (C++20, Qt 6 Widgets, FFmpeg libraries). It opens a video, lets the user mark spans on the timeline, and encodes them back-to-back into one MP4 (AV1 + AAC) with guaranteed A/V sync.

`design_handoff_reference_clipper/` is the spec: `README.md` is authoritative, and the logic in `Reference Clipper.dc.html` fills gaps such as the size-estimate formula. It is a design reference, not code to build on. Do not edit it.

## Commands

```sh
make build                                         # the dev build below
make run                                           # builds, then launches the app

cmake --preset dev && cmake --build --preset dev   # Debug + ASan/UBSan, in build/
ctest --preset dev                                 # all tests, needs the dev build
./build/reference-clipper [file]                   # run the app

cmake --preset release && cmake --build --preset release   # in build-release/

# One test binary or one test function (Qt Test)
QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_encode
QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_encode cutsAreFrameAccurateAcrossSpans

# Screenshot every key UI state, to compare with the design
QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=0 RC_SCREENSHOT_DIR=/some/dir ./build/tests/test_ui
```

Needs Qt ≥ 6.8, the FFmpeg dev libraries with `libsvtav1`, and `libpulse-simple` (Linux; Windows uses `winmm`). `-DFFMPEG_ROOT=<dir>` builds against a prebuilt FFmpeg tree instead of pkg-config.

Release builds come from `.github/workflows/release.yml`. On every push it builds and tests a Windows zip (`packaging/build-windows.sh`: Qt's llvm-mingw build, under Git Bash) and a Linux AppImage (`packaging/build-appimage.sh`: inside `ubuntu:22.04`, which sets the oldest glibc it runs on). Pushes to `mastah` also publish both as a GitHub release. Dependency versions live in `packaging/versions.sh`. The packages link BtbN's LGPL FFmpeg so the download stays LGPL; tests make their fixtures with the GPL `ffmpeg` (for libx264), passed in through `$FFMPEG`. Tests also need the `ffmpeg` CLI with `libx264`, which generates fixtures into a temp dir at test time. Developed against FFmpeg 9.0 and SVT-AV1 4.2.

## Architecture

Three static libraries, with dependencies pointing strictly downward:

- `src/core` (`rc_core`): pure C++ with no Qt or FFmpeg. Spec formulas, span edits, playback stepping, the source-to-output map, the config format.
- `src/media` (`rc_media`): everything that touches libav*, with no Qt. Blocking calls; cancellation and progress go through `std::atomic` and `std::function`.
- `src/app` (`rc_app`): the Qt UI, a library so tests can drive the real `MainWindow`.

`Session` holds all application state and every user action. Widgets read from it, call its actions and connect to its signals; they do not talk to each other or call the media layer for edits. All slow work runs off the UI thread and posts back with `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`: `FrameBroker` (frames, coalescing to the newest request), `ClipAssets` (filmstrip, waveform), `GalleryScanner`, `JobQueue` (one encode at a time), `SceneSearch` (one search at a time), `AudioPreview`. Each joins its thread in its destructor, so anything added there must stay cancellable or quitting hangs.

### Invariants worth knowing before changing the encoder

- **Video is the master timeline.** Each span snaps to the real frames inside it (`encoder.cpp`), so its output length is a whole number of source ticks. Audio is then cut to exactly that length, counted in source-rate samples. Two `StreamReader`s, one per stream, advance in lockstep so the muxer is fed interleaved even though spans seek independently.
- **`EncodeResult::segments` is the truth about a finished encode**, not the marked spans: they differ by up to a frame per edge. `OutputMap` is built from segments, and history stores both.
- **Clip time 0 is the first video frame** (`StreamReader::origin`), for audio too. Never use raw stream timestamps or the container's duration field; Matroska's is really an end timestamp, so `probeClip` reads the last packets.
- **`seekBefore` targets can be negative on purpose.** AAC priming packets sit before 0, and landing after them loses the first frame of real audio.
- **Video packets need a duration set** (`drainEncoder`). libsvtav1 emits none, and an MP4's final sample would get length 0 and vanish from playback.
- **Drift is measured with priming excluded** (`probeDrift`). Priming is hidden by the edit list; counting it would fail every job above ~47 fps.

### Crop

A crop is one static rectangle in **displayed source pixels**: after rotation, with square pixels, and even in every value (`core/crop.h`). Everything downstream of it treats the cropped size as the source: `Session::outputSize`, the estimate, resolution presets. There are two pipelines that must agree: the encoder's filter chain (rotate, crop, scale) and `croppedFrame` for previews (maps back to stored pixels). `cropAppliesToTheUprightPictureInBothPipelines` checks both against the `ffmpeg` CLI's autorotated picture for every rotation; change one and that test tells you about the other. It also applies to scene detection, so a cut inside a 4:3 inset counts while a static 9:16 surround is ignored.

### Scene detection

`src/media/scene.cpp` finds hard cuts on demand, outward from a click in growing chunks, rather than scanning a whole clip on open. A cut is a spike in frame difference where one frame step accounts for most of the change between the medians of the frames before and after ("explained", which rejects animation), and most of that step survives ("persisted", which rejects flashes). Ordinary threshold detection fails on fixed-background footage: ffmpeg's `scene >= 0.3` finds none of the 16 cuts in the clip this was tuned on. If you touch the constants, re-measure on real footage with hand-verified cuts; the synthetic fixtures pass far looser settings.

### Behaviour that is deliberate

- Spans are half-open `[start, end)`, matching the encoder. Overlaps are allowed and the shared region appears twice, in both output and playback; `Playback` tracks a span index for this reason.
- Times from the UI go through `snapToFrame`, which floors to the displayed frame. The encoder drops `pts < start`, so without it an in-point set on the visible frame would lose that frame.
- A finished output only drives the preview while the spans and crop still match it (`Session::finishedOutput`). Any edit to either returns the output pane to seek preview.
- Right-click adds a one-frame span immediately and the search grows it, so the click responds at once and the edges can be seen moving. Editing or removing that span abandons its search: the user's hand wins. Inside an existing span, right-click only selects it. A false cut costs a short span, which is why detection favours a usable result over more tuning.
- Scene edges are real frame times and are not passed through `snapToFrame`, which assumes constant frame rate.
- Encodes write `<output>.part` and rename on success, so failed and cancelled jobs leave nothing at the output path.
- `crfForQuality` uses integers on purpose: q=70 and q=90 land on exact .5 ties that floats round inconsistently.
- Audio uses `libpulse-simple` (served by PipeWire too), not QtMultimedia; see the comment in `audio_preview.h`.

### Where this differs from the prototype

The prototype is a mock, so these are intentional: clips open with no spans rather than two demo spans; quality hints follow the README's `<35/<60/<85` boundaries; the frame label sits in the corner rather than over the image being inspected; the `DECODED AV1` badge keeps a dark fill in dark theme so white text stays readable; the status bar shows real library versions; dialogs are native. Added beyond the spec: `failed` job status, a confirmation before replacing a file, one before quitting mid-encode, right-click span creation, scene detection and crop.

## Testing notes

- `test_encode` measures decoded output instead of trusting the encoder: its fixture flashes a white frame and beeps on each whole second, and the tests compare where those land. When one fails, confirm with `ffmpeg`/`ffprobe` whether the file or the reader is wrong; a reader bug can look exactly like a sync bug.
- `test_ui` points `HOME`, XDG paths and `PULSE_SERVER` at nowhere, so it never touches real config, `~/Videos` or the speakers. Keep it that way.
- Offscreen tests cannot catch real-platform problems (the audio path, shutdown). To exercise the real binary, run it under `xvfb-run` with `XDG_CONFIG_HOME`/`XDG_DATA_HOME` redirected and focus the window with `xdotool windowfocus`, since Xvfb has no window manager.
