#pragma once

#include "app/widgets.h"

class QLineEdit;
class QSlider;
class Session;

class OutputPanel : public Panel {
    Q_OBJECT
public:
    explicit OutputPanel(Session* session, QWidget* parent = nullptr);

private:
    void sync();

    Session* session_;
    Segmented* resolution_;
    QLabel* dims_;
    QSlider* videoQuality_;
    QLabel* videoReadout_;
    QLabel* videoHint_;
    QSlider* audioQuality_;
    QLabel* audioReadout_;
    QLineEdit* fileName_;
    QLineEdit* folder_;
    QLabel* length_;
    QLabel* size_;
    QLabel* audioSummary_;
};
