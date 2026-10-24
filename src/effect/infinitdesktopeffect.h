/*
    KDE Infinit Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#pragma once

#include "camera.h"
#include "motion.h"
#include "navigation.h"
#include "placementmemory.h"
#include "shortcutdefs.h"

#include "core/region.h"
#include "effect/effect.h"
#include "effect/effectwindow.h"
#include "scene/item.h"

#include <KConfigWatcher>

#include <QHash>
#include <QMatrix4x4>
#include <QPointer>
#include <QTimer>

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>

class QAction;

namespace KWin
{
class LogicalOutput;
class SurfaceInterface;
class Window;
class VirtualDesktop;
class XdgPopupInterface;
}

namespace InfinitDesktop
{

class CanvasInputFilter;
class CanvasRenderer;
class CrashGuard;
class DBusInterface;
class LauncherPositioner;
class StateStore;

/// Settings in the units the effect works with (converted from InfinitDesktopConfig).
struct EffectSettings
{
    bool dragDesktopToPan = true;
    bool middleButtonPan = true;
    Qt::KeyboardModifiers panModifiers = Qt::MetaModifier | Qt::AltModifier;
    bool wheelZoomOnDesktop = true;
    Qt::KeyboardModifiers zoomModifiers = Qt::MetaModifier;
    bool invertWheelZoom = false;
    bool shiftWheelPans = true;
    bool touchpadScrollPans = true;
    int swipeFingers = 3; // 0 = off
    bool pinchZoom = true;
    bool touchscreenPan = true;
    bool invertScrollPan = false;
    qreal panSpeed = 1.0;
    qreal zoomStep = 1.15;
    qreal minZoom = 0.1;
    qreal maxZoom = 1.0;
    bool snapZoomToOne = true;
    bool sharpZoomedContent = true;
    bool momentum = true;
    qreal friction = 5.0;
    std::chrono::milliseconds animationDuration{280};
    qreal keyboardPanStep = 0.25;
    bool revealOnActivate = true;
    RevealMode revealMode = RevealMode::Minimal;
    bool newWindowsExitZoom = true;
    bool interactWhileZoomed = true;
    bool clickZoomedWindowFocuses = true;
    bool pinOnAllDesktops = true;
    bool pinKeepAboveWindows = false;
    QStringList excludedClasses;
    bool perDesktopCanvas = true;
    bool rememberWindowPlacement = true;
    bool restoreOnCanvas = true;
    bool restoreWindowSize = true;
    QStringList placementExcludedClasses;
    bool flingToOtherScreen = true;
    qreal flingThreshold = 1600;
    bool edgePan = true;
    qreal edgePanMargin = 24;
    qreal edgePanSpeed = 1400;
    bool lockFullscreen = true;
    bool minimizedFullscreenOnCanvas = true;
    bool tileLiveContent = true;
    qreal tileSize = 0.25;
    enum class Grid {
        Never,
        WhileNavigating,
        Always,
    } grid = Grid::Never;
    qreal gridSpacing = 64;
    qreal gridOpacity = 0.35;
    QColor gridColor = Qt::white;
    bool zoomIndicator = true;
    Grid offscreenMarkers = Grid::WhileNavigating; ///< same three states as the grid
    bool launcherAtCursor = false;
    bool crashGuard = true;
    bool rememberCanvas = true;
    bool debugLogging = false;
};

/// One canvas: a camera looking at the windows of one output on one virtual desktop.
struct Canvas
{
    QString key;
    QString outputName;
    QString desktopId;

    /// What is shown right now.
    Camera camera;
    /// The camera the physical window positions correspond to.
    Camera committed;
    CameraMotion motion;
    /// While zoomed (camera.zoom != 1) every window is parked and input is handled by the plugin.
    bool navigation = false;
    /// A gesture/drag is in progress: do not commit, do not start glides from elsewhere.
    int interactions = 0;
    /// The camera before zooming out, restored by Escape.
    std::optional<Camera> beforeNavigation;
    std::optional<Camera> beforeOverview;
    std::optional<Camera> beforeHome;
    std::array<std::optional<Camera>, 4> bookmarks;
    /// Grid fade for the "while navigating" mode.
    qreal gridAlpha = 0.0;
    /// Visibility of the markers of windows outside the view.
    qreal markerAlpha = 0.0;
    TimePoint lastActivity;
    /// Edge scrolling velocity in screen pixels per second while dragging a window.
    QPointF edgePanVelocity;
    TimePoint lastEdgePanTick;
    TimePoint lastFadeTick;
};

/**
 * Owns all canvases. Unlike QHash (whose element addresses change on insertion),
 * a Canvas never moves in memory, so Canvas pointers/references stay valid.
 */
