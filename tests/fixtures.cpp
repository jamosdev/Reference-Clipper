#include "fixtures.h"

#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

namespace fixtures {

QString dir()
{
    static QTemporaryDir temp;
    return temp.path();
}

QString make(const QString& name, const QStringList& args)
{
    const QString path = dir() + "/" + name;
    if (QFile::exists(path)) return path;
    QProcess ffmpeg;
    ffmpeg.setProcessChannelMode(QProcess::MergedChannels);
    ffmpeg.start("ffmpeg", QStringList{"-hide_banner", "-loglevel", "error", "-y"} + args + QStringList{path});
    if (!ffmpeg.waitForFinished(120000) || ffmpeg.exitCode() != 0)
        qFatal("ffmpeg failed for %s: %s", qPrintable(name), ffmpeg.readAll().constData());
    return path;
}

QStringList flashBeepInputs()
{
    return {"-f", "lavfi", "-i",
            "color=black:s=640x360:r=30:d=12,format=yuv420p,"
            "drawbox=c=white:t=fill:enable='lt(mod(n\\,30)\\,1)'",
            "-f", "lavfi", "-i",
            "aevalsrc='if(lt(mod(t\\,1)\\,0.03)\\,0.8*sin(2*PI*1000*t)\\,0)':s=44100:c=stereo:d=12"};
}

QString flashBeep(const QString& name, const QStringList& extra)
{
    return make(name, flashBeepInputs()
                          + QStringList{"-c:v", "libx264", "-bf", "2", "-g", "48", "-preset", "veryfast",
                                        "-c:a", "aac", "-b:a", "192k"}
                          + extra);
}

}  // namespace fixtures
