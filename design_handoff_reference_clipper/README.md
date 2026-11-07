# Handoff: Reference Clipper (Linux desktop)

## Overview
Reference Clipper opens a video file, lets the user mark one or more spans (extents) on the source timeline, and encodes those spans back-to-back into one MP4 (AV1 video + AAC audio) at a chosen resolution and quality, with guaranteed A/V sync. A side-by-side preview shows the input frame and the output frame; before encoding the output pane is a seek-preview of the source, after encoding it is the decoded output file so artifacts can be inspected.

## About the Design Files
`Reference Clipper.dc.html` and `industry.css` are **design references created in HTML**: an interactive prototype that shows intended look and behavior. They are not production code. The task is to recreate the design natively on Linux using a C++ build with FFmpeg libraries (libavformat/libavcodec/libavfilter/libswresample/libswscale). Recommended stack: **Qt 6 / KDE Frameworks 6 (Kirigami or QtWidgets)** because the requested feel is KDE-like and the UI is panel-based; Godot 4 (GDExtension in C++) is an acceptable alternative if the team prefers it. Keep the layout, information architecture and interactions as specified below; map the visual tokens to the KDE/Breeze equivalents where a native widget already exists.

## Fidelity
**High-fidelity.** Layout, spacing, type, colors and interaction states are intended. Where a native toolkit widget covers a control (menubar, file dialog, sliders, segmented radios), prefer the native widget and apply the tokens; do not pixel-copy HTML chrome.

## Window structure (root grid)
Rows: `menubar (auto)` · `toolbar (auto)` · `main (1fr)` · `statusbar (auto)`. Min window width 1100 px. Theme follows the system (light / dark); explicit light/dark override available in preferences.

### 1. Menubar
Brand label `REFERENCE CLIPPER` (Barlow Condensed 600, 15 px, letter-spacing .02 em) then menus **File · Edit · View · Help**.
File menu: `Open… (Ctrl+O)`, section `OPEN RECENT` (last 3 files, monospace 12 px), separator, `Close clip (Ctrl+W)`, `Quit (Ctrl+Q)`.
Menu popup: bg `--color-bg`, 1 px divider border, registration corner marks, shadow `--shadow-lg`; item hover `--color-accent-100`.

### 2. Toolbar
Height ~40 px, 1 px bottom divider. Left: secondary button `Open` (Lucide `folder` icon 15 px, 1.5 stroke). When a clip is loaded: file name (monospace 12 px), metadata muted 12 px `3840×2160 · 29.97 fps · 03:07.12 · 1.9 GB`, spacer, primary button `Encode AV1 / AAC` (accent fill, square corners, corner marks).

### 3. Main — empty state (no clip)
Two columns `1.2fr 1fr`, 14 px gap/padding.
- **Drop zone** (left, full height, blueprint frame): centered column, Lucide `film`-style 44 px icon in accent, h2 `Drop a video here` (30 px), body `or open one from the gallery, the file browser, or pass a path on the command line.`, primary button `Open file…`, monospace note `reference-clipper <file>` / `registered for video/mp4 · video/x-matroska · video/webm · video/quicktime`. Drag-over: border becomes dashed, fill `--color-accent-100`.
- **Gallery · ~/Videos** (right top): auto-fill grid of 150 px cards; 16:9 thumbnail with duration badge bottom-left (10 px mono, white on 55 % black), name mono 11 px, meta 10 px muted. Click opens.
- **Previous jobs** (right bottom): header shows path `~/.local/share/reference-clipper/history.json`; rows 56×32 thumbnail + name + `date · n spans · duration · size`. Click reloads the source clip of that job.

Ways to open a clip (all must work): drag & drop onto window, gallery click, File ▸ Open… / toolbar Open (native file dialog, video filter), Open Recent, history row, CLI argument / xdg file association (`.desktop` with the MIME types above).

### 4. Main — editor (clip loaded)
Panels are blueprint frames (1 px `--color-divider` border, square, four `+` corner marks). Three layouts are offered; ship **classic** by default and expose the others under View ▸ Layout.
- classic: columns `1fr 360px`, rows `1.3fr 1fr`, areas `preview output / timeline jobs`
- threeColumn: `300px 1fr 340px`, areas `jobs preview output / jobs timeline output`
- stacked: `1fr 1fr`, rows `1.4fr 230px 1fr`, areas `preview preview / timeline timeline / output jobs`

