#pragma once

#include <QMainWindow>

#include "app/session.h"
#include "app/widgets.h"

class EmptyView;
class JobsPanel;
class OutputPanel;
class PreviewPanel;
class QActionGroup;
class QGridLayout;
class QLabel;
class QMenu;
class QStackedWidget;
class TimelineWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    Session* session() { return &session_; }
    PreviewPanel* previewPanel() const { return preview_; }
    TimelineWidget* timeline() const { return timeline_; }
    QPushButton* encodeButton() const { return encode_; }
    Toast* toast() const { return toast_; }
    void applyLayout(const QString& name);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void buildMenus();
    void rebuildFileMenu();
    QWidget* buildToolbar();
    QWidget* buildTimelinePanel();
    QWidget* buildStatusRow();
    void openDialog();
    void encode();
    void syncClip();
    void syncTimelineHeader();
    void syncEncodeButton();
    void syncStatus();
    bool handleKey(class QKeyEvent* event);

    Session session_;
    QStackedWidget* stack_;
    EmptyView* empty_;
    QGridLayout* editor_;
    PreviewPanel* preview_;
    TimelineWidget* timeline_;
    QWidget* timelinePanel_;
    OutputPanel* output_;
    JobsPanel* jobs_;
    Toast* toast_;

    QMenu* fileMenu_;
    QLabel* clipName_;
    QLabel* clipMeta_;
    QPushButton* encode_;
    QLabel* spansSummary_;
    QPushButton* removeSpan_;
    QLabel* stateLabel_;
    QLabel* folderLabel_;
    QActionGroup* layoutActions_;
};
