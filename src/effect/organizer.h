/*
    KDE Infinite Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#pragma once

#include "areas.h"
#include "camera.h"
#include "groups.h"
#include "minimap.h"
#include "noteplacements.h"

#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QSet>
#include <QString>

#include <chrono>
#include <memory>
#include <optional>
#include <unordered_map>

namespace KWin
{
class EffectWindow;
class GLTexture;
class LogicalOutput;
class Region;
struct KeyboardKeyEvent;
struct PointerAxisEvent;
struct PointerButtonEvent;
class RenderTarget;
class RenderViewport;
}

namespace InfiniteDesktop
{

class InfiniteDesktopEffect;
class StateStore;
struct Canvas;
struct ManagedWindow;

/// D-Bus name of the companion app (notes, tray icon).
inline constexpr const char *CompanionService = "org.kde.InfiniteDesktop.Companion";
/// Desktop file name (Wayland app id) of the companion app; its note windows have it.
inline constexpr const char *CompanionAppKey = "org.kde.infinitedesktop.companion";

/**
 * Notes, groups, areas and the map, plus the overlays that belong to them: the canvas
 * menu, drawing a rectangle and the map itself.
 *
 * The logic lives in the core (groups.h, areas.h, noteplacements.h, minimap.h); this class
 * connects it to KWin windows, persistence, the companion app and the screen.
 *
 * Lifetime rules: windows are never stored as pointers here. ManagedWindow carries the
 * note id, the group tag and the area id; pending launches and expected notes only hold
 * strings. Overlay state holds the output's name, not the output.
 */
class Organizer : public QObject
{
    Q_OBJECT

public:
    /// Owned by the effect (no QObject parent), destroyed before the state store.
    Organizer(InfiniteDesktopEffect *effect, StateStore *store);
    ~Organizer() override;

    // ---- Hooks from the effect --------------------------------------------------------
    /// A window was put on a canvas. Returns true if it was placed here (a note or a window
    /// started for a group), so the effect must not place it again.
    bool windowAdopted(ManagedWindow &window, bool newlyMapped);
    /// Before the effect forgets a closed window: keeps the place of notes.
    void windowClosed(ManagedWindow &window);
    /// The user finished moving or resizing a canvas window (KWin or the plugin moved it).
    void windowArranged(ManagedWindow &window);
    /// The window changed its size by itself: areas follow (coalesced).
    void windowResized(ManagedWindow &window);
    /// The effect forgets this window: drop cached textures of it.
    void windowDeleted(KWin::EffectWindow *w);
    /// Pin state of a window changed (shortcut, note button, rules).
    void pinChanged(ManagedWindow &window);
    /// Saves what changed.
    void persist();
    bool isDirty() const
    {
        return m_dirty;
    }
    /// True while something must be painted although nothing else on the canvas changes.
    bool needsPainting(const Canvas &canvas) const;
    bool hasAreas(const Canvas &canvas) const;

    // ---- Painting ---------------------------------------------------------------------
    /// Areas: above the wallpaper, below the windows (canvas layer).
    void paintAreas(const Canvas &canvas, qreal alpha, const KWin::RenderTarget &renderTarget, const KWin::RenderViewport &viewport,
                    KWin::LogicalOutput *output, const KWin::Region &deviceRegion);
    /// Menu, selection rectangle and map: above everything.
    void paintOverlay(const KWin::RenderTarget &renderTarget, const KWin::RenderViewport &viewport, KWin::LogicalOutput *output,
                      const KWin::Region &deviceRegion);

    // ---- Input while an overlay is open (everything is consumed) ----------------------
    bool overlayActive() const
    {
        return m_overlay != Overlay::None;
    }
    bool pointerMotion(const QPointF &pos);
    bool pointerButton(KWin::PointerButtonEvent *event);
    bool pointerAxis(KWin::PointerAxisEvent *event);
    bool keyboardKey(KWin::KeyboardKeyEvent *event);
    void closeOverlay();

    // ---- Actions (shortcuts, canvas menu, D-Bus) --------------------------------------
    void showCanvasMenu(KWin::LogicalOutput *output, const QPointF &screenPos);
    void toggleMap(KWin::LogicalOutput *output);
    void startSelection(bool group, KWin::LogicalOutput *output);
    void newNote(KWin::LogicalOutput *output, const QPointF &screenPos);
    void toggleNotes();
    void openSettings();
    /// Opens the group with this index (shortcut slot), `toggle` is unused for now.
    void openGroup(int index);
    void openGroup(const QString &id);
    /// Stores the current arrangement of a group (the active window's group if empty).
    void saveGroupLayout(const QString &id = QString());

