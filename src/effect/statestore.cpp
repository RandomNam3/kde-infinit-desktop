/*
    KDE Infinite Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "statestore.h"
#include "logging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <cmath>

namespace InfiniteDesktop
{

namespace
{

QJsonObject cameraToJson(const Camera &camera)
{
    return QJsonObject{
        {QStringLiteral("x"), camera.center.x()},
        {QStringLiteral("y"), camera.center.y()},
        {QStringLiteral("zoom"), camera.zoom},
    };
}

std::optional<Camera> cameraFromJson(const QJsonValue &value)
{
    if (!value.isObject()) {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    Camera camera;
    camera.center = QPointF(object.value(QStringLiteral("x")).toDouble(), object.value(QStringLiteral("y")).toDouble());
    camera.zoom = object.value(QStringLiteral("zoom")).toDouble(1.0);
    // Never trust files on disk blindly: a broken camera would make the desktop unusable.
    if (!std::isfinite(camera.center.x()) || !std::isfinite(camera.center.y()) || !std::isfinite(camera.zoom)
        || camera.zoom <= 0.0 || std::abs(camera.center.x()) > 1e8 || std::abs(camera.center.y()) > 1e8) {
        return std::nullopt;
    }
    return camera;
}

} // namespace

QString StateStore::filePath()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation);
#else
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
#endif
    return base + QStringLiteral("/kde-infinite-desktop/canvas.json");
}

QString StateStore::placementsFilePath()
{
    return QFileInfo(filePath()).absolutePath() + QStringLiteral("/placements.json");
}

QJsonObject StateStore::loadPlacements() const
{
    QFile file(placementsFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return QJsonObject();
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        qCWarning(KWIN_INFINITEDESKTOP) << "ignoring broken placements file" << placementsFilePath() << error.errorString();
        return QJsonObject();
    }
    return document.object();
}

void StateStore::savePlacements(const QJsonObject &placements) const
{
    const QString path = placementsFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(KWIN_INFINITEDESKTOP) << "cannot write placements file" << path;
        return;
    }
    file.write(QJsonDocument(placements).toJson(QJsonDocument::Compact));
    file.commit();
}

QJsonObject StateStore::loadFile(const QString &fileName) const
{
    const QString path = QFileInfo(filePath()).absolutePath() + QLatin1Char('/') + fileName;
    QFile file(path);
    // Our own files are small; anything huge is not ours to parse inside the compositor.
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024) {
        return QJsonObject();
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        qCWarning(KWIN_INFINITEDESKTOP) << "ignoring broken file" << path << error.errorString();
        return QJsonObject();
    }
    return document.object();
}

void StateStore::saveFile(const QString &fileName, const QJsonObject &object) const
{
    const QString path = QFileInfo(filePath()).absolutePath() + QLatin1Char('/') + fileName;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(KWIN_INFINITEDESKTOP) << "cannot write" << path;
        return;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    file.commit();
}

QHash<QString, StoredCanvas> StateStore::load() const
{
    QHash<QString, StoredCanvas> result;
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return result;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        qCWarning(KWIN_INFINITEDESKTOP) << "ignoring broken state file" << filePath() << error.errorString();
        return result;
    }
    const QJsonObject canvases = document.object().value(QStringLiteral("canvases")).toObject();
    for (auto it = canvases.constBegin(); it != canvases.constEnd(); ++it) {
        const QJsonObject object = it.value().toObject();
        StoredCanvas stored;
        if (auto camera = cameraFromJson(object.value(QStringLiteral("camera")))) {
            stored.camera = *camera;
        }
        const QJsonArray bookmarks = object.value(QStringLiteral("bookmarks")).toArray();
        for (int i = 0; i < int(stored.bookmarks.size()) && i < bookmarks.size(); ++i) {
            stored.bookmarks[i] = cameraFromJson(bookmarks.at(i));
        }
        result.insert(it.key(), stored);
    }
    return result;
}

void StateStore::save(const QHash<QString, StoredCanvas> &canvases) const
{
    QJsonObject canvasesObject;
    for (auto it = canvases.constBegin(); it != canvases.constEnd(); ++it) {
        QJsonArray bookmarks;
        for (const std::optional<Camera> &bookmark : it->bookmarks) {
            bookmarks.append(bookmark ? QJsonValue(cameraToJson(*bookmark)) : QJsonValue());
        }
        canvasesObject.insert(it.key(), QJsonObject{
            {QStringLiteral("camera"), cameraToJson(it->camera)},
            {QStringLiteral("bookmarks"), bookmarks},
        });
    }
    const QJsonObject root{
        {QStringLiteral("version"), 1},
        {QStringLiteral("canvases"), canvasesObject},
    };

    const QString path = filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(KWIN_INFINITEDESKTOP) << "cannot write state file" << path;
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.commit();
}

} // namespace InfiniteDesktop
