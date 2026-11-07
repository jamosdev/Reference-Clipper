#include "app/main_window.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "app/empty_view.h"
#include "app/jobs_panel.h"
#include "app/output_panel.h"
#include "app/preview_panel.h"
#include "app/theme.h"
#include "app/timeline_widget.h"
#include "core/quality.h"

namespace {

constexpr int kPanelGap = 14 - 2 * Panel::kOverhang;
constexpr int kPanelPadding = 14 - Panel::kOverhang;
constexpr int kPanelFrame = 2 * Panel::kOverhang + 2;

QString firstLocalFile(const QMimeData* mime)
{
    if (!mime->hasUrls()) return {};
    for (const QUrl& url : mime->urls())
        if (url.isLocalFile()) return url.toLocalFile();
    return {};
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle("Reference Clipper");
    setMinimumWidth(1100);
    resize(1440, 900);
    setAcceptDrops(true);

    auto* central = new QWidget(this);
    auto* column = new QVBoxLayout(central);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    setCentralWidget(central);

    buildMenus();
    column->addWidget(buildToolbar());

    stack_ = new QStackedWidget(central);
    empty_ = new EmptyView(&session_, stack_);
    stack_->addWidget(empty_);
    auto* editorPage = new QWidget(stack_);
    editor_ = new QGridLayout(editorPage);
    editor_->setContentsMargins(kPanelPadding, kPanelPadding, kPanelPadding, kPanelPadding);
    editor_->setSpacing(kPanelGap);
    preview_ = new PreviewPanel(&session_, editorPage);
    timelinePanel_ = buildTimelinePanel();
    output_ = new OutputPanel(&session_, editorPage);
    jobs_ = new JobsPanel(&session_, editorPage);
    stack_->addWidget(editorPage);
    column->addWidget(stack_, 1);
    column->addWidget(buildStatusRow());

    toast_ = new Toast(this);
    connect(&session_, &Session::toast, toast_, &Toast::show);

    connect(empty_, &EmptyView::openRequested, this, &MainWindow::openDialog);
    connect(&session_, &Session::clipChanged, this, &MainWindow::syncClip);
    connect(&session_, &Session::recentChanged, this, &MainWindow::rebuildFileMenu);
    for (auto signal : {&Session::spansChanged, &Session::outputSettingsChanged}) {
        connect(&session_, signal, this, &MainWindow::syncTimelineHeader);
        connect(&session_, signal, this, &MainWindow::syncEncodeButton);
        connect(&session_, signal, this, &MainWindow::syncStatus);
    }
    connect(&session_, &Session::selectionChanged, this, &MainWindow::syncTimelineHeader);
    connect(&session_, &Session::playingChanged, this, &MainWindow::syncStatus);
    connect(&session_, &Session::historyChanged, this, &MainWindow::syncStatus);
    connect(timeline_, &TimelineWidget::editingChanged, this, &MainWindow::syncStatus);

    qApp->installEventFilter(this);
    applyLayout(session_.layoutName());
    rebuildFileMenu();
    syncClip();
}

void MainWindow::buildMenus()
{
    auto* bar = menuBar();
    auto* brand = new QLabel("REFERENCE CLIPPER", bar);
    QFont brandFont = ui::headingFont(15);
    brandFont.setLetterSpacing(QFont::PercentageSpacing, 102);
    brand->setFont(brandFont);
    brand->setContentsMargins(6, 0, 12, 0);
    bar->setCornerWidget(brand, Qt::TopLeftCorner);

    fileMenu_ = bar->addMenu("&File");

    auto* edit = bar->addMenu("&Edit");
    edit->addAction("&Add span", &session_, &Session::addSpan);
    edit->addAction("Set in point", &session_, &Session::setInPoint);
    edit->addAction("Set out point", &session_, &Session::setOutPoint);
    edit->addAction("Remove span", &session_, &Session::removeSelectedSpan);

    auto* view = bar->addMenu("&View");
    auto* layouts = view->addMenu("Layout");
    layoutActions_ = new QActionGroup(this);
    const std::pair<const char*, const char*> layoutNames[] = {
        {"Classic", "classic"}, {"Three columns", "threeColumn"}, {"Stacked", "stacked"}};
    for (const auto& [label, name] : layoutNames) {
        QAction* action = layouts->addAction(label);
        action->setCheckable(true);
        action->setData(QString(name));
        layoutActions_->addAction(action);
        connect(action, &QAction::triggered, this, [this, name = QString(name)] {
            applyLayout(name);
            session_.setLayoutName(name);
        });
    }

    auto* themes = view->addMenu("Theme");
    auto* themeGroup = new QActionGroup(this);
    const QString currentTheme = session_.themeName();
    for (const char* name : {"system", "light", "dark"}) {
        QAction* action = themes->addAction(QString(name).replace(0, 1, QChar(name[0]).toUpper()));
        action->setCheckable(true);
        action->setChecked(currentTheme == name);
        themeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, name = QString(name)] {
            Theme::instance().setMode(Theme::modeFromName(name));
            session_.setThemeName(name);
        });
    }

    auto* help = bar->addMenu("&Help");
    help->addAction("About Reference Clipper", this, [this] {
        QMessageBox::about(this, "About Reference Clipper",
                           QString("<b>Reference Clipper %1</b><p>Mark spans in a video and encode them to one "
                                   "AV1/AAC MP4.</p><p>%2</p>")
                               .arg(QCoreApplication::applicationVersion(), session_.libraryStatus()));
    });
}