class CanvasStore
{
public:
    Canvas *find(const QString &key) const
    {
        auto it = m_canvases.find(key);
        return it == m_canvases.end() ? nullptr : it->second.get();
    }
    /// Returns the canvas for @p key, creating it if needed.
    Canvas &obtain(const QString &key, bool *created = nullptr)
    {
        std::unique_ptr<Canvas> &slot = m_canvases[key];
        if (created) {
            *created = !slot;
        }
        if (!slot) {
            slot = std::make_unique<Canvas>();
        }
        return *slot;
    }

    class Iterator
    {
    public:
        using Base = std::map<QString, std::unique_ptr<Canvas>>::const_iterator;
        explicit Iterator(Base it)
            : m_it(it)
        {
        }
        Canvas &operator*() const
        {
            return *m_it->second;
        }
        Iterator &operator++()
        {
            ++m_it;
            return *this;
        }
        bool operator!=(const Iterator &other) const
        {
            return m_it != other.m_it;
        }

    private:
        Base m_it;
    };
    Iterator begin() const
    {
        return Iterator(m_canvases.cbegin());
    }
    Iterator end() const
    {
        return Iterator(m_canvases.cend());
    }

private:
    std::map<QString, std::unique_ptr<Canvas>> m_canvases;
};

/// Bookkeeping for every window that lives on a canvas.
struct ManagedWindow
{
    KWin::EffectWindow *effectWindow = nullptr;
    QPointer<KWin::Window> window;
    QString canvasKey;
    /// Frame top left in canvas coordinates.
    QPointF canvasPos;
    /// Physically moved into the void outside of all outputs.
    bool parked = false;
    /// Pinned to the screen with the "Pin Window to Screen" shortcuts.
    bool userPinned = false;
    /// "Keep above" was switched on by the pin shortcut and is restored on unpin.
    bool keepAboveSetByPin = false;
    /// When the window was mapped (newly opened windows only).
    std::optional<TimePoint> mappedAt;
    /// Closed, kept until deleted so close animations are drawn at the right place.
    bool closed = false;
    TimePoint closedAt;
    /// Canvas position of the tile shown while a fullscreen window is minimized.
    std::optional<QPointF> tilePos;
    /// Without a frame and covering exactly its whole screen ("borderless fullscreen" of games,
    /// e.g. under Wine/Proton): treated like a fullscreen window. Follows the geometry changes
    /// made by the client and by KWin, not the plugin's own moves.
    bool borderlessFullScreen = false;
    /// Application key for the placement memory, and whether this window takes part in it.
    QString appKey;
    bool placementEligible = false;
    /// The remembered placement this window was restored from (0 = none).
    quint64 placementEntry = 0;
    /// Resized by the plugin while zoomed out: these edges move, the opposite corner (canvas
    /// coordinates) stays, also when the client answers later or with another size.
    struct ResizeAnchor
    {
        Qt::Edges edges;
        QPointF corner;
    };
    std::optional<ResizeAnchor> resizeAnchor;
    /// The window asked KWin to move it while it was used zoomed out (client side title bar):
    /// the plugin moves it on the canvas, until KWin's move is cancelled the window stays a
    /// canvas window.
    bool moveTakenOver = false;
    /// Buffer scale asked from the client while zoomed out (0: the output's own scale), and
    /// the frame size before, which the client gets back afterwards.
    qreal contentScale = 0.0;
    QSizeF sizeBeforeContentScale;
    /// Keeps the item composited (no direct scanout) while it is drawn transformed.
    std::unique_ptr<KWin::ItemEffect> itemEffect;
    /// Keeps a minimized fullscreen window painted for the live tile.
    std::unique_ptr<KWin::EffectWindowVisibleRef> visibleRef;
};

enum class WindowMode {
    Canvas, ///< moves with the canvas
    Fullscreen, ///< fullscreen, fixed on its screen
    Tile, ///< minimized fullscreen window shown as tile on the canvas
    Minimized,
    Pinned, ///< fixed on the screen
    Moving, ///< interactively moved or resized by KWin
    Hidden, ///< on another desktop or activity
};

