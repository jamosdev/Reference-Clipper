// The no-clip screen: drop zone, ~/Videos gallery, previous jobs.
#pragma once

#include <QVector>

#include "app/assets.h"
#include "app/widgets.h"

class QVBoxLayout;
class Session;

class DropZone : public Panel {
    Q_OBJECT
public:
    explicit DropZone(QWidget* parent = nullptr);
    void setDragActive(bool active);
    QPushButton* openButton() const { return open_; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void refreshIcon();

    bool active_ = false;
    QPushButton* open_;
    QLabel* icon_;
};

class GalleryGrid : public QWidget {
    Q_OBJECT
public:
    explicit GalleryGrid(QWidget* parent = nullptr);
    void add(QWidget* card);
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    QSize sizeHint() const override { return {320, heightForWidth(320)}; }

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    QVector<QWidget*> cards_;
};

class EmptyView : public QWidget {
    Q_OBJECT
public:
    explicit EmptyView(Session* session, QWidget* parent = nullptr);
    void setDragActive(bool active) { dropZone_->setDragActive(active); }

signals:
    void openRequested();

private:
    void syncHistory();

    Session* session_;
    DropZone* dropZone_;
    GalleryGrid* gallery_;
    GalleryScanner scanner_;
    QVBoxLayout* history_;
};