void MainWindow::rebuildFileMenu()
{
    fileMenu_->clear();
    QAction* open = fileMenu_->addAction("Open…", this, &MainWindow::openDialog);
    open->setShortcut(QKeySequence::Open);
    const QStringList recent = session_.recentFiles();
    if (!recent.isEmpty()) {
        fileMenu_->addSection("OPEN RECENT");
        for (const QString& path : recent) {
            QAction* action = fileMenu_->addAction(QFileInfo(path).fileName(), this, [this, path] { session_.openFile(path); });
            action->setFont(ui::monoFont(12));
            action->setToolTip(path);
        }
    }
    fileMenu_->addSeparator();
    QAction* close = fileMenu_->addAction("Close clip", &session_, &Session::closeClip);
    close->setShortcut(QKeySequence("Ctrl+W"));
    QAction* quit = fileMenu_->addAction("Quit", this, &QWidget::close);
    quit->setShortcut(QKeySequence("Ctrl+Q"));
}

QWidget* MainWindow::buildToolbar()
{
    auto* bar = new QWidget(this);
    bar->setAttribute(Qt::WA_StyledBackground);
    bar->setProperty("divider", "bottom");
    bar->setFixedHeight(44);
    auto* row = new QHBoxLayout(bar);
    row->setContentsMargins(10, 0, 4, 0);
    row->setSpacing(8);

    auto* open = ui::button("Open");
    open->setIconSize({15, 15});
    const auto paintOpenIcon = [open] { open->setIcon(ui::iconPixmap("folder", Theme::tokens().text, 15, open->devicePixelRatioF())); };
    paintOpenIcon();
    connect(&Theme::instance(), &Theme::changed, open, paintOpenIcon);
    connect(open, &QPushButton::clicked, this, &MainWindow::openDialog);

    clipName_ = ui::label("", nullptr, ui::monoFont(12));
    clipName_->setContentsMargins(6, 0, 0, 0);
    clipMeta_ = ui::label("", "caption", ui::bodyFont(12));
    encode_ = new MarkedButton("Encode AV1 / AAC");
    connect(encode_, &QPushButton::clicked, this, &MainWindow::encode);

    row->addWidget(open);
    row->addWidget(clipName_);
    row->addWidget(clipMeta_);
    row->addStretch(1);
    row->addWidget(encode_);
    return bar;
}

