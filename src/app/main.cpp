#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>

#include "app/main_window.h"
#include "app/theme.h"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("reference-clipper");
    QApplication::setApplicationDisplayName("Reference Clipper");
    QApplication::setApplicationVersion("0.1.0");
    QApplication::setDesktopFileName("reference-clipper");
    QApplication::setWindowIcon(QIcon::fromTheme("reference-clipper", QIcon(":/icons/reference-clipper.svg")));

    QCommandLineParser parser;
    parser.setApplicationDescription("Mark spans in a video and encode them to one AV1/AAC MP4.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("file", "Video file to open.", "[file]");
    parser.process(app);

    Theme::instance().install();
    MainWindow window;
    Theme::instance().setMode(Theme::modeFromName(window.session()->themeName()));
    window.show();

    const QStringList files = parser.positionalArguments();
    if (!files.isEmpty()) window.session()->openFile(QFileInfo(files.first()).absoluteFilePath());
    return QApplication::exec();
}
