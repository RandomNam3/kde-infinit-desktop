/*
    KDE Infinite Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#pragma once

#include "camera.h"

#include <QHash>
#include <QJsonObject>
#include <QString>

#include <array>
#include <optional>

namespace InfiniteDesktop
{

/// Persistent part of a canvas: camera and bookmarks.
struct StoredCanvas
{
    Camera camera;
    std::array<std::optional<Camera>, 4> bookmarks;
};

/**
 * Loads and saves camera positions and bookmarks of all canvases as JSON in
 * $XDG_STATE_HOME/kde-infinite-desktop/canvas.json. Window positions are not
 * stored: windows of a new session are new windows for KWin anyway.
 */
class StateStore
{
public:
    QHash<QString, StoredCanvas> load() const;
    void save(const QHash<QString, StoredCanvas> &canvases) const;

    /// Remembered window placements (see PlacementMemory), stored next to the canvas state.
    QJsonObject loadPlacements() const;
    void savePlacements(const QJsonObject &placements) const;

    /// Another JSON file next to the canvas state ("groups.json", ...); empty if missing or broken.
    QJsonObject loadFile(const QString &fileName) const;
    void saveFile(const QString &fileName, const QJsonObject &object) const;

    static QString filePath();
    static QString placementsFilePath();
};

} // namespace InfiniteDesktop
