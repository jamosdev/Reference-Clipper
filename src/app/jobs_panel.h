// The encode queue and history, shared between the editor and the empty state.
#pragma once

#include <QHash>
#include <QSet>

#include "app/widgets.h"

class QVBoxLayout;
class Session;
class JobRow;

// A history row: thumbnail, name, meta. Clicking reloads the job's source.
class HistoryRow : public QWidget {
    Q_OBJECT
public:
    HistoryRow(const QString& jobId, const QString& name, const QString& meta, QSize thumbSize, QWidget* parent = nullptr);

signals:
    void clicked();

protected:
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QPixmap thumb_;
    QSize thumbSize_;
    bool hovered_ = false;
};

QString historyMeta(const class Session& session, int index);

class JobsPanel : public Panel {
    Q_OBJECT
public:
    explicit JobsPanel(Session* session, QWidget* parent = nullptr);

private:
    void syncQueue();
    void syncHistory();

    Session* session_;
    QLabel* summary_;
    QLabel* empty_;
    QVBoxLayout* queueRows_;
    QVBoxLayout* historyRows_;
    QHash<int, JobRow*> rows_;
};