QWidget* MainWindow::buildTimelinePanel()
{
    auto* panel = new Panel;
    auto* column = new QVBoxLayout(panel);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    spansSummary_ = ui::label("", "caption", ui::monoFont(11));
    const auto small = [](const QString& text, const QString& tip) {
        auto* b = ui::button(text);
        b->setFont(ui::bodyFont(12));
        b->setToolTip(tip);
        return b;
    };
    auto* add = small("+ Span", "Add a span at the playhead");
    auto* in = small("I", "Set in point at playhead (I)");
    auto* out = small("O", "Set out point at playhead (O)");
    removeSpan_ = small("✕", "Remove selected span (Del)");
    connect(add, &QPushButton::clicked, &session_, &Session::addSpan);
    connect(in, &QPushButton::clicked, &session_, &Session::setInPoint);
    connect(out, &QPushButton::clicked, &session_, &Session::setOutPoint);
    connect(removeSpan_, &QPushButton::clicked, &session_, &Session::removeSelectedSpan);

    column->addWidget(ui::headerRow({ui::kicker("Source timeline"),
                                     ui::label("drag edges to trim · drag body to move · click to seek", "caption", ui::bodyFont(11)),
                                     ui::spacer(), spansSummary_, add, in, out, removeSpan_}));
    timeline_ = new TimelineWidget(&session_, panel);
    timeline_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    column->addWidget(timeline_, 1);
    return panel;
}

QWidget* MainWindow::buildStatusRow()
{
    auto* bar = new QWidget(this);
    bar->setAttribute(Qt::WA_StyledBackground);
    bar->setProperty("divider", "top");
    auto* row = new QHBoxLayout(bar);
    row->setContentsMargins(10, 3, 10, 3);
    row->setSpacing(16);
    QFont font = ui::monoFont(10);
    font.setPixelSize(11);
    row->addWidget(ui::label(session_.libraryStatus(), "caption", font));
    stateLabel_ = ui::label("", "caption", font);
    row->addWidget(stateLabel_);
    row->addStretch(1);
    folderLabel_ = ui::label("", "caption", font);
    row->addWidget(folderLabel_);
    return bar;
}

void MainWindow::applyLayout(const QString& name)
{
    while (editor_->takeAt(0)) {
    }
    for (int i = 0; i < 3; ++i) {
        editor_->setColumnStretch(i, 0);
        editor_->setColumnMinimumWidth(i, 0);
        editor_->setRowStretch(i, 0);
        editor_->setRowMinimumHeight(i, 0);
    }
    const auto fixedColumn = [&](int column, int width) { editor_->setColumnMinimumWidth(column, width + kPanelFrame); };

    if (name == "threeColumn") {
        fixedColumn(0, 300);
        editor_->setColumnStretch(1, 1);
        fixedColumn(2, 340);
        editor_->setRowStretch(0, 13);
        editor_->setRowStretch(1, 10);
        editor_->addWidget(jobs_, 0, 0, 2, 1);
        editor_->addWidget(preview_, 0, 1);
        editor_->addWidget(timelinePanel_, 1, 1);
        editor_->addWidget(output_, 0, 2, 2, 1);
    } else if (name == "stacked") {
        editor_->setColumnStretch(0, 1);
        editor_->setColumnStretch(1, 1);
        editor_->setRowStretch(0, 14);
        editor_->setRowMinimumHeight(1, 230 + kPanelFrame);
        editor_->setRowStretch(2, 10);
        editor_->addWidget(preview_, 0, 0, 1, 2);
        editor_->addWidget(timelinePanel_, 1, 0, 1, 2);
        editor_->addWidget(output_, 2, 0);
        editor_->addWidget(jobs_, 2, 1);
    } else {
        editor_->setColumnStretch(0, 1);
        fixedColumn(1, 360);
        editor_->setRowStretch(0, 13);
        editor_->setRowStretch(1, 10);
        editor_->addWidget(preview_, 0, 0);
        editor_->addWidget(output_, 0, 1);
        editor_->addWidget(timelinePanel_, 1, 0);
        editor_->addWidget(jobs_, 1, 1);
    }
    for (QAction* action : layoutActions_->actions()) {
        const bool current = action->data().toString() == name
                             || (name != "threeColumn" && name != "stacked" && action->data().toString() == "classic");
        action->setChecked(current);
    }
}

