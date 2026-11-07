// Job history in ~/.local/share/reference-clipper, and the XDG paths.
#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>
#include <vector>

#include "core/config.h"
#include "core/quality.h"
#include "core/timeline.h"

namespace paths {
QString configFile();
QString dataDir();
QString historyFile();
QString thumbFile(const QString& jobId);
QString defaultOutputDir();
}  // namespace paths

rc::Config loadConfig();
void saveConfig(const rc::Config& config);

struct JobSettings {
    QString resolution = "1080p";
    int presetHeight = 1080;
    rc::Dims size;
    int videoQuality = 70;
    int audioQuality = 70;
};

struct HistoryEntry {
    QString id;
    QString source;
    QString output;
    QString name;
    QDateTime date;
    std::vector<rc::Span> spans;     // as marked
    std::vector<rc::Span> segments;  // as encoded, snapped to frames
    JobSettings settings;
    long long sizeBytes = 0;
    double duration = 0;
    double driftMs = 0;
};

QVector<HistoryEntry> loadHistory();
void saveHistory(const QVector<HistoryEntry>& entries);
