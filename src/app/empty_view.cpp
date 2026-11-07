#include "app/empty_view.h"

#include <QGridLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "app/jobs_panel.h"
#include "app/session.h"
#include "app/theme.h"

namespace {

constexpr int kCardMin = 150;
constexpr int kCardGap = 10;
constexpr int kCardPadding = 10;

class GalleryCard : public QWidget {
public:
    GalleryCard(const GalleryItem& item, std::function<void()> open, QWidget* parent = nullptr)
        : QWidget(parent), item_(item), open_(std::move(open))
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setToolTip(item.path);
    }

    int heightFor(int width) const { return width * 9 / 16 + 34; }

protected:
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (rect().contains(event->pos())) open_();
    }

    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::HoverEnter || event->type() == QEvent::HoverLeave) update();
        return QWidget::event(event);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        const Tokens& t = Theme::tokens();
        const QRect thumb(0, 0, width(), width() * 9 / 16);
        p.fillRect(thumb, Qt::black);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        if (!item_.thumb.isNull()) {
            QRect target(QPoint(0, 0), item_.thumb.size().scaled(thumb.size(), Qt::KeepAspectRatio));
            target.moveCenter(thumb.center());
            p.drawImage(target, item_.thumb);
        }
        p.setPen(t.divider);
        p.drawRect(thumb.adjusted(0, 0, -1, -1));

        p.setFont(ui::monoFont(10));
        const QRect chip(4, thumb.bottom() - 19, p.fontMetrics().horizontalAdvance(item_.duration) + 10, 15);
        p.fillRect(chip, QColor(0, 0, 0, 140));
        p.setPen(Qt::white);
        p.drawText(chip, Qt::AlignCenter, item_.duration);

        p.setFont(ui::monoFont(11));
        p.setPen(underMouse() ? t.accent : t.text);
        p.drawText(QRect(0, thumb.bottom() + 5, width(), 14), Qt::AlignLeft | Qt::AlignVCenter,
                   p.fontMetrics().elidedText(item_.name, Qt::ElideRight, width()));
        p.setFont(ui::bodyFont(10));
        p.setPen(Theme::faded(t.text, 0.55));
        p.drawText(QRect(0, thumb.bottom() + 19, width(), 13), Qt::AlignLeft | Qt::AlignVCenter,
                   p.fontMetrics().elidedText(item_.meta, Qt::ElideRight, width()));
    }

private:
    GalleryItem item_;
    std::function<void()> open_;
};

struct GridShape {
    int columns;
    int cardWidth;
};

GridShape shapeFor(int width)
{
    const int columns = std::max(1, (width + kCardGap) / (kCardMin + kCardGap));
    return {columns, (width - kCardGap * (columns - 1)) / columns};
}

}  // namespace

DropZone::DropZone(QWidget* parent) : Panel(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setAlignment(Qt::AlignCenter);
    outer->setSpacing(14);

    icon_ = new QLabel(this);
    icon_->setAlignment(Qt::AlignCenter);
    auto* title = new QLabel("Drop a video here", this);
    title->setFont(ui::headingFont(30));
    title->setAlignment(Qt::AlignCenter);
    auto* body = ui::label("or open one from the gallery, the file browser,\nor pass a path on the command line.",
                           "caption", ui::bodyFont(14), this);
    body->setAlignment(Qt::AlignCenter);
    open_ = new MarkedButton("Open file…", this);
    open_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto* note = ui::label("reference-clipper &lt;file&gt;<br>registered for video/mp4 · video/x-matroska · video/webm · video/quicktime",
                           "faint", ui::monoFont(11), this);
    note->setTextFormat(Qt::RichText);
    note->setAlignment(Qt::AlignCenter);

    outer->addWidget(icon_, 0, Qt::AlignHCenter);
    outer->addWidget(title, 0, Qt::AlignHCenter);
    outer->addWidget(body, 0, Qt::AlignHCenter);
    outer->addWidget(open_, 0, Qt::AlignHCenter);
    outer->addWidget(note, 0, Qt::AlignHCenter);

    connect(&Theme::instance(), &Theme::changed, this, &DropZone::refreshIcon);
    refreshIcon();
}

void DropZone::refreshIcon()
{
    icon_->setPixmap(ui::iconPixmap("film", Theme::tokens().accent, 44, devicePixelRatioF()));
}

void DropZone::setDragActive(bool active)
{
    active_ = active;
    update();
}

void DropZone::paintEvent(QPaintEvent* event)
{
    if (!active_) {
        Panel::paintEvent(event);
        return;
    }
    QPainter p(this);
    const QRect box = rect().adjusted(kOverhang, kOverhang, -kOverhang - 1, -kOverhang - 1);
    p.fillRect(box, Theme::tokens().accent100);
    p.setPen(QPen(Theme::tokens().accent, 1, Qt::DashLine));
    p.drawRect(box);
    paintCornerMarks(p, box);
}

