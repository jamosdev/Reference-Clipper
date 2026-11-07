#include "app/output_panel.h"

#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>

#include "app/session.h"
#include "core/quality.h"

namespace {

QWidget* field(const QString& title, QWidget* content, QWidget* below = nullptr)
{
    auto* box = new QWidget;
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(ui::label(title, "caption", ui::bodyFont(12)));
    layout->addWidget(content);
    if (below) layout->addWidget(below);
    return box;
}

struct SliderField {
    QWidget* box;
    QSlider* slider;
    QLabel* readout;
};

SliderField sliderField(const QString& title, QWidget* captions)
{
    auto* box = new QWidget;
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    auto* head = new QHBoxLayout;
    head->addWidget(ui::label(title, "caption", ui::bodyFont(12)));
    head->addStretch(1);
    auto* readout = ui::label("", nullptr, ui::monoFont(11));
    head->addWidget(readout);
    layout->addLayout(head);
    auto* slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, 100);
    slider->setFixedHeight(20);
    layout->addWidget(slider);
    layout->addWidget(captions);
    return {box, slider, readout};
}

QWidget* threeCaptions(QLabel* left, QLabel* middle, QLabel* right)
{
    auto* row = new QWidget;
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(left);
    layout->addStretch(1);
    layout->addWidget(middle);
    layout->addStretch(1);
    layout->addWidget(right);
    return row;
}

}  // namespace

OutputPanel::OutputPanel(Session* session, QWidget* parent) : Panel(parent), session_(session)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(ui::headerRow({ui::kicker("Output"), ui::spacer(), ui::label("mp4 · av1 · aac", "kicker", ui::monoFont(10))}));

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget;
    auto* form = new QVBoxLayout(content);
    form->setContentsMargins(12, 12, 12, 8);
    form->setSpacing(14);

    QStringList labels;
    for (const ResolutionPreset& preset : kResolutions) labels << preset.label;
    resolution_ = new Segmented(labels, 12, true);
    dims_ = ui::label("", "caption", ui::monoFont(11));
    form->addWidget(field("Resolution", resolution_, dims_));

    videoHint_ = ui::label("", "faint", ui::bodyFont(10));
    const SliderField video = sliderField("Video quality",
        threeCaptions(ui::label("smaller", "faint", ui::bodyFont(10)), videoHint_, ui::label("sharper", "faint", ui::bodyFont(10))));
    videoQuality_ = video.slider;
    videoReadout_ = video.readout;
    form->addWidget(video.box);

    const SliderField audio = sliderField("Audio quality",
        threeCaptions(ui::label("64 kb/s", "faint", ui::bodyFont(10)), ui::label("aac · 48 kHz · stereo", "faint", ui::bodyFont(10)),
                      ui::label("320 kb/s", "faint", ui::bodyFont(10))));
    audioQuality_ = audio.slider;
    audioReadout_ = audio.readout;
    form->addWidget(audio.box);

    fileName_ = new QLineEdit;
    fileName_->setFont(ui::monoFont(12));
    form->addWidget(field("File name", fileName_));

    auto* folderRow = new QWidget;
    auto* folderLayout = new QHBoxLayout(folderRow);
    folderLayout->setContentsMargins(0, 0, 0, 0);
    folderLayout->setSpacing(6);
    folder_ = new QLineEdit;
    folder_->setFont(ui::monoFont(12));
    folder_->setReadOnly(true);
    auto* browse = ui::button("Browse");
    browse->setFont(ui::bodyFont(12));
    folderLayout->addWidget(folder_, 1);
    folderLayout->addWidget(browse);
    form->addWidget(field("Folder (remembered)", folderRow));

    auto* plate = new QWidget;
    plate->setAttribute(Qt::WA_StyledBackground);
    plate->setProperty("plate", true);
    auto* grid = new QGridLayout(plate);
    grid->setContentsMargins(10, 10, 10, 10);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(2);
    const auto row = [&](int index, const QString& name, QLabel* value) {
        grid->addWidget(ui::label(name, "muted", ui::monoFont(11)), index, 0);
        value->setFont(ui::monoFont(11));
        grid->addWidget(value, index, 1);
    };
    length_ = new QLabel;
    size_ = new QLabel;
    audioSummary_ = new QLabel;
    row(0, "output length", length_);
    row(1, "estimated size", size_);
    row(2, "video", new QLabel(QString("libsvtav1 · preset %1 · 10-bit").arg(rc::kEncoderPreset)));
    row(3, "audio", audioSummary_);
    grid->setColumnStretch(1, 1);
    form->addWidget(plate);

    auto* note = ui::label("A/V sync: each span is decoded from the previous keyframe and trimmed on exact PTS, audio is "
                           "resampled with aresample=async=1, timestamps are regenerated across the concatenation, and the "
                           "muxed file is probed for drift before the job is marked done.", "caption", ui::bodyFont(11));
    note->setWordWrap(true);
    form->addWidget(note);
    form->addStretch(1);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    connect(resolution_, &Segmented::picked, session_, &Session::setResolutionIndex);
    connect(videoQuality_, &QSlider::valueChanged, session_, &Session::setVideoQuality);
    connect(audioQuality_, &QSlider::valueChanged, session_, &Session::setAudioQuality);
    connect(fileName_, &QLineEdit::textEdited, session_, &Session::setOutputName);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, "Output folder", session_->outputFolder());
        if (!dir.isEmpty()) session_->setOutputFolder(dir);
    });
    connect(session_, &Session::outputSettingsChanged, this, &OutputPanel::sync);
    sync();
}

void OutputPanel::sync()
{
    const rc::Dims size = session_->outputSize();
    resolution_->setCurrent(session_->resolutionIndex());
    dims_->setText(QString("%1×%2 · scale=lanczos").arg(size.w).arg(size.h));

    const int vq = session_->videoQuality();
    const int aq = session_->audioQuality();
    const int kbps = rc::audioKbpsForQuality(aq);
    videoQuality_->setValue(vq);
    audioQuality_->setValue(aq);
    videoReadout_->setText(QString("%1 · crf %2").arg(vq).arg(rc::crfForQuality(vq)));
    audioReadout_->setText(QString("%1 · %2 kb/s").arg(aq).arg(kbps));
    videoHint_->setText(rc::qualityHint(vq));

    if (fileName_->text() != session_->outputName()) fileName_->setText(session_->outputName());
    folder_->setText(ui::tildePath(session_->outputFolder()));
    length_->setText(session_->timecode(session_->outputLength()));
    size_->setText(session_->hasClip() ? session_->estimatedSize() : QString());
    audioSummary_->setText(QString("aac · %1 kb/s").arg(kbps));
}
