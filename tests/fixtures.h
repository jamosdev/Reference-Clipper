// Synthetic media generated with the ffmpeg CLI into a per-run temp directory.
#pragma once

#include <QString>
#include <QStringList>

namespace fixtures {

QString dir();
// Runs ffmpeg ($FFMPEG if set; it needs libx264) with `args`, writing dir()/name unless it already exists.
QString make(const QString& name, const QStringList& args);

// 12 s, 640x360, 30 fps H.264 with B-frames and 44.1 kHz stereo AAC. A white
// frame flashes and a 1 kHz beep starts on every whole second.
QString flashBeep(const QString& name = "flash_beep.mp4", const QStringList& extra = {});
QStringList flashBeepInputs();

// 9 s, 30 fps, three scenes with cuts at 3 s and 6 s. Scene 2 has a one-frame
// white flash at 4 s, and all of them keep moving, so neither is a cut.
QString threeScenes();

// 360x640 portrait: a 360x270 picture at y=185 with hard cuts at 3 s and 6 s,
// inside a static frame. The picture's corners are brightness-coded for crop tests.
QString shortWithInset();

// 640x360 with four flat quadrants of luma 40 (TL), 100 (TR), 160 (BL), 220 (BR).
QString quadrants();

}  // namespace fixtures
