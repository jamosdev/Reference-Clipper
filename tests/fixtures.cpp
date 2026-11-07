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
    // $FFMPEG lets CI use a full build for fixtures while linking a smaller one.
    ffmpeg.start(qEnvironmentVariable("FFMPEG", "ffmpeg"), QStringList{"-hide_banner", "-loglevel", "error", "-y"} + args + QStringList{path});
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

QString threeScenes()
{
    return make("three_scenes.mp4",
                {"-f", "lavfi", "-i", "testsrc2=s=320x180:r=30:d=3", "-f", "lavfi", "-i",
                 "mandelbrot=s=320x180:r=30,trim=duration=3,format=yuv420p,"
                 "drawbox=c=white:t=fill:enable='eq(n\\,30)'",
                 "-f", "lavfi", "-i", "life=s=320x180:r=30:mold=10:ratio=.2:death_color=blue,trim=duration=3,format=yuv420p",
                 "-filter_complex", "[0:v]format=yuv420p,setsar=1[a];[1:v]setsar=1[b];[2:v]setsar=1[c];[a][b][c]concat=n=3:v=1:a=0",
                 "-c:v", "libx264", "-preset", "veryfast", "-g", "30"});
}

QString shortWithInset()
{
    return make("short_inset.mp4",
                {"-f", "lavfi", "-i", "color=c=0x202830:s=360x640:r=30:d=9", "-f", "lavfi", "-i",
                 "testsrc2=s=360x270:r=30:d=3", "-f", "lavfi", "-i", "testsrc=s=360x270:r=30:d=3", "-f", "lavfi", "-i",
                 "smptebars=s=360x270:r=30:d=3",
                 "-filter_complex",
                 "[1:v]format=yuv420p[a];[2:v]format=yuv420p[b];[3:v]format=yuv420p[c];"
                 "[a][b][c]concat=n=3:v=1:a=0[p];[0:v][p]overlay=0:185,format=yuv420p",
                 "-c:v", "libx264", "-preset", "veryfast", "-g", "30"});
}

QString quadrants()
{
    return make("quadrants.mp4",
                {"-f", "lavfi", "-i",
                 "color=c=0x282828:s=640x360:r=30:d=2,format=yuv420p,"
                 "drawbox=x=320:y=0:w=320:h=180:c=0x646464:t=fill,"
                 "drawbox=x=0:y=180:w=320:h=180:c=0xa0a0a0:t=fill,"
                 "drawbox=x=320:y=180:w=320:h=180:c=0xdcdcdc:t=fill",
                 "-c:v", "libx264", "-preset", "veryfast", "-qp", "4"});
}

QString flashBeep(const QString& name, const QStringList& extra)
{
    return make(name, flashBeepInputs()
                          + QStringList{"-c:v", "libx264", "-bf", "2", "-g", "48", "-preset", "veryfast",
                                        "-c:a", "aac", "-b:a", "192k"}
                          + extra);
}

}  // namespace fixtures