/// Result of visual hit testing.
/// What is under the pointer while zoomed out, for using windows at that zoom.
struct ZoomedTarget
{
    KWin::EffectWindow *window = nullptr; ///< canvas window, or one of its popups/dialogs
    ManagedWindow *managed = nullptr; ///< the canvas window it belongs to
    bool decoration = false; ///< on the server side decoration (title bar, borders) of the canvas window
    QMatrix4x4 inputTransform; ///< global pointer position -> position on the window's surface
};

/// Marker at the screen edge for a window outside the view (screen coordinates).
struct OffscreenMarker
{
    /// Diameter of a marker in logical pixels.
    static constexpr qreal Size = 40.0;
    ManagedWindow *window = nullptr;
    QPointF position; ///< centre of the marker
    QPointF direction; ///< unit vector towards the window
};

struct CanvasHit
{
    enum class Kind {
        Nothing, ///< empty canvas / desktop
        Window, ///< a canvas window drawn at its physical place (KWin will deliver the input)
        ParkedWindow, ///< a canvas window that is only drawn there (input must be handled by us)
        Tile,
        Foreign, ///< panel, notification, pinned window, ... (let KWin handle it)
    } kind = Kind::Nothing;
    ManagedWindow *window = nullptr;
};

class InfinitDesktopEffect : public KWin::Effect
{
    Q_OBJECT

public:
    InfinitDesktopEffect();
    ~InfinitDesktopEffect() override;

    static bool supported();

    // KWin::Effect
    void reconfigure(ReconfigureFlags flags) override;
    void prePaintScreen(KWin::ScreenPrePaintData &data) override;
    bool paintScreen(const KWin::RenderTarget &renderTarget, const KWin::RenderViewport &viewport, int mask, const KWin::Region &deviceRegion, KWin::LogicalOutput *screen) override;
    void postPaintScreen() override;
    void prePaintWindow(KWin::RenderView *view, KWin::EffectWindow *w, KWin::WindowPrePaintData &data) override;
    bool paintWindow(const KWin::RenderTarget &renderTarget, const KWin::RenderViewport &viewport, KWin::EffectWindow *w, int mask, const KWin::Region &deviceRegion, KWin::WindowPaintData &data) override;
    bool isActive() const override;
    int requestedEffectChainPosition() const override;
    bool blocksDirectScanout() const override;
    QString debug(const QString &parameter) const override;

    // ---- API for the input filter, the D-Bus interface and the shortcuts ----
    const EffectSettings &settings() const
    {
        return m_settings;
    }
    /// False in safe mode, while paused, while the screen is locked or another fullscreen effect runs.
    bool isOperational() const;
    bool isSafeMode() const;
    bool isPaused() const
    {
        return m_paused;
    }
    void setPaused(bool paused);
    void leaveSafeMode();

    /// The output that strictly contains @p pos, or nullptr.
    KWin::LogicalOutput *outputContaining(const QPointF &pos) const;
    KWin::LogicalOutput *pointerOutput() const;
    Canvas *currentCanvas(KWin::LogicalOutput *output);
    Canvas *canvasByKey(const QString &key);
    KWin::LogicalOutput *outputOf(const Canvas &canvas) const;
    /// True while a focused fullscreen window keeps this output from being navigated.
    bool isLocked(KWin::LogicalOutput *output) const;

    CanvasHit hitTest(const QPointF &pos, KWin::LogicalOutput *output) const;
    /// The Plasma desktop (wallpaper) or a layer shell wallpaper: static, never transformed.
    static bool isWallpaperWindow(KWin::EffectWindow *w);
    /// The canvas rect (frame geometry in canvas coordinates) of a window.
    QRectF canvasRect(const ManagedWindow &window) const;
    QRectF tileCanvasRect(const ManagedWindow &window, const Canvas &canvas) const;
    WindowMode modeOf(const ManagedWindow &window) const;
    /// Fullscreen, or borderless fullscreen (see ManagedWindow::borderlessFullScreen).
    bool isFullScreen(const ManagedWindow &window) const;
    /// Fixed on the screen: pinned by the user, on all desktops, kept above (if enabled) or not movable.
    bool isPinned(const ManagedWindow &window) const;
    ManagedWindow *findManaged(KWin::EffectWindow *w) const
    {
        return record(w);
    }
    const std::unordered_map<KWin::EffectWindow *, std::unique_ptr<ManagedWindow>> &managedWindows() const
    {
        return m_windows;
    }
    /// True while a window of one screen physically reaches into another screen.
    bool hasForeignOverlap() const
    {
        return m_foreignOverlap;
    }
    CanvasRenderer *renderer() const
    {
        return m_renderer.get();
    }
    LauncherPositioner *launcher() const
    {
        return m_launcher.get();
    }