    // ---- D-Bus ------------------------------------------------------------------------
    QString groupsJson() const;
    bool setGroupsJson(const QString &json);
    QString areasJson() const;
    bool setAreasJson(const QString &json);
    void expectNote(const QString &id);
    QString noteState(const QString &id) const;
    void setNotePinned(const QString &id, bool pinned, bool keepAbove);
    void forgetNote(const QString &id);
    void retainNotes(const QStringList &existing);
    /// For the state over D-Bus (runtime tests): open overlay and the textures it holds.
    QJsonObject debugState() const;

Q_SIGNALS:
    void groupsChanged();
    void areasChanged();
    void notePinnedChanged(const QString &id, bool pinned, bool keepAbove);

private:
    enum class Overlay {
        None,
        Menu,
        Selection,
        Map,
    };
    struct MenuItem
    {
        QString id;
        QString text;
        bool separator = false;
    };
    struct PendingLaunch
    {
        QString appKey;
        QString groupId;
        int member = -1;
        QString canvasKey;
        QRectF target;
        std::chrono::steady_clock::time_point deadline;
    };
    struct ExpectedNote
    {
        QString id;
        std::chrono::steady_clock::time_point deadline;
    };
    struct CachedTexture
    {
        std::unique_ptr<KWin::GLTexture> texture;
        QSize size;
    };

    void beginOverlay(Overlay overlay, KWin::LogicalOutput *output);
    /// Places a note or a window started for a group; false for any other window.
    bool placeNewWindow(ManagedWindow &window);
    KWin::LogicalOutput *overlayOutput() const;
    Canvas *canvasForGroup(const Group &group) const;
    QList<Area> areasOf(const Canvas &canvas) const;
    bool areaBelongsTo(const Area &area, const Canvas &canvas) const;
    void updateAreas(const Canvas &canvas);
    void createGroupFromRect(const QRectF &canvasRect, Canvas &canvas);
    void createAreaFromRect(const QRectF &canvasRect, Canvas &canvas);
    void fillMissingGeometry(Group &group, Canvas &canvas);
    void revealGroup(const Group &group, Canvas &canvas);
    bool launch(const QString &desktopFile);
    void callCompanion(const QString &method, const QVariantList &arguments = {});
    void prunePending();
    void markDirty();
    void repaintOverlay();
    QList<QRectF> memberRects(const Group &group) const;

    // Menu
    QList<MenuItem> menuItems() const;
    QRectF menuRect() const;
    int menuItemAt(const QPointF &pos) const;
    void triggerMenuItem(const QString &id);
    QImage renderMenu(qreal scale) const;

    // Map
    void updateMapView();
    QPointF mapCameraCenter(const QPointF &screenPos) const;

    // GL helpers
    KWin::GLTexture *labelTexture(const QString &text, int pixelHeight, const QColor &color);
    KWin::GLTexture *iconTexture(KWin::EffectWindow *window, int pixels);

    InfiniteDesktopEffect *const m_effect;
    StateStore *const m_store;
    GroupStore m_groups;
    AreaStore m_areas;
    NotePlacements m_notes;
    bool m_dirty = false;
    bool m_groupsDirty = false;
    bool m_areasDirty = false;
    bool m_notesDirty = false;

    QList<PendingLaunch> m_launches;
    QList<ExpectedNote> m_expectedNotes;
    struct
    {
        QString outputName;
        QPointF screenPos;
        std::chrono::steady_clock::time_point deadline;
    } m_newNote;
    /// Group that was opened last (for "save arrangement" without an active group window).
    QString m_lastGroup;
    bool m_notesHidden = false;

    Overlay m_overlay = Overlay::None;
    QString m_overlayOutput;
    // Menu
    QPointF m_menuPos;
    QPointF m_menuCanvasPos;
    int m_menuHover = -1;
    std::unique_ptr<KWin::GLTexture> m_menuTexture;
    int m_menuTextureHover = -2;
    qreal m_menuTextureScale = 0;
    // Selection
    bool m_selectGroup = true;
    bool m_selecting = false;
    QPointF m_selectStart;
    QPointF m_selectEnd;
    // Map
    MapView m_map;
    QRectF m_mapPanel;
    bool m_mapDragging = false;
    bool m_mapMoved = false;
    QPointF m_mapPressPos;
    QPointF m_mapStartCenter;

    /// Canvases whose areas are recomputed at the next event loop round.
    QSet<QString> m_areaUpdates;

    std::unordered_map<QString, CachedTexture> m_labels;
    std::unordered_map<KWin::EffectWindow *, CachedTexture> m_icons;
};

} // namespace InfiniteDesktop
