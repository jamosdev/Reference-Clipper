// Synthetic media generated with the ffmpeg CLI into a per-run temp directory.
#pragma once

#include <QString>
#include <QStringList>

namespace fixtures {

QString dir();
// Runs ffmpeg with `args`, writing dir()/name unless it already exists.
QString make(const QString& name, const QStringList& args);

// 12 s, 640x360, 30 fps H.264 with B-frames and 44.1 kHz stereo AAC. A white
// frame flashes and a 1 kHz beep starts on every whole second.
QString flashBeep(const QString& name = "flash_beep.mp4", const QStringList& extra = {});
QStringList flashBeepInputs();

}  // namespace fixtures