    // Camera operations. Distances are in screen pixels.
    void beginInteraction(Canvas &canvas);
    void endInteraction(Canvas &canvas, const QPointF &screenVelocity = QPointF());
    void setCamera(Canvas &canvas, const Camera &camera);
    void panBy(Canvas &canvas, const QPointF &screenDelta);
    void animateTo(Canvas &canvas, const Camera &camera, std::optional<std::chrono::milliseconds> duration = std::nullopt);
    /// Multiplies the zoom by @p factor around @p screenAnchor.
    void zoomBy(Canvas &canvas, const QPointF &screenAnchor, qreal factor, bool animate);
    void setZoom(Canvas &canvas, const QPointF &screenAnchor, qreal zoom, bool animate);
    void stopMotion(Canvas &canvas);
    /// Moves windows to where the camera shows them (or parks them).
    void commit(Canvas &canvas);
    void commitIfDirty(Canvas &canvas);
    bool isDirty(const Canvas &canvas) const;
    /// Camera the canvas is heading to (motion target or current camera).
    Camera targetCamera(const Canvas &canvas) const;
    void noteActivity(Canvas &canvas);

    // Window operations
    void moveWindowOnCanvas(ManagedWindow &window, Canvas &canvas, const QPointF &canvasPos);
    void moveTileOnCanvas(ManagedWindow &window, Canvas &canvas, const QPointF &canvasPos);
    /// Resize zone of a zoomed out window under @p pos (screen coordinates), none if not resizable.
    Qt::Edges resizeEdgesAt(const ManagedWindow &window, const Canvas &canvas, const QPointF &pos) const;
    /// Resizes a window that is only drawn by the plugin; @p delta is in canvas units from @p start.
    void resizeWindowOnCanvas(ManagedWindow &window, Canvas &canvas, Qt::Edges edges, const QRectF &start, const QPointF &delta);
    void endCanvasResize(ManagedWindow &window);
    /// Shows the resize cursor for @p edges while the pointer is over a resize zone (none: normal cursor).
    void setResizeCursor(Qt::Edges edges);
    /// Markers of the windows outside the view of @p canvas (empty while they are hidden).
    QList<OffscreenMarker> offscreenMarkers(const Canvas &canvas) const;
    ManagedWindow *offscreenMarkerAt(const QPointF &pos, KWin::LogicalOutput *output) const;
    /// Keeps the markers visible while the pointer is close to one of them.
    void keepMarkersNear(const QPointF &pos, KWin::LogicalOutput *output);
    void flyToWindow(ManagedWindow &window);
    /// The window drawn under @p pos while the canvas of @p output is zoomed out, if any.
    std::optional<ZoomedTarget> zoomedTargetAt(const QPointF &pos, KWin::LogicalOutput *output) const;
    /// Current input transformation of a window drawn zoomed out (it changes with the camera).
    std::optional<QMatrix4x4> zoomedInputTransform(KWin::EffectWindow *w) const;
    /// The canvas window that has the pointer while zoomed out (nullptr: none). Xwayland keeps
    /// the pointer of X11 clients inside the screen, so an X11 window parked outside of it
    /// would get every click at the screen edge: while it has the pointer, it is parked on its
    /// screen instead, out of the way of the pointer (see x11InputSpot()).
    void setZoomedInputWindow(KWin::EffectWindow *w);
    /// Keeps @p w on its screen while the plugin moves it after the client started a move
    /// itself, without moving it right now (KWin is reporting its geometry change).
    void holdZoomedInputWindow(KWin::EffectWindow *w);
    void transferWindow(ManagedWindow &window, Canvas &to, const QPointF &canvasPos);
    void activateAndReveal(ManagedWindow &window, bool returnToUnitZoom);
    void restoreTile(ManagedWindow &window);
    /// Parks a physical window of another output that is under the pointer on @p output.
    bool resolveForeignOverlap(const QPointF &pos, KWin::LogicalOutput *output);

    // Interactive (KWin) window moves
    struct MoveState
    {
        QPointer<KWin::Window> window;
        ManagedWindow *record = nullptr;
        KWin::LogicalOutput *output = nullptr;
        QPointF virtualPosition; ///< accumulated unaccelerated motion, used for the fling speed
    };
    std::optional<MoveState> &moveState()
    {
        return m_move;
    }
    void setEdgePan(KWin::LogicalOutput *output, const QPointF &screenVelocity);
    void stopEdgePan();
    /// KWin's "drag to the edge to tile/maximize" uses the same 20 px zone as edge scrolling:
    /// switched off (in memory only) while a canvas window is dragged.
    void suspendElectricBorders();
    void restoreElectricBorders();