#### Preview panel
Header: kicker `PREVIEW`, hint `input · output side by side`, spacer, label `Audio` + segmented radio **Input | Output**.
Body (surface fill `--color-surface`, 12 px padding): two equal columns, each a caption row then a 16:9 frame sized `min(100 % width, height·16/9)` and centered.
- Input frame caption: `INPUT` + `filename · W×H`. Shows the source frame at the playhead (decoded via seek).
- Output frame caption: `OUTPUT` + (`not encoded yet · preview via seek · W×H` | `outname.mp4 · W×H`). Content rules:
  - playhead outside every span → solid black (label `black`, or `black · silence` when audio = Output)
  - inside a span, no finished encode → same source frame scaled to the output resolution (label `seek preview @ tc`)
  - inside a span, finished encode exists → frame decoded from the output file at the mapped output time (label `decoded @ tc`, badge `DECODED AV1` top-right, accent-700 fill, white 10 px caps)
Transport row: `⏮` (to first span), `◀` frame back, primary `Play spans` / `Pause` (110 px), `▶` frame forward, playhead timecode 14 px mono, `/ clip length` 11 px muted, spacer, hint `Space play · I/O set in/out · ←/→ frame · Del remove span`.

#### Timeline panel (source timeline, full panel width)
Header: kicker `SOURCE TIMELINE`, hint `drag edges to trim · drag body to move · click to seek`, spacer, `n spans · total output tc`, buttons `+ Span`, `I`, `O`, `✕` (remove selected; disabled when nothing selected).
Track area (14 px side margins): ruler of 8 ticks (9 px mono, mm:ss) · filmstrip strip 44 px tall (thumbnails decoded at evenly spaced times) · waveform strip 26 px (bars in `--color-accent-500`). Spans overlay both strips: fill accent at 18 %, 1 px accent border (2 px when selected), label above-left `n · duration` (Barlow Condensed 12 px, accent). Edge handles 10 px wide hit area, 4×26 px accent grip; body drag moves the span preserving length; spans clamp to [0, duration] and to ≥ 1 frame. Playhead: 2 px text-color line with a downward triangle cap; clicking/dragging on empty track seeks.

#### Output panel
Header: kicker `OUTPUT`, tag `mp4 · av1 · aac`.
Fields (14 px vertical rhythm):
- `Resolution` segmented radio: **Source | 1080p | 720p | 480p**; caption `W×H · scale=lanczos` (width = source width × preset/source height, rounded to even; presets taller than source show source size).
- `Video quality` slider 0–100 (default 70) with readout `70 · crf 25`; CRF = round(63 − q·0.55); hints below: `smaller` / contextual (`low bitrate` <35, `good for review` <60, `visually transparent` <85, `near-lossless`) / `sharper`.
- `Audio quality` slider 0–100 (default 70) readout `70 · 243 kb/s`; kb/s = round(64 + q·2.56); caption `64 kb/s · aac · 48 kHz · stereo · 320 kb/s`.
- `File name` text input (mono), default `<source-basename>_clip.mp4`.
- `Folder (remembered)` read-only path + `Browse` button; persisted in config (`~/.config/reference-clipper/config.toml`), default `~/Videos/Clips`.
- Summary plate (1 px frame, 2-col mono 11 px): `output length`, `estimated size`, `video  libsvtav1 · preset 6 · 10-bit`, `audio  aac · N kb/s`.
- Note (11 px, 65 %): "A/V sync: each span is decoded from the previous keyframe and trimmed on exact PTS, audio is resampled with aresample=async=1, timestamps are regenerated across the concatenation, and the muxed file is probed for drift before the job is marked done."

#### Jobs panel
`ENCODE QUEUE` header with `n active` / `idle`. Empty text: `No jobs queued. Encode adds the current spans and settings as a job.` Job row: 48×27 thumbnail, name (mono 12), meta `n spans · dur · preset · crf · est size`, 3 px progress bar (accent on divider), right column status (`queued` | `NN% · NN fps` pulsing | `done ✓`) and ghost button `Cancel` / `Play output`.
`HISTORY` sub-header with `~/.local/share/reference-clipper/`. Rows as in the empty state; most recent first; click reloads source.

### 5. Status bar
Mono 10.5 px, 75 %: `ffmpeg 7.1 · libsvtav1 2.3 · aac` · state (`ready | playing | editing span · n spans · W×H`, or `n jobs in history`) · spacer · `out: <folder>`.

### 6. Toast
Bottom-right blueprint card, `--shadow-md`, 12 px; e.g. `Job added to queue`, `Output folder saved to config`, `<name> written · A/V drift 0 ms`. Auto-dismiss 2–3.5 s.

