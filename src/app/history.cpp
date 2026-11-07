#include "app/history.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace paths {

QString configFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + "/reference-clipper/config.toml";
}

QString dataDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/reference-clipper";
}

QString historyFile()
{
    return dataDir() + "/history.json";
}

QString thumbFile(const QString& jobId)
{
    return dataDir() + "/thumbs/" + jobId + ".jpg";
}

QString defaultOutputDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/Clips";
}

}  // namespace paths

namespace {

void writeAtomically(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).path());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(bytes);
    file.commit();
}

QJsonArray spansToJson(const std::vector<rc::Span>& spans)
{
    QJsonArray array;
    for (const rc::Span& s : spans) array.append(QJsonObject{{"start", s.start}, {"end", s.end}});
    return array;
}

std::vector<rc::Span> spansFromJson(const QJsonArray& array)
{
    std::vector<rc::Span> spans;
    int64_t id = 1;
    for (const QJsonValue& value : array) {
        const QJsonObject o = value.toObject();
        spans.push_back({id++, o["start"].toDouble(), o["end"].toDouble()});
    }
    return spans;
}

}  // namespace

rc::Config loadConfig()
{
    QFile file(paths::configFile());
    if (!file.open(QIODevice::ReadOnly)) return {};
    return rc::parseConfig(file.readAll().toStdString());
}

void saveConfig(const rc::Config& config)
{
    writeAtomically(paths::configFile(), QByteArray::fromStdString(rc::serializeConfig(config)));
}

QVector<HistoryEntry> loadHistory()
{
    QVector<HistoryEntry> entries;
    QFile file(paths::historyFile());
    if (!file.open(QIODevice::ReadOnly)) return entries;
    const QJsonArray array = QJsonDocument::fromJson(file.readAll()).array();
    for (const QJsonValue& value : array) {
        const QJsonObject o = value.toObject();
        HistoryEntry e;
        e.id = o["id"].toString();
        e.source = o["source"].toString();
        e.output = o["output"].toString();
        e.name = o["name"].toString();
        e.date = QDateTime::fromString(o["date"].toString(), Qt::ISODate);
        e.spans = spansFromJson(o["spans"].toArray());
        e.segments = spansFromJson(o["segments"].toArray());
        e.settings.resolution = o["resolution"].toString("1080p");
        e.settings.presetHeight = o["presetHeight"].toInt();
        e.settings.size = {o["width"].toInt(), o["height"].toInt()};
        if (const QJsonObject c = o["crop"].toObject(); !c.isEmpty())
            e.settings.crop = rc::Crop{c["x"].toInt(), c["y"].toInt(), c["w"].toInt(), c["h"].toInt()};
        e.settings.videoQuality = o["videoQuality"].toInt(70);
        e.settings.audioQuality = o["audioQuality"].toInt(70);
        e.sizeBytes = o["sizeBytes"].toInteger();
        e.duration = o["duration"].toDouble();
        e.driftMs = o["driftMs"].toDouble();
        entries.push_back(std::move(e));
    }
    return entries;
}

void saveHistory(const QVector<HistoryEntry>& entries)
{
    QJsonArray array;
    for (const HistoryEntry& e : entries) {
        QJsonObject entry{
            {"id", e.id},
            {"source", e.source},
            {"output", e.output},
            {"name", e.name},
            {"date", e.date.toString(Qt::ISODate)},
            {"spans", spansToJson(e.spans)},
            {"segments", spansToJson(e.segments)},
            {"resolution", e.settings.resolution},
            {"presetHeight", e.settings.presetHeight},
            {"width", e.settings.size.w},
            {"height", e.settings.size.h},
            {"videoQuality", e.settings.videoQuality},
            {"audioQuality", e.settings.audioQuality},
            {"sizeBytes", qint64(e.sizeBytes)},
            {"duration", e.duration},
            {"driftMs", e.driftMs},
        };
        if (const auto& c = e.settings.crop)
            entry["crop"] = QJsonObject{{"x", c->x}, {"y", c->y}, {"w", c->w}, {"h", c->h}};
        array.append(entry);
    }
    writeAtomically(paths::historyFile(), QJsonDocument(array).toJson());
}