    void showZoomIndicator(const Canvas &canvas);
    void showMessage(const QString &text, const QString &icon = QString());
    QString stateJson() const;
    QString windowPlacementsJson() const;
    void forgetWindowPlacements();
    void forgetWindowPlacements(const QString &appKey);
    void requestRepaint(KWin::LogicalOutput *output);
    void requestRepaint(const Canvas &canvas);
    void repaintClosingWindows();

    // Shortcut handlers (also used by D-Bus)
    void triggerAction(ShortcutAction action);
    void zoomToFit(Canvas &canvas, bool toggle);
    void goHome(Canvas &canvas, bool toggle);

Q_SIGNALS:
    void statusChanged();

private:
    void loadSettings();
    void setupShortcuts();
    void setupWindow(KWin::EffectWindow *w);
    void adoptWindow(KWin::EffectWindow *w, bool newlyMapped);
    void forgetWindow(KWin::EffectWindow *w);
    bool isCanvasCandidate(KWin::EffectWindow *w) const;
    ManagedWindow *record(KWin::EffectWindow *w) const;
    ManagedWindow *record(KWin::Window *window) const;
    /// The managed window a popup/transient belongs to.
    ManagedWindow *ownerRecord(KWin::EffectWindow *w) const;
    /// An X11 menu/popup that names no parent (WM_TRANSIENT_FOR), like the override-redirect
    /// menus of Steam (CEF) and Wine.
    bool isLoneX11Popup(KWin::EffectWindow *w) const;
    bool hasOpenX11Popup(KWin::EffectWindow *owner) const;
    QString canvasKeyFor(KWin::LogicalOutput *output, KWin::VirtualDesktop *desktop) const;
    QString canvasKeyForWindow(const ManagedWindow &window, KWin::LogicalOutput *output) const;
    Canvas &ensureCanvas(const QString &key, const QString &outputName, const QString &desktopId);
    bool isCurrentCanvas(const Canvas &canvas) const;
    QRectF workArea(KWin::LogicalOutput *output) const;
    /// A normal window without frame, not maximized, whose frame is exactly the screen it is on.
    bool coversScreenBorderless(const ManagedWindow &window) const;
    /// A window opening while zoomed out returns to 100 % (setting). Not for windows outside of
    /// the task bar: splash and helper windows of launchers (Steam, Wine/Proton).
    bool newWindowEndsZoom(const ManagedWindow &window) const;
    void placeWindow(ManagedWindow &window, Canvas &canvas, KWin::LogicalOutput *output);
    void parkWindow(ManagedWindow &window, KWin::LogicalOutput *output);
    /// Where an X11 window that has the pointer while zoomed out is parked: fully on @p output
    /// (where Xwayland can put the pointer) but not under the pointer (KWin must not pick it).
    QPointF x11InputSpot(const ManagedWindow &window, KWin::LogicalOutput *output) const;
    void moveWindowPhysically(KWin::Window *window, const QPointF &topLeft);
    void enterNavigation(Canvas &canvas);
    void leaveNavigation(Canvas &canvas);
    void updateNavigationState(Canvas &canvas);
    void commitAll();
    void scheduleCommitAll();
    /// Asks zoomed out windows to draw for their size on screen (and the others to draw normally again).
    void applyContentScales();
    void setContentScale(ManagedWindow &window, qreal scale);
    void restoreAllWindows();
    void updateActivity();
    void updateItemEffect(ManagedWindow &window, bool transformed);
    void advanceAnimations(KWin::LogicalOutput *output, TimePoint now);
    void persistSoon();
    void persistNow();
    void updateTileState(ManagedWindow &window);
    /// Puts a window (back) onto the canvas at the place where it is shown on screen right now.
    void adoptCurrentGeometry(ManagedWindow &window);
    bool isPlacementEligible(KWin::EffectWindow *w) const;
    void applyPlacement(ManagedWindow &window, const WindowPlacement &placement);
    void rememberPlacement(ManagedWindow &window);
    static OutputIdentity identityOf(const KWin::LogicalOutput *output);
    KWin::LogicalOutput *findOutput(const OutputIdentity &identity) const;
    void setUserPinned(ManagedWindow &window, bool pinned, bool keepAbove);