## Interactions & behavior
- **Playback** plays only the spans, in source order, skipping gaps: on reaching a span end jump to the next span start; stop after the last. Pressing play outside a span jumps to the next (or first) span. Advance by wall-clock delta, not a fixed per-tick step.
- **Keys**: Space play/pause · I / O set in/out of the selected span at the playhead (clamped to ≥ 1 frame) · ← / → one frame · Delete/Backspace remove selected span · Esc close menus/dialogs. Ignore keys while a text field has focus.
- **+ Span** adds a span at the playhead, length min(10 s, 5 % of clip), selects it.
- Clicking a span selects it; selected span is highlighted.
- **Encode** adds a job (spans, resolution, qualities, output name/folder). One job encodes at a time; others wait as `queued`. On completion: history entry (with a JPEG thumbnail of the first span's first frame), audio radio switches to Output, playhead to first span, toast with measured A/V drift.
- **Audio radio**: Input plays source audio at the playhead; Output plays encoded audio when a finished output exists (silence outside spans) and source-trimmed audio before that.
- Resolution/quality changes update the estimate live.

## State
`clip {path,name,w,h,fps,duration,size}` · `spans[{id,start,end}]` (seconds) · `selectedSpanId` · `playhead` · `playing` · `audioSource ('input'|'output')` · `resPreset` · `videoQuality` · `audioQuality` · `outDir` · `outName` · `queue[{id,name,status,progress,fps,settings}]` · `history[]` (persisted) · `layout` · `theme`.

## Encoding pipeline (FFmpeg, C++)
- Decode: `avformat` + `avcodec`; seek to the keyframe ≤ span.start, decode and drop frames with pts < start, stop at pts ≥ end (frame-accurate trim, not `-ss` on the container).
- Video: `libsvtav1`, preset 6, `crf` from slider, `pix_fmt yuv420p10le`, scale with `lanczos` to the preset height (even width), concat spans in one encoder instance with monotonically regenerated PTS (offset = cumulative output duration).
- Audio: decode, trim on sample-accurate PTS, `aresample=async=1:first_pts=0`, encode AAC 48 kHz stereo at the slider bitrate, same PTS offsets as video.
- Mux: MP4 with `movflags=+faststart`; after muxing, probe first/last PTS of both streams and report drift in the toast; mark job failed if |drift| > 1 frame.
- Progress: frames encoded / total span frames; report fps.
- Preview: a dedicated decoder for the source (seek + decode one frame) and, once a job is done, a second decoder for the output file; map source time → output time via cumulative span offsets.
- Thumbnails: filmstrip = N evenly spaced frames scaled to the strip height; job thumbnail = first frame of first span as JPEG ~320 px wide stored next to `history.json`.

## Persistence
- `~/.config/reference-clipper/` : last output folder, layout, theme, recent files.
- `~/.local/share/reference-clipper/history.json` + `thumbs/<jobid>.jpg` : every job (source path, spans, settings, output path, size, duration, date, drift).

## Design tokens (Industry system — see `industry.css`)
Light: bg `#f2f2f3`, surface `#e9e9ea`, text `#1d1f20`, accent `#5980a6`, divider = text at 16 %. Accent ramp 100→900: `#eef6ff #d6ebff #b5d9fd #94bce3 #749dc4 #597ea3 #416180 #2c455d #1d2d3d`. Neutral ramp: `#f5f5f8 #e7e7ea #d4d4d7 #b7b7ba #98989b #7a7a7d #5d5d60 #424244 #2b2b2d`.
Dark override: bg `#1b1d1f`, surface `#25282b`, text `#e4e6e8`, accent `#7fa6cf`, accent-100 `#243a50`, accent-800 `#cfe5fb`, divider = text at 18 %.
Type: headings Barlow Condensed 600; body Barlow 400/500/700; code/timecodes `ui-monospace`. UI base 13 px; kickers 10 px uppercase, letter-spacing .1 em, accent; captions 11 px at 60 %.
Spacing scale: 3.4 / 6.8 / 10.2 / 13.6 / 20.4 / 27.2 px. Radius: 0 on frames, buttons, inputs (square). Shadows: sm `0 1px 2px 14%`, md `0 3px 10px 16%`, lg `0 12px 32px 22%` of `#2b2b2d`.
Buttons: primary = accent fill, bg-colored text, hover accent-600, active accent-700; secondary = divider border, hover text 7 %; ghost = accent text. Focus ring 2 px accent, offset 2 px. Disabled 45 % opacity.

## Assets
No bitmap assets. Icons: Lucide, stroke 1.5 (folder, film, skip-back, chevron-left/right, x). Thumbnails/frames in the prototype are striped placeholders; in the app they are decoded frames.

## Files
- `Reference Clipper.dc.html` — interactive prototype (all screens, states, layouts; tweaks `layout`, `theme`)
- `industry.css` — design tokens and component classes