GalleryGrid::GalleryGrid(QWidget* parent) : QWidget(parent)
{
    QSizePolicy policy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

void GalleryGrid::add(QWidget* card)
{
    card->setParent(this);
    card->show();
    cards_.push_back(card);
    updateGeometry();
    resizeEvent(nullptr);
}

int GalleryGrid::heightForWidth(int width) const
{
    const GridShape shape = shapeFor(width);
    const int rows = (int(cards_.size()) + shape.columns - 1) / shape.columns;
    const int cardHeight = shape.cardWidth * 9 / 16 + 34;
    return rows ? rows * cardHeight + (rows - 1) * kCardGap : 0;
}

void GalleryGrid::resizeEvent(QResizeEvent*)
{
    const GridShape shape = shapeFor(width());
    const int cardHeight = shape.cardWidth * 9 / 16 + 34;
    for (int i = 0; i < cards_.size(); ++i) {
        const int column = i % shape.columns;
        const int row = i / shape.columns;
        cards_[i]->setGeometry(column * (shape.cardWidth + kCardGap), row * (cardHeight + kCardGap), shape.cardWidth, cardHeight);
    }
}

EmptyView::EmptyView(Session* session, QWidget* parent) : QWidget(parent), session_(session)
{
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(8, 8, 8, 8);
    grid->setSpacing(2);
    grid->setColumnStretch(0, 12);
    grid->setColumnStretch(1, 10);
    grid->setRowStretch(0, 1);
    grid->setRowStretch(1, 1);

    dropZone_ = new DropZone(this);
    grid->addWidget(dropZone_, 0, 0, 2, 1);
    connect(dropZone_->openButton(), &QPushButton::clicked, this, &EmptyView::openRequested);

    const QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    auto* galleryPanel = new Panel(this);
    auto* galleryColumn = new QVBoxLayout(galleryPanel);
    galleryColumn->setContentsMargins(0, 0, 0, 0);
    galleryColumn->setSpacing(0);
    galleryColumn->addWidget(ui::headerRow({ui::kicker("Gallery · " + ui::tildePath(videos)), ui::spacer()}));
    auto* galleryScroll = new QScrollArea(galleryPanel);
    galleryScroll->setWidgetResizable(true);
    galleryScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    galleryScroll->setFrameShape(QFrame::NoFrame);
    auto* galleryContent = new QWidget;
    auto* galleryLayout = new QVBoxLayout(galleryContent);
    galleryLayout->setContentsMargins(kCardPadding, kCardPadding, kCardPadding, kCardPadding);
    gallery_ = new GalleryGrid;
    galleryLayout->addWidget(gallery_);
    galleryLayout->addStretch(1);
    galleryScroll->setWidget(galleryContent);
    galleryColumn->addWidget(galleryScroll, 1);
    grid->addWidget(galleryPanel, 0, 1);

    auto* historyPanel = new Panel(this);
    auto* historyColumn = new QVBoxLayout(historyPanel);
    historyColumn->setContentsMargins(0, 0, 0, 0);
    historyColumn->setSpacing(0);
    historyColumn->addWidget(ui::headerRow({ui::kicker("Previous jobs"),
                                            ui::label(ui::tildePath(paths::historyFile()), "faint", ui::monoFont(10)), ui::spacer()}));
    auto* historyScroll = new QScrollArea(historyPanel);
    historyScroll->setWidgetResizable(true);
    historyScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    historyScroll->setFrameShape(QFrame::NoFrame);
    auto* historyContent = new QWidget;
    auto* historyLayout = new QVBoxLayout(historyContent);
    historyLayout->setContentsMargins(0, 0, 0, 0);
    historyLayout->setSpacing(0);
    history_ = new QVBoxLayout;
    history_->setSpacing(0);
    historyLayout->addLayout(history_);
    historyLayout->addStretch(1);
    historyScroll->setWidget(historyContent);
    historyColumn->addWidget(historyScroll, 1);
    grid->addWidget(historyPanel, 1, 1);

    connect(&scanner_, &GalleryScanner::found, this, [this](const GalleryItem& item) {
        const QString path = item.path;
        gallery_->add(new GalleryCard(item, [this, path] { session_->openFile(path); }));
    });
    connect(session_, &Session::historyChanged, this, &EmptyView::syncHistory);
    syncHistory();
    scanner_.scan(videos);
}

void EmptyView::syncHistory()
{
    while (QLayoutItem* item = history_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const QVector<HistoryEntry>& entries = session_->history();
    for (int i = 0; i < entries.size(); ++i) {
        auto* row = new HistoryRow(entries[i].id, entries[i].name, historyMeta(*session_, i), {56, 32});
        connect(row, &HistoryRow::clicked, this, [this, i] { session_->openHistoryEntry(i); });
        history_->addWidget(row);
    }
}