    void slotWindowAdded(KWin::EffectWindow *w);
    void slotKeepAboveChanged(KWin::EffectWindow *w);
    void slotWindowClosed(KWin::EffectWindow *w);
    void slotWindowDeleted(KWin::EffectWindow *w);
    void slotWindowActivated(KWin::EffectWindow *w);
    void slotFrameGeometryChanged(KWin::EffectWindow *w, const KWin::RectF &oldGeometry);
    void slotStartUserMovedResized(KWin::EffectWindow *w);
    void slotFinishUserMovedResized(KWin::EffectWindow *w);
    void slotMinimizedChanged(KWin::EffectWindow *w);
    void slotFullScreenChanged(KWin::EffectWindow *w);
    void slotDesktopsChanged(KWin::EffectWindow *w);
    void slotWindowDamaged(KWin::EffectWindow *w);
    void slotCurrentDesktopChanged();
    void slotOutputsChanged();

    /// How a window has to be painted on the output currently being painted.
    struct PaintDecision
    {
        enum class Kind {
            Default,
            Skip,
            Transform,
        } kind = Kind::Default;
        qreal scale = 1.0;
        QPointF translation;
    };
    PaintDecision decide(KWin::EffectWindow *w, KWin::LogicalOutput *output) const;
    /// Where a Wayland popup of a canvas window appears on screen (its frame's top left corner),
    /// placed around the drawn parent; nullopt if it is no xdg popup.
    std::optional<QPointF> visualPopupPosition(KWin::Window *popup, const ManagedWindow &owner, const Canvas &canvas, KWin::LogicalOutput *output, int depth = 0) const;
    /// The output a render viewport shows, or nullptr if it covers several (workspace captures).
    KWin::LogicalOutput *outputForViewport(const KWin::RenderViewport &viewport) const;
    /// The screen whose canvas a window (or the window owning a popup) lives on.
    KWin::LogicalOutput *ownerOutputOf(KWin::EffectWindow *w) const;

    EffectSettings m_settings;
    std::unique_ptr<CrashGuard> m_crashGuard;
    std::unique_ptr<StateStore> m_stateStore;
    std::unique_ptr<CanvasInputFilter> m_inputFilter;
    std::unique_ptr<CanvasRenderer> m_renderer;
    std::unique_ptr<LauncherPositioner> m_launcher;
    DBusInterface *m_dbus = nullptr;
    KConfigWatcher::Ptr m_configWatcher;

    std::unordered_map<KWin::EffectWindow *, std::unique_ptr<ManagedWindow>> m_windows;
    /// X11 window used zoomed out, parked on its screen (setZoomedInputWindow()).
    QPointer<KWin::EffectWindow> m_x11Input;
    /// X11 popups without parent (isLoneX11Popup()) and the input window they opened for. The
    /// popup is a QPointer too: KWin does not always announce the end of unmanaged windows.
    struct X11Popup
    {
        QPointer<KWin::EffectWindow> popup;
        QPointer<KWin::EffectWindow> owner;
    };
    QHash<KWin::EffectWindow *, X11Popup> m_x11Popups;
    /// xdg popups by surface: their positioners place them around the drawn parent.
    QHash<KWin::SurfaceInterface *, QPointer<KWin::XdgPopupInterface>> m_xdgPopups;
    CanvasStore m_canvases;
    PlacementMemory m_placements;
    bool m_placementsDirty = false;
    QList<QAction *> m_actions;

    KWin::LogicalOutput *m_paintOutput = nullptr;
    KWin::Region m_paintRegion;
    bool m_canvasLayerPainted = false;
    std::optional<MoveState> m_move;
    struct ElectricBorders
    {
        bool tiling;
        bool maximize;
    };
    std::optional<ElectricBorders> m_suspendedElectricBorders;
    bool m_applyingLayout = false;
    bool m_foreignOverlap = false;
    bool m_rearranged = false;
    bool m_paused = false;
    bool m_active = false;
    /// Edges of the resize zone under the pointer while zoomed out (mouse interception for the cursor).
    Qt::Edges m_resizeCursorEdges;
    bool m_commitAllScheduled = false;
    QTimer m_persistTimer;
    QTimer m_indicatorTimer;
    /// Applies the content scales of zoomed out windows once the zoom has settled.
    QTimer m_contentScaleTimer;
    QTimer m_scrollCommitTimer;
};

} // namespace InfinitDesktop