void MainWindow::openDialog()
{
    const QString start = session_.hasClip() ? QFileInfo(QString::fromStdString(session_.clip().path)).path()
                                             : QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    const QString path = QFileDialog::getOpenFileName(this, "Open video", start,
                                                      "Video files (*.mp4 *.mkv *.webm *.mov *.m4v *.avi);;All files (*)");
    if (!path.isEmpty()) session_.openFile(path);
}

void MainWindow::encode()
{
    if (session_.outputConflicts()) {
        const auto answer = QMessageBox::question(
            this, "Replace file?",
            QString("%1 already exists or is queued. Replace it?").arg(ui::tildePath(session_.outputPath())));
        if (answer != QMessageBox::Yes) return;
    }
    session_.encode();
}

void MainWindow::syncClip()
{
    const bool hasClip = session_.hasClip();
    stack_->setCurrentIndex(hasClip ? 1 : 0);
    if (hasClip) {
        const rc::ClipInfo& clip = session_.clip();
        clipName_->setText(QString::fromStdString(clip.name));
        clipMeta_->setText(QString("%1×%2 · %3 fps · %4 · %5")
                               .arg(clip.display.w)
                               .arg(clip.display.h)
                               .arg(QString::number(clip.fps, 'g', 4), session_.timecode(clip.duration),
                                    QString::fromStdString(rc::formatFileSize(clip.sizeBytes))));
        setWindowTitle(clipName_->text() + " — Reference Clipper");
    } else {
        setWindowTitle("Reference Clipper");
    }
    clipName_->setVisible(hasClip);
    clipMeta_->setVisible(hasClip);
    encode_->setVisible(hasClip);
    syncTimelineHeader();
    syncEncodeButton();
    syncStatus();
}

void MainWindow::syncTimelineHeader()
{
    const size_t count = session_.spans().size();
    spansSummary_->setText(QString("%1 span%2 · %3")
                               .arg(count)
                               .arg(count == 1 ? "" : "s")
                               .arg(session_.timecode(session_.outputLength())));
    removeSpan_->setEnabled(session_.selectedSpan() != 0);
}

void MainWindow::syncEncodeButton()
{
    encode_->setEnabled(session_.canEncode());
}

void MainWindow::syncStatus()
{
    stateLabel_->setText(session_.stateText(timeline_->editingSpan()));
    folderLabel_->setText("out: " + ui::tildePath(session_.outputFolder()));
}

bool MainWindow::handleKey(QKeyEvent* event)
{
    if (!session_.hasClip()) return false;
    switch (event->key()) {
    case Qt::Key_Space:
        if (!event->isAutoRepeat()) session_.togglePlay();
        return true;
    case Qt::Key_I: session_.setInPoint(); return true;
    case Qt::Key_O: session_.setOutPoint(); return true;
    case Qt::Key_Left: session_.stepFrames(-1); return true;
    case Qt::Key_Right: session_.stepFrames(1); return true;
    case Qt::Key_Delete:
    case Qt::Key_Backspace: session_.removeSelectedSpan(); return true;
    default: return false;
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    // At the application level, or a focused button would take Space and a
    // focused slider the arrows.
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        const bool plain = !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
        const bool textField = qobject_cast<QLineEdit*>(QApplication::focusWidget());
        const bool ours = isActiveWindow() || watched == this;
        if (plain && ours && !textField && !QApplication::activeModalWidget() && !QApplication::activePopupWidget()
            && handleKey(key))
            return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (firstLocalFile(event->mimeData()).isEmpty()) return;
    event->acceptProposedAction();
    empty_->setDragActive(true);
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent*)
{
    empty_->setDragActive(false);
}

void MainWindow::dropEvent(QDropEvent* event)
{
    empty_->setDragActive(false);
    const QString path = firstLocalFile(event->mimeData());
    if (path.isEmpty()) return;
    event->acceptProposedAction();
    session_.openFile(path);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    if (toast_->isVisible()) toast_->reposition();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (session_.encoding()) {
        const auto answer = QMessageBox::question(this, "Quit Reference Clipper?",
                                                  "Encoding is still running. Quit and discard it?");
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
    }
    event->accept();
}
