/*
    KDE Infinit Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "infinitdesktopeffect.h"
#include "canvasrenderer.h"
#include "crashguard.h"
#include "dbusinterface.h"
#include "infinitdesktopconfig.h"
#include "inputfilter.h"
#include "launcherpositioner.h"
#include "logging.h"
#include "parking.h"
#include "statestore.h"

#include "core/output.h"
#include "core/rendertarget.h"
#include "core/renderviewport.h"
#include "effect/effecthandler.h"
#include "input.h"
#include "options.h"
#include "pointer_input.h"
#include "scene/windowitem.h"
#include "virtualdesktops.h"
#include "wayland/surface.h"
#include "wayland/xdgshell.h"
#include "wayland_server.h"
#include "window.h"
#include "workspace.h"

#include <KGlobalAccel>
#include <KLocalizedString>

#include <QAction>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineF>
#include <QScopedValueRollback>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace std::chrono_literals;

namespace InfinitDesktop
{

namespace
{

constexpr qreal RevealMargin = 24.0;
constexpr qreal FitMargin = 48.0;
constexpr auto ZoomStepDuration = 150ms;
constexpr auto ActivityLinger = 700ms;
constexpr auto MarkerLinger = 2000ms;
constexpr qreal MarkerSize = OffscreenMarker::Size;
/// Distance of the markers from the edge of the usable screen area.
constexpr qreal MarkerMargin = 10.0;
/// Zoomed out windows are asked to redraw for their size once the zoom rested this long.
constexpr auto ContentScaleDelay = 350ms;
/// Below this, windows are only shrunk (tiny buffers are not worth a redraw).
constexpr qreal MinimumContentScale = 0.25;
/// Upper bound for other effects' close animations of parked windows.
constexpr auto ClosingAnimationLimit = 10s;
/// Offset between windows of one application that would open at the same place.
constexpr qreal CascadeOffset = 32.0;
/// Width of the resize zone along the edges of a zoomed out window, in screen pixels.
constexpr qreal ResizeGrab = 8.0;
/// Smallest size a window gets by resizing it while zoomed out.
constexpr qreal MinimumResizeSize = 64.0;

QRectF toQRectF(const KWin::RectF &rect)
{
    return QRectF(rect.x(), rect.y(), rect.width(), rect.height());
}

QRectF outputRect(const KWin::LogicalOutput *output)
{
    return output ? toQRectF(output->geometryF()) : QRectF();
}

QList<QRectF> allOutputRects(int *indexOf = nullptr, const KWin::LogicalOutput *of = nullptr)
{
    QList<QRectF> rects;
    const auto outputs = KWin::workspace()->outputs();
    rects.reserve(outputs.size());
    for (int i = 0; i < outputs.size(); ++i) {
        if (indexOf && outputs[i] == of) {
            *indexOf = i;
        }
        rects.append(outputRect(outputs[i]));
    }
    return rects;
}

bool intersectsAnyOutput(const QRectF &rect)
{
    const auto outputs = KWin::workspace()->outputs();
    return std::any_of(outputs.cbegin(), outputs.cend(), [&rect](const KWin::LogicalOutput *output) {
        return outputRect(output).intersects(rect);
    });
}

Qt::KeyboardModifiers panModifiersFromConfig(int value)
{
    using E = InfinitDesktopConfig::EnumPanModifier;
    switch (value) {
    case E::MetaAlt:
        return Qt::MetaModifier | Qt::AltModifier;
    case E::MetaCtrl:
        return Qt::MetaModifier | Qt::ControlModifier;
    case E::MetaShift:
        return Qt::MetaModifier | Qt::ShiftModifier;
    case E::CtrlAlt:
        return Qt::ControlModifier | Qt::AltModifier;
    case E::Meta:
        return Qt::MetaModifier;
    default:
        return Qt::NoModifier;
    }
}

Qt::KeyboardModifiers zoomModifiersFromConfig(int value)
{
    using E = InfinitDesktopConfig::EnumZoomModifier;
    switch (value) {
    case E::Meta:
        return Qt::MetaModifier;
    case E::MetaAlt:
        return Qt::MetaModifier | Qt::AltModifier;
    case E::MetaShift:
        return Qt::MetaModifier | Qt::ShiftModifier;
    case E::CtrlAlt:
        return Qt::ControlModifier | Qt::AltModifier;
    default:
        return Qt::NoModifier;
    }
}

QJsonObject cameraJson(const Camera &camera)
{
    return QJsonObject{{QStringLiteral("x"), camera.center.x()}, {QStringLiteral("y"), camera.center.y()}, {QStringLiteral("zoom"), camera.zoom}};
}

QJsonObject rectJson(const QRectF &rect)
{
    return QJsonObject{{QStringLiteral("x"), rect.x()}, {QStringLiteral("y"), rect.y()}, {QStringLiteral("width"), rect.width()}, {QStringLiteral("height"), rect.height()}};
}

QString modeName(WindowMode mode)
{
    switch (mode) {
    case WindowMode::Canvas:
        return QStringLiteral("canvas");
    case WindowMode::Fullscreen:
        return QStringLiteral("fullscreen");
    case WindowMode::Tile:
        return QStringLiteral("tile");
    case WindowMode::Minimized:
        return QStringLiteral("minimized");
    case WindowMode::Pinned:
        return QStringLiteral("pinned");
    case WindowMode::Moving:
        return QStringLiteral("moving");
    case WindowMode::Hidden:
        return QStringLiteral("hidden");
    }
    return QString();
}

} // namespace

// ============================================================================
// Construction
// ============================================================================

bool InfinitDesktopEffect::supported()
{
    return KWin::effects->waylandDisplay() != nullptr;
}

InfinitDesktopEffect::InfinitDesktopEffect()
    : m_crashGuard(std::make_unique<CrashGuard>())
    , m_stateStore(std::make_unique<StateStore>())
{
    InfinitDesktopConfig::instance(KWin::effects->config());
    loadSettings();

    if (m_crashGuard->start(m_settings.crashGuard)) {
        showMessage(i18n("KDE Infinit Desktop started in safe mode after repeated crashes."), QStringLiteral("dialog-warning"));
        QTimer::singleShot(6s, this, [this]() {
            KWin::effects->hideOnScreenMessage();
        });
    }

    m_renderer = std::make_unique<CanvasRenderer>();
    m_launcher = std::make_unique<LauncherPositioner>(this);

    if (m_settings.rememberCanvas) {
        const auto stored = m_stateStore->load();
        for (auto it = stored.cbegin(); it != stored.cend(); ++it) {
            const QStringList parts = it.key().split(QLatin1Char('|'));
            if (parts.size() != 2) {
                continue;
            }
            Canvas &canvas = ensureCanvas(it.key(), parts[0], parts[1]);
            // Never come back zoomed: a zoomed canvas parks every window.
            canvas.camera = Camera{it->camera.center, 1.0};
            canvas.committed = canvas.camera;
            canvas.bookmarks = it->bookmarks;
        }
    }

    m_placements.fromJson(m_stateStore->loadPlacements());

    m_persistTimer.setSingleShot(true);
    m_persistTimer.setInterval(2s);
    connect(&m_persistTimer, &QTimer::timeout, this, &InfinitDesktopEffect::persistNow);

    m_contentScaleTimer.setSingleShot(true);
    m_contentScaleTimer.setInterval(ContentScaleDelay);
    connect(&m_contentScaleTimer, &QTimer::timeout, this, &InfinitDesktopEffect::applyContentScales);
    m_indicatorTimer.setSingleShot(true);
    m_indicatorTimer.setInterval(900ms);
    connect(&m_indicatorTimer, &QTimer::timeout, this, []() {
        KWin::effects->hideOnScreenMessage();
    });

    using KWin::EffectsHandler;
    connect(KWin::effects, &EffectsHandler::windowAdded, this, &InfinitDesktopEffect::slotWindowAdded);
    connect(KWin::effects, &EffectsHandler::windowClosed, this, &InfinitDesktopEffect::slotWindowClosed);
    connect(KWin::effects, &EffectsHandler::windowDeleted, this, &InfinitDesktopEffect::slotWindowDeleted);
    connect(KWin::effects, &EffectsHandler::windowActivated, this, &InfinitDesktopEffect::slotWindowActivated);
    connect(KWin::effects, &EffectsHandler::desktopChanged, this, &InfinitDesktopEffect::slotCurrentDesktopChanged);
    connect(KWin::effects, &EffectsHandler::screenAdded, this, &InfinitDesktopEffect::slotOutputsChanged);
    connect(KWin::effects, &EffectsHandler::screenRemoved, this, &InfinitDesktopEffect::slotOutputsChanged);
    connect(KWin::effects, &EffectsHandler::virtualScreenGeometryChanged, this, &InfinitDesktopEffect::slotOutputsChanged);
    connect(KWin::effects, &EffectsHandler::screenLockingChanged, this, &InfinitDesktopEffect::updateActivity);
    connect(KWin::effects, &EffectsHandler::hasActiveFullScreenEffectChanged, this, &InfinitDesktopEffect::updateActivity);

    // KWin places popups around the parent's real position; their positioners let
    // visualPopupPosition() place them around the drawn parent instead.
    if (auto *shell = KWin::waylandServer() ? KWin::waylandServer()->findChild<KWin::XdgShellInterface *>() : nullptr) {
        connect(shell, &KWin::XdgShellInterface::popupCreated, this, [this](KWin::XdgPopupInterface *popup) {
            m_xdgPopups.insert(popup->surface(), popup);
            connect(popup, &QObject::destroyed, this, [this]() {
                for (auto it = m_xdgPopups.begin(); it != m_xdgPopups.end();) {
                    it = it.value() ? std::next(it) : m_xdgPopups.erase(it);
                }
            });
        });
    }

    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow *w : windows) {
        setupWindow(w);
        adoptWindow(w, false);
    }

    setupShortcuts();

    m_inputFilter = std::make_unique<CanvasInputFilter>(this);
    KWin::input()->installInputEventFilter(m_inputFilter.get());

    m_dbus = new DBusInterface(this);

    m_configWatcher = KConfigWatcher::create(KWin::effects->config());
    connect(m_configWatcher.get(), &KConfigWatcher::configChanged, this, [this](const KConfigGroup &group) {
        if (group.name() == QLatin1String("Effect-infinitdesktop")) {
            reconfigure(ReconfigureAll);
        }
    });

    commitAll();
    updateActivity();
    qCDebug(KWIN_INFINITDESKTOP) << "KDE Infinit Desktop loaded, managing" << m_windows.size() << "windows";
}

InfinitDesktopEffect::~InfinitDesktopEffect()
{
    // Order matters: no more input while windows are being restored.
    if (m_inputFilter) {
        m_inputFilter->endZoomedInput();
    }
    m_inputFilter.reset();
    setResizeCursor({});
    m_contentScaleTimer.stop();
    for (auto &[w, managed] : m_windows) {
        setContentScale(*managed, 0.0);
    }
    m_launcher.reset();
    restoreElectricBorders();
    persistNow();
    restoreAllWindows();
    m_crashGuard->markClean();
}

void InfinitDesktopEffect::reconfigure(ReconfigureFlags)
{
    loadSettings();
    for (auto &[w, window] : m_windows) {
        updateTileState(*window);
    }
    commitAll();
    updateActivity();
}

void InfinitDesktopEffect::loadSettings()
{
    InfinitDesktopConfig *config = InfinitDesktopConfig::self();
    config->read();

    EffectSettings &s = m_settings;
    s.dragDesktopToPan = config->dragDesktopToPan();
    s.middleButtonPan = config->middleButtonPan();
    s.panModifiers = panModifiersFromConfig(config->panModifier());
    s.wheelZoomOnDesktop = config->wheelZoomOnDesktop();
    s.zoomModifiers = zoomModifiersFromConfig(config->zoomModifier());
    s.invertWheelZoom = config->invertWheelZoom();
    s.shiftWheelPans = config->shiftWheelPans();
    s.touchpadScrollPans = config->touchpadScrollPans();
    switch (config->swipeFingers()) {
    case InfinitDesktopConfig::EnumSwipeFingers::Three:
        s.swipeFingers = 3;
        break;
    case InfinitDesktopConfig::EnumSwipeFingers::Four:
        s.swipeFingers = 4;
        break;
    default:
        s.swipeFingers = 0;
        break;
    }
    s.pinchZoom = config->pinchZoom();
    s.touchscreenPan = config->touchscreenPan();
    s.invertScrollPan = config->invertScrollPan();
    s.panSpeed = std::clamp(config->panSpeed(), 20, 400) / 100.0;
    s.zoomStep = 1.0 + std::clamp(config->zoomStep(), 2, 100) / 100.0;
    s.minZoom = std::clamp(config->minZoom(), 2, 100) / 100.0;
    s.maxZoom = std::clamp(config->maxZoom(), 100, 400) / 100.0;
    s.snapZoomToOne = config->snapZoomToOne();
    s.sharpZoomedContent = config->sharpZoomedContent();
    s.momentum = config->momentum();
    s.friction = std::clamp(config->friction(), 1, 20);
    s.animationDuration = std::chrono::milliseconds(std::clamp(config->animationDuration(), 0, 2000));
    s.keyboardPanStep = std::clamp(config->keyboardPanStep(), 5, 100) / 100.0;
    s.revealOnActivate = config->revealOnActivate();
    s.revealMode = config->revealMode() == InfinitDesktopConfig::EnumRevealMode::Center ? RevealMode::Center : RevealMode::Minimal;
    s.newWindowsExitZoom = config->newWindowsExitZoom();
    s.interactWhileZoomed = config->interactWhileZoomed();
    s.clickZoomedWindowFocuses = config->clickZoomedWindowFocuses();
    s.pinOnAllDesktops = config->pinOnAllDesktops();
    s.pinKeepAboveWindows = config->pinKeepAboveWindows();
    s.excludedClasses = config->excludedWindowClasses();
    s.perDesktopCanvas = config->perDesktopCanvas();
    s.rememberWindowPlacement = config->rememberWindowPlacement();
    s.restoreOnCanvas = config->restorePlacementMode() == InfinitDesktopConfig::EnumRestorePlacementMode::Canvas;
    s.restoreWindowSize = config->restoreWindowSize();
    s.placementExcludedClasses = config->placementExcludedClasses();
    s.flingToOtherScreen = config->flingToOtherScreen();
    s.flingThreshold = std::clamp(config->flingThreshold(), 100, 10000);
    s.edgePan = config->edgePan();
    s.edgePanMargin = std::clamp(config->edgePanMargin(), 2, 200);
    s.edgePanSpeed = std::clamp(config->edgePanSpeed(), 100, 8000);
    s.lockFullscreen = config->lockFullscreen();
    s.minimizedFullscreenOnCanvas = config->minimizedFullscreenOnCanvas();
    s.tileLiveContent = config->minimizedFullscreenContent() == InfinitDesktopConfig::EnumMinimizedFullscreenContent::LiveContent;
    s.tileSize = std::clamp(config->tileSize(), 5, 80) / 100.0;
    switch (config->backgroundGrid()) {
    case InfinitDesktopConfig::EnumBackgroundGrid::Never:
        s.grid = EffectSettings::Grid::Never;
        break;
    case InfinitDesktopConfig::EnumBackgroundGrid::Always:
        s.grid = EffectSettings::Grid::Always;
        break;
    default:
        s.grid = EffectSettings::Grid::WhileNavigating;
        break;
    }
    s.gridSpacing = std::clamp(config->gridSpacing(), 16, 512);
    s.gridOpacity = std::clamp(config->gridOpacity(), 5, 100) / 100.0;
    s.gridColor = config->gridColor();
    s.zoomIndicator = config->zoomIndicator();
    switch (config->offscreenMarkers()) {
    case InfinitDesktopConfig::EnumOffscreenMarkers::Never:
        s.offscreenMarkers = EffectSettings::Grid::Never;
        break;
    case InfinitDesktopConfig::EnumOffscreenMarkers::Always:
        s.offscreenMarkers = EffectSettings::Grid::Always;
        break;
    default:
        s.offscreenMarkers = EffectSettings::Grid::WhileNavigating;
        break;
    }
    s.launcherAtCursor = config->launcherPosition() == InfinitDesktopConfig::EnumLauncherPosition::Cursor;
    s.crashGuard = config->crashGuard();
    s.rememberCanvas = config->rememberCanvas();
    s.debugLogging = config->debugLogging();

    const_cast<QLoggingCategory &>(KWIN_INFINITDESKTOP()).setEnabled(QtDebugMsg, s.debugLogging);
    const_cast<QLoggingCategory &>(KWIN_INFINITDESKTOP()).setEnabled(QtInfoMsg, s.debugLogging);
}

void InfinitDesktopEffect::setupShortcuts()
{
    for (const ShortcutDefinition &definition : shortcutDefinitions()) {
        auto *action = new QAction(this);
        action->setObjectName(QString::fromLatin1(definition.id));
        action->setText(definition.text.toString());
        KGlobalAccel::self()->setGlobalShortcut(action, definition.defaultShortcuts);
        const ShortcutAction id = definition.action;
        connect(action, &QAction::triggered, this, [this, id]() {
            triggerAction(id);
        });
        m_actions.append(action);
    }
}

// ============================================================================
// State queries
// ============================================================================

bool InfinitDesktopEffect::isSafeMode() const
{
    return m_crashGuard->isSafeMode();
}

bool InfinitDesktopEffect::isOperational() const
{
    if (m_crashGuard->isSafeMode() || KWin::effects->isScreenLocked()) {
        return false;
    }
    const KWin::Effect *fullScreenEffect = KWin::effects->activeFullScreenEffect();
    return !fullScreenEffect || fullScreenEffect == this;
}

void InfinitDesktopEffect::setPaused(bool paused)
{
    if (m_paused == paused) {
        return;
    }
    m_paused = paused;
    if (paused) {
        // Gestures are off while paused, so do not leave anything half way.
        for (Canvas &canvas : m_canvases) {
            if (canvas.navigation || !canvas.camera.isUnitZoom()) {
                canvas.motion.stop();
                setCamera(canvas, Camera{canvas.camera.center, 1.0});
            }
            canvas.interactions = 0;
            commitIfDirty(canvas);
        }
        stopEdgePan();
        showMessage(i18n("Infinit Desktop gestures paused"), QStringLiteral("media-playback-pause"));
    } else {
        showMessage(i18n("Infinit Desktop gestures resumed"), QStringLiteral("media-playback-start"));
    }
    m_indicatorTimer.start(1500ms);
    updateActivity();
    Q_EMIT statusChanged();
}

void InfinitDesktopEffect::leaveSafeMode()
{
    if (!m_crashGuard->isSafeMode()) {
        return;
    }
    m_crashGuard->leaveSafeMode();
    KWin::effects->hideOnScreenMessage();
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow *w : windows) {
        adoptWindow(w, false);
    }
    commitAll();
    updateActivity();
    Q_EMIT statusChanged();
}

KWin::LogicalOutput *InfinitDesktopEffect::outputContaining(const QPointF &pos) const
{
    const auto outputs = KWin::workspace()->outputs();
    for (KWin::LogicalOutput *output : outputs) {
        if (outputRect(output).contains(pos)) {
            return output;
        }
    }
    return nullptr;
}

KWin::LogicalOutput *InfinitDesktopEffect::pointerOutput() const
{
    const QPointF pos = KWin::effects->cursorPos();
    if (KWin::LogicalOutput *output = outputContaining(pos)) {
        return output;
    }
    return KWin::workspace()->outputAt(pos);
}

QString InfinitDesktopEffect::canvasKeyFor(KWin::LogicalOutput *output, KWin::VirtualDesktop *desktop) const
{
    const QString desktopId = (m_settings.perDesktopCanvas && desktop) ? desktop->id() : QStringLiteral("*");
    return output->name() + QLatin1Char('|') + desktopId;
}

QString InfinitDesktopEffect::canvasKeyForWindow(const ManagedWindow &window, KWin::LogicalOutput *output) const
{
    KWin::VirtualDesktop *desktop = nullptr;
    const auto desktops = window.effectWindow->desktops();
    if (desktops.size() == 1 && !window.effectWindow->isOnAllDesktops()) {
        desktop = desktops.first();
    } else {
        desktop = KWin::effects->currentDesktop(output);
    }
    return canvasKeyFor(output, desktop);
}

Canvas &InfinitDesktopEffect::ensureCanvas(const QString &key, const QString &outputName, const QString &desktopId)
{
    bool created = false;
    Canvas &canvas = m_canvases.obtain(key, &created);
    if (created) {
        canvas.key = key;
        canvas.outputName = outputName;
        canvas.desktopId = desktopId;
    }
    return canvas;
}

Canvas *InfinitDesktopEffect::currentCanvas(KWin::LogicalOutput *output)
{
    if (!output) {
        return nullptr;
    }
    KWin::VirtualDesktop *desktop = KWin::effects->currentDesktop(output);
    const QString key = canvasKeyFor(output, desktop);
    return &ensureCanvas(key, output->name(), (m_settings.perDesktopCanvas && desktop) ? desktop->id() : QStringLiteral("*"));
}

Canvas *InfinitDesktopEffect::canvasByKey(const QString &key)
{
    return m_canvases.find(key);
}

KWin::LogicalOutput *InfinitDesktopEffect::outputOf(const Canvas &canvas) const
{
    return KWin::workspace()->findOutput(canvas.outputName);
}

bool InfinitDesktopEffect::isCurrentCanvas(const Canvas &canvas) const
{
    KWin::LogicalOutput *output = outputOf(canvas);
    return output && canvas.key == canvasKeyFor(output, KWin::effects->currentDesktop(output));
}

bool InfinitDesktopEffect::isLocked(KWin::LogicalOutput *output) const
{
    if (!m_settings.lockFullscreen || !output) {
        return false;
    }
    const KWin::EffectWindow *active = KWin::effects->activeWindow();
    return active && active->isFullScreen() && !active->isMinimized() && active->window() && active->window()->output() == output;
}

QRectF InfinitDesktopEffect::workArea(KWin::LogicalOutput *output) const
{
    return toQRectF(KWin::effects->clientArea(KWin::MaximizeArea, output));
}

ManagedWindow *InfinitDesktopEffect::record(KWin::EffectWindow *w) const
{
    auto it = m_windows.find(w);
    return it == m_windows.end() ? nullptr : it->second.get();
}

ManagedWindow *InfinitDesktopEffect::record(KWin::Window *window) const
{
    if (!window) {
        return nullptr;
    }
    for (const auto &[w, managed] : m_windows) {
        if (managed->window == window) {
            return managed.get();
        }
    }
    return nullptr;
}

ManagedWindow *InfinitDesktopEffect::ownerRecord(KWin::EffectWindow *w) const
{
    KWin::Window *window = w ? w->window() : nullptr;
    if (!window) {
        return nullptr;
    }
    int depth = 0;
    for (KWin::Window *parent = window->transientFor(); parent && depth < 16; parent = parent->transientFor(), ++depth) {
        if (ManagedWindow *managed = record(parent)) {
            return managed;
        }
    }
    return nullptr;
}

QRectF InfinitDesktopEffect::canvasRect(const ManagedWindow &window) const
{
    return QRectF(window.canvasPos, window.effectWindow->frameGeometry().size());
}

QRectF InfinitDesktopEffect::tileCanvasRect(const ManagedWindow &window, const Canvas &canvas) const
{
    KWin::LogicalOutput *output = outputOf(canvas);
    const QSizeF outputSize = output ? output->geometryF().size() : QSizeF(1920, 1080);
    const QSizeF frame = window.effectWindow->frameGeometry().size();
    const qreal width = std::max<qreal>(64.0, outputSize.width() * m_settings.tileSize);
    const qreal aspect = frame.width() > 0 && frame.height() > 0 ? frame.height() / frame.width() : 9.0 / 16.0;
    return QRectF(window.tilePos.value_or(QPointF()), QSizeF(width, width * aspect));
}

WindowMode InfinitDesktopEffect::modeOf(const ManagedWindow &window) const
{
    const KWin::EffectWindow *w = window.effectWindow;
    if (window.closed) {
        return WindowMode::Canvas;
    }
    if (!window.window) {
        return WindowMode::Hidden;
    }
    if (!w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) {
        return WindowMode::Hidden;
    }
    if (w->isMinimized()) {
        return (w->isFullScreen() && m_settings.minimizedFullscreenOnCanvas) ? WindowMode::Tile : WindowMode::Minimized;
    }
    if (w->isFullScreen()) {
        return WindowMode::Fullscreen;
    }
    if ((window.window->isInteractiveMove() || window.window->isInteractiveResize())) {
        return WindowMode::Moving;
    }
    if (isPinned(window)) {
        return WindowMode::Pinned;
    }
    return WindowMode::Canvas;
}

bool InfinitDesktopEffect::isPinned(const ManagedWindow &window) const
{
    const KWin::EffectWindow *w = window.effectWindow;
    return window.userPinned || (m_settings.pinOnAllDesktops && w->isOnAllDesktops()) || (m_settings.pinKeepAboveWindows && w->keepAbove())
        || (window.window && !window.window->isMovable());
}

bool InfinitDesktopEffect::isDirty(const Canvas &canvas) const
{
    return canvas.motion.isActive() || !canvas.camera.fuzzyEquals(canvas.committed);
}

Camera InfinitDesktopEffect::targetCamera(const Canvas &canvas) const
{
    return canvas.motion.isActive() ? canvas.motion.target() : canvas.camera;
}

// ============================================================================
// Window tracking
// ============================================================================

bool InfinitDesktopEffect::isWallpaperWindow(KWin::EffectWindow *w)
{
    if (w->isDesktop()) {
        return true;
    }
    // Wallpaper daemons using wlr-layer-shell on the background layer.
    KWin::Window *window = w->window();
    return window && window->inherits("KWin::LayerShellV1Window") && window->layer() == KWin::DesktopLayer;
}

bool InfinitDesktopEffect::isCanvasCandidate(KWin::EffectWindow *w) const
{
    if (!w || w->isDeleted() || !w->window()) {
        return false;
    }
    if (!w->isManaged() || w->isDesktop() || w->isDock() || w->isSplash() || w->isToolbar() || w->isNotification()
        || w->isCriticalNotification() || w->isOnScreenDisplay() || w->isAppletPopup() || w->isPopupWindow() || w->isMenu()
        || w->isDropdownMenu() || w->isPopupMenu() || w->isTooltip() || w->isComboBox() || w->isDNDIcon() || w->isLockScreen()
        || w->isInputMethod() || w->isOutline() || w->isSpecialWindow()) {
        return false;
    }
    if (!(w->isNormalWindow() || w->isDialog() || w->isUtility())) {
        return false;
    }
    KWin::Window *window = w->window();
    // Layer shell surfaces (wallpaper daemons like waywallen-layer-shell, bars, ...)
    // are reported as "normal" windows unless their namespace says otherwise.
    if (window->inherits("KWin::LayerShellV1Window")) {
        return false;
    }
    const QString resourceClass = window->resourceClass();
    const QString resourceName = window->resourceName();
    const QString desktopFile = window->desktopFileName();
    for (const QString &excluded : m_settings.excludedClasses) {
        const QString pattern = excluded.trimmed();
        if (pattern.isEmpty()) {
            continue;
        }
        if (pattern.compare(resourceClass, Qt::CaseInsensitive) == 0 || pattern.compare(resourceName, Qt::CaseInsensitive) == 0
            || pattern.compare(desktopFile, Qt::CaseInsensitive) == 0) {
            return false;
        }
    }
    return true;
}

void InfinitDesktopEffect::setupWindow(KWin::EffectWindow *w)
{
    using KWin::EffectWindow;
    connect(w, &EffectWindow::windowFrameGeometryChanged, this, &InfinitDesktopEffect::slotFrameGeometryChanged);
    connect(w, &EffectWindow::windowStartUserMovedResized, this, &InfinitDesktopEffect::slotStartUserMovedResized);
    connect(w, &EffectWindow::windowFinishUserMovedResized, this, &InfinitDesktopEffect::slotFinishUserMovedResized);
    connect(w, &EffectWindow::minimizedChanged, this, &InfinitDesktopEffect::slotMinimizedChanged);
    connect(w, &EffectWindow::windowFullScreenChanged, this, &InfinitDesktopEffect::slotFullScreenChanged);
    connect(w, &EffectWindow::windowDesktopsChanged, this, &InfinitDesktopEffect::slotDesktopsChanged);
    connect(w, &EffectWindow::windowDamaged, this, &InfinitDesktopEffect::slotWindowDamaged);
    connect(w, &EffectWindow::windowKeepAboveChanged, this, &InfinitDesktopEffect::slotKeepAboveChanged);
}

void InfinitDesktopEffect::adoptCurrentGeometry(ManagedWindow &window)
{
    if (!window.window || window.closed) {
        return;
    }
    KWin::LogicalOutput *output = window.window->output();
    if (!output) {
        return;
    }
    const QString key = canvasKeyForWindow(window, output);
    Canvas &canvas = ensureCanvas(key, output->name(), key.section(QLatin1Char('|'), 1));
    window.canvasKey = key;
    const Camera reference = canvas.navigation ? canvas.camera : canvas.committed;
    window.canvasPos = reference.screenToCanvas(toQRectF(window.effectWindow->frameGeometry()).topLeft(), outputRect(output));
    window.parked = false;
    if (canvas.navigation && isOperational() && modeOf(window) == WindowMode::Canvas) {
        parkWindow(window, output);
    }
}

void InfinitDesktopEffect::setUserPinned(ManagedWindow &window, bool pinned, bool keepAbove)
{
    if (!window.window) {
        return;
    }
    if (pinned) {
        if (window.parked) {
            // Pinning a window that is only drawn by the plugin: put it where it is shown first.
            if (Canvas *canvas = canvasByKey(window.canvasKey); canvas && !canvas->navigation) {
                placeWindow(window, *canvas, outputOf(*canvas));
            }
        }
        window.userPinned = true;
        if (keepAbove && !window.effectWindow->keepAbove()) {
            window.keepAboveSetByPin = true;
            window.window->setKeepAbove(true);
        }
    } else {
        window.userPinned = false;
        if (window.keepAboveSetByPin) {
            window.keepAboveSetByPin = false;
            window.window->setKeepAbove(false);
        }
        if (!isPinned(window)) {
            adoptCurrentGeometry(window);
        }
    }
    updateActivity();
}

void InfinitDesktopEffect::slotKeepAboveChanged(KWin::EffectWindow *w)
{
    ManagedWindow *managed = record(w);
    if (!managed) {
        return;
    }
    if (!w->keepAbove()) {
        managed->keepAboveSetByPin = false;
    }
    // Leaving the pinned state: continue on the canvas from where it is on screen.
    if (m_settings.pinKeepAboveWindows && !isPinned(*managed)) {
        adoptCurrentGeometry(*managed);
    }
    updateActivity();
}

void InfinitDesktopEffect::adoptWindow(KWin::EffectWindow *w, bool newlyMapped)
{
    if (record(w) || !isCanvasCandidate(w)) {
        return;
    }
    KWin::LogicalOutput *output = w->window()->output();
    if (!output) {
        return;
    }

    auto managed = std::make_unique<ManagedWindow>();
    managed->effectWindow = w;
    managed->window = w->window();
    managed->canvasKey = canvasKeyForWindow(*managed, output);
    managed->appKey = PlacementMemory::applicationKey(w->window()->desktopFileName(), w->window()->resourceClass());
    managed->placementEligible = isPlacementEligible(w);

    KWin::VirtualDesktop *desktop = KWin::effects->currentDesktop(output);
    const auto desktops = w->desktops();
    if (desktops.size() == 1 && !w->isOnAllDesktops()) {
        desktop = desktops.first();
    }
    Canvas &canvas = ensureCanvas(managed->canvasKey, output->name(), (m_settings.perDesktopCanvas && desktop) ? desktop->id() : QStringLiteral("*"));

    // KWin placed the window on the screen as the user sees it right now, i.e.
    // relative to the visual camera (at 100 % zoom if the canvas is zoomed).
    const Camera reference{canvas.camera.center, canvas.navigation ? 1.0 : canvas.camera.zoom};
    managed->canvasPos = reference.screenToCanvas(toQRectF(w->frameGeometry()).topLeft(), outputRect(output));

    ManagedWindow &window = *managed;
    m_windows.emplace(w, std::move(managed));
    updateTileState(window);

    if (!isOperational()) {
        updateActivity();
        return;
    }

    if (newlyMapped && m_settings.rememberWindowPlacement && window.placementEligible && !window.appKey.isEmpty() && modeOf(window) == WindowMode::Canvas) {
        quint64 entry = 0;
        if (const auto placement = m_placements.take(window.appKey, &entry)) {
            window.placementEntry = entry;
            applyPlacement(window, *placement);
            updateActivity();
            return;
        }
    }

    if (newlyMapped && canvas.navigation && isCurrentCanvas(canvas) && modeOf(window) == WindowMode::Canvas) {
        if (m_settings.newWindowsExitZoom) {
            animateTo(canvas, reference);
        } else {
            parkWindow(window, output);
        }
    }
    updateActivity();
}

void InfinitDesktopEffect::forgetWindow(KWin::EffectWindow *w)
{
    if (m_renderer) {
        m_renderer->forget(w);
    }
    if (m_move && m_move->record && m_move->record->effectWindow == w) {
        m_move.reset();
        restoreElectricBorders();
        stopEdgePan();
    }
    m_windows.erase(w);
}

void InfinitDesktopEffect::slotWindowAdded(KWin::EffectWindow *w)
{
    setupWindow(w);
    adoptWindow(w, true);
}

void InfinitDesktopEffect::slotWindowClosed(KWin::EffectWindow *w)
{
    if (ManagedWindow *managed = record(w)) {
        if (isOperational()) {
            rememberPlacement(*managed);
        }
        managed->closed = true;
        managed->closedAt = Clock::now();
        managed->visibleRef.reset();
        if (m_move && m_move->record == managed) {
            m_move.reset();
            restoreElectricBorders();
            stopEdgePan();
        }
    }
    updateActivity();
    repaintClosingWindows();
}

void InfinitDesktopEffect::slotWindowDeleted(KWin::EffectWindow *w)
{
    forgetWindow(w);
    updateActivity();
}

void InfinitDesktopEffect::slotWindowActivated(KWin::EffectWindow *w)
{
    if (!w || !isOperational() || !m_settings.revealOnActivate) {
        return;
    }
    ManagedWindow *managed = record(w);
    if (!managed || modeOf(*managed) != WindowMode::Canvas) {
        return;
    }
    // A click into a window activates it while the button is still held: do not
    // move the canvas under the user's fingers. Task bar and Alt+Tab activation
    // happen without a held button.
    if (KWin::input()->qtButtonStates() != Qt::NoButton) {
        return;
    }
    QPointer<KWin::EffectWindow> guard(w);
    // Activation can come together with a desktop switch; let KWin finish it first.
    QTimer::singleShot(0, this, [this, guard]() {
        if (!guard) {
            return;
        }
        ManagedWindow *managed = record(guard.data());
        if (!managed || modeOf(*managed) != WindowMode::Canvas || KWin::effects->activeWindow() != guard.data()) {
            return;
        }
        Canvas *canvas = canvasByKey(managed->canvasKey);
        if (!canvas || !isCurrentCanvas(*canvas) || canvas->interactions > 0) {
            return;
        }
        KWin::LogicalOutput *output = outputOf(*canvas);
        const Camera base = targetCamera(*canvas);
        const QRectF rect = canvasRect(*managed);
        if (!canvas->navigation && !managed->parked && isFullyVisible(base, rect, outputRect(output), workArea(output))) {
            return;
        }
        animateTo(*canvas, revealCamera(base, rect, outputRect(output), workArea(output), m_settings.revealMode, RevealMargin, 1.0));
    });
}

void InfinitDesktopEffect::slotFrameGeometryChanged(KWin::EffectWindow *w, const KWin::RectF &oldGeometry)
{
    if (m_applyingLayout) {
        return;
    }
    ManagedWindow *managed = record(w);
    if (!managed || managed->closed || !managed->window || !isOperational()) {
        return;
    }
    if (KWin::workspace()->inRearrange()) {
        // Outputs or struts changed: KWin rescues windows into the visible area.
        // Our model stays authoritative, the layout is re-applied afterwards.
        m_rearranged = true;
        scheduleCommitAll();
        return;
    }
    if (modeOf(*managed) != WindowMode::Canvas) {
        return;
    }

    const QRectF geometry = toQRectF(w->frameGeometry());
    const bool moved = geometry.topLeft() != toQRectF(oldGeometry).topLeft();
    if (managed->resizeAnchor) {
        // Resized while zoomed out: the client chose the final size, the anchored corner stays.
        const ManagedWindow::ResizeAnchor &anchor = *managed->resizeAnchor;
        managed->canvasPos = QPointF((anchor.edges & Qt::LeftEdge) ? anchor.corner.x() - geometry.width() : anchor.corner.x(),
                                     (anchor.edges & Qt::TopEdge) ? anchor.corner.y() - geometry.height() : anchor.corner.y());
        if (Canvas *canvas = canvasByKey(managed->canvasKey)) {
            requestRepaint(*canvas);
        }
    }
    if (managed->parked) {
        // The window stays where it is on the canvas and is parked again (with its new size).
        // Parked windows are outside of every screen, so KWin likes to "rescue" them when it
        // configures them (a new size, a new target scale while zoomed out): adopting such a
        // position would make the window jump across the canvas. The user moving it is an
        // interactive move, which is handled on its own.
        QPointer<KWin::EffectWindow> guard(w);
        QTimer::singleShot(0, this, [this, guard]() {
            ManagedWindow *managed = guard ? record(guard.data()) : nullptr;
            Canvas *canvas = managed ? canvasByKey(managed->canvasKey) : nullptr;
            if (managed && managed->parked && canvas && modeOf(*managed) == WindowMode::Canvas) {
                parkWindow(*managed, outputOf(*canvas));
            }
        });
        return;
    } else if (!moved) {
        // Pure size change, the top left corner stays on the canvas.
        updateActivity();
        return;
    }

    KWin::LogicalOutput *output = managed->window->output();
    if (!output) {
        return;
    }
    const QString key = canvasKeyForWindow(*managed, output);
    Canvas &canvas = ensureCanvas(key, output->name(), key.section(QLatin1Char('|'), 1));
    managed->canvasKey = key;
    // Physical positions correspond to the committed camera.
    const Camera &reference = canvas.navigation ? canvas.camera : canvas.committed;
    managed->canvasPos = reference.screenToCanvas(geometry.topLeft(), outputRect(output));
    managed->parked = false;
    if (canvas.navigation) {
        parkWindow(*managed, output);
    }
    updateActivity();
    requestRepaint(canvas);
}

void InfinitDesktopEffect::slotStartUserMovedResized(KWin::EffectWindow *w)
{
    ManagedWindow *managed = record(w);
    if (!managed || !managed->window) {
        return;
    }
    // Pinned windows are moved like on a normal desktop: no screen fling, no edge scrolling.
    if (w->isUserMove() && !isPinned(*managed)) {
        m_move = MoveState{managed->window, managed, managed->window->output(), KWin::effects->cursorPos()};
        if (m_settings.edgePan && isOperational() && !m_paused) {
            suspendElectricBorders();
        }
    }
    updateActivity();
}

void InfinitDesktopEffect::suspendElectricBorders()
{
    if (m_suspendedElectricBorders || !KWin::options) {
        return;
    }
    m_suspendedElectricBorders = ElectricBorders{KWin::options->electricBorderTiling(), KWin::options->electricBorderMaximize()};
    KWin::options->setElectricBorderTiling(false);
    KWin::options->setElectricBorderMaximize(false);
}

void InfinitDesktopEffect::restoreElectricBorders()
{
    if (!m_suspendedElectricBorders || !KWin::options) {
        m_suspendedElectricBorders.reset();
        return;
    }
    KWin::options->setElectricBorderTiling(m_suspendedElectricBorders->tiling);
    KWin::options->setElectricBorderMaximize(m_suspendedElectricBorders->maximize);
    m_suspendedElectricBorders.reset();
}

void InfinitDesktopEffect::slotFinishUserMovedResized(KWin::EffectWindow *w)
{
    const bool wasMove = m_move && m_move->record && m_move->record->effectWindow == w;
    if (wasMove) {
        m_move.reset();
        restoreElectricBorders();
    }
    stopEdgePan();

    ManagedWindow *managed = record(w);
    if (!managed || !managed->window || managed->closed) {
        return;
    }
    if (isPinned(*managed)) {
        // A pinned window stays exactly where it was dropped.
        updateActivity();
        return;
    }
    KWin::LogicalOutput *output = managed->window->output();
    if (!output) {
        return;
    }
    const QString oldKey = managed->canvasKey;
    const QString key = canvasKeyForWindow(*managed, output);
    Canvas &canvas = ensureCanvas(key, output->name(), key.section(QLatin1Char('|'), 1));
    managed->canvasKey = key;
    // The canvas may have scrolled underneath the window (edge scrolling): use the visual camera.
    const Camera reference{canvas.camera.center, canvas.navigation ? 1.0 : canvas.camera.zoom};
    managed->canvasPos = reference.screenToCanvas(toQRectF(w->frameGeometry()).topLeft(), outputRect(output));
    managed->parked = false;

    if (isOperational()) {
        if (canvas.navigation) {
            placeWindow(*managed, canvas, output);
        }
        commitIfDirty(canvas);
        if (oldKey != key) {
            if (Canvas *previous = canvasByKey(oldKey)) {
                commitIfDirty(*previous);
                requestRepaint(*previous);
            }
        }
    }
    updateActivity();
    requestRepaint(canvas);
}

void InfinitDesktopEffect::slotMinimizedChanged(KWin::EffectWindow *w)
{
    ManagedWindow *managed = record(w);
    if (!managed) {
        return;
    }
    updateTileState(*managed);
    if (!w->isMinimized() && isOperational() && modeOf(*managed) == WindowMode::Canvas) {
        // The canvas may have moved while the window was minimized.
        if (Canvas *canvas = canvasByKey(managed->canvasKey); canvas && isCurrentCanvas(*canvas)) {
            placeWindow(*managed, *canvas, outputOf(*canvas));
        }
    }
    updateActivity();
    if (Canvas *canvas = canvasByKey(managed->canvasKey)) {
        requestRepaint(*canvas);
    }
}

void InfinitDesktopEffect::slotFullScreenChanged(KWin::EffectWindow *w)
{
    ManagedWindow *managed = record(w);
    if (!managed || !managed->window) {
        return;
    }
    if (KWin::LogicalOutput *output = managed->window->output()) {
        if (w->isFullScreen()) {
            managed->canvasKey = canvasKeyForWindow(*managed, output);
            if (Canvas *canvas = canvasByKey(managed->canvasKey)) {
                // A fullscreen window covers the screen: stop zooming around below it.
                if (canvas->navigation && w == KWin::effects->activeWindow()) {
                    canvas->motion.stop();
                    setCamera(*canvas, Camera{canvas->camera.center, 1.0});
                    leaveNavigation(*canvas);
                }
            }
        } else {
            // Leaving fullscreen restores the old geometry; adopt it once KWin is done.
            QPointer<KWin::EffectWindow> guard(w);
            QTimer::singleShot(0, this, [this, guard]() {
                ManagedWindow *managed = guard ? record(guard.data()) : nullptr;
                if (!managed || !managed->window || modeOf(*managed) != WindowMode::Canvas) {
                    return;
                }
                KWin::LogicalOutput *output = managed->window->output();
                const QString key = canvasKeyForWindow(*managed, output);
                Canvas &canvas = ensureCanvas(key, output->name(), key.section(QLatin1Char('|'), 1));
                managed->canvasKey = key;
                managed->canvasPos = canvas.committed.screenToCanvas(toQRectF(guard->frameGeometry()).topLeft(), outputRect(output));
                managed->parked = false;
                placeWindow(*managed, canvas, output);
                updateActivity();
            });
        }
    }
    updateTileState(*managed);
    updateActivity();
}

void InfinitDesktopEffect::slotDesktopsChanged(KWin::EffectWindow *w)
{
    ManagedWindow *managed = record(w);
    if (!managed || !managed->window) {
        return;
    }
    KWin::LogicalOutput *output = managed->window->output();
    if (!output) {
        return;
    }
    const QString key = canvasKeyForWindow(*managed, output);
    if (key == managed->canvasKey) {
        updateActivity();
        return;
    }
    // Keep the window at the same place on the screen when it moves to another desktop.
    Canvas *previous = canvasByKey(managed->canvasKey);
    const QPointF screenPos = (managed->parked && previous) ? previous->committed.canvasToScreen(managed->canvasPos, outputRect(output))
                                                            : toQRectF(w->frameGeometry()).topLeft();
    Canvas &canvas = ensureCanvas(key, output->name(), key.section(QLatin1Char('|'), 1));
    managed->canvasKey = key;
    managed->canvasPos = Camera{canvas.camera.center, 1.0}.screenToCanvas(screenPos, outputRect(output));
    if (isOperational() && isCurrentCanvas(canvas)) {
        placeWindow(*managed, canvas, output);
    }
    updateActivity();
}

void InfinitDesktopEffect::slotWindowDamaged(KWin::EffectWindow *w)
{
    ManagedWindow *managed = record(w);
    if (!managed) {
        managed = ownerRecord(w);
    }
    if (!managed || !m_active) {
        return;
    }
    Canvas *canvas = canvasByKey(managed->canvasKey);
    if (!canvas || !isCurrentCanvas(*canvas)) {
        return;
    }
    const WindowMode mode = modeOf(*managed);
    if ((mode == WindowMode::Canvas && (managed->parked || canvas->navigation || isDirty(*canvas))) || mode == WindowMode::Tile) {
        // The damage happened at the physical position (maybe in the void);
        // repaint where the window is actually shown.
        requestRepaint(*canvas);
    }
}

void InfinitDesktopEffect::slotCurrentDesktopChanged()
{
    // Windows on several desktops follow the canvas of the current desktop.
    for (auto &[w, managed] : m_windows) {
        if (!managed->window || managed->closed) {
            continue;
        }
        if (w->isOnAllDesktops() || w->desktops().size() > 1) {
            if (KWin::LogicalOutput *output = managed->window->output()) {
                managed->canvasKey = canvasKeyForWindow(*managed, output);
            }
        }
    }
    QTimer::singleShot(0, this, [this]() {
        commitAll();
        updateActivity();
    });
}

void InfinitDesktopEffect::slotOutputsChanged()
{
    scheduleCommitAll();
}

// ============================================================================
// Physical layout
// ============================================================================

void InfinitDesktopEffect::moveWindowPhysically(KWin::Window *window, const QPointF &topLeft)
{
    if (!window || window->isDeleted() || window->isInteractiveMove() || window->isInteractiveResize()) {
        return;
    }
    const QPointF current = toQRectF(window->frameGeometry()).topLeft();
    if (std::abs(current.x() - topLeft.x()) < 0.01 && std::abs(current.y() - topLeft.y()) < 0.01) {
        return;
    }
    QScopedValueRollback<bool> guard(m_applyingLayout, true);
    window->move(topLeft);
}

void InfinitDesktopEffect::parkWindow(ManagedWindow &window, KWin::LogicalOutput *output)
{
    if (!window.window || !output) {
        return;
    }
    int ownerIndex = -1;
    const QList<QRectF> outputs = allOutputRects(&ownerIndex, output);
    const ParkingSpot spot = computeParkingSpot(outputs, ownerIndex, window.effectWindow->frameGeometry().size());
    window.parked = true;
    moveWindowPhysically(window.window, spot.topLeft);
}

void InfinitDesktopEffect::placeWindow(ManagedWindow &window, Canvas &canvas, KWin::LogicalOutput *output)
{
    if (!output || !window.window || window.closed || modeOf(window) != WindowMode::Canvas) {
        return;
    }
    const QRectF viewport = outputRect(output);
    const QRectF visual = canvas.camera.canvasToScreen(canvasRect(window), viewport);
    if (!canvas.navigation && canvas.camera.isUnitZoom() && visual.intersects(viewport)) {
        const QPointF target = snapToPixelGrid(visual.topLeft(), output->scale());
        // Keep the canvas position exactly in sync with the snapped physical position,
        // otherwise a sub pixel offset would keep the window "transformed" forever.
        window.canvasPos = canvas.camera.screenToCanvas(target, viewport);
        window.parked = false;
        moveWindowPhysically(window.window, target);
    } else {
        parkWindow(window, output);
    }
}

void InfinitDesktopEffect::commit(Canvas &canvas)
{
    if (!isOperational()) {
        canvas.committed = canvas.camera;
        return;
    }
    KWin::LogicalOutput *output = outputOf(canvas);
    if (!output || !isCurrentCanvas(canvas)) {
        canvas.committed = canvas.camera;
        return;
    }
    for (auto &[w, managed] : m_windows) {
        if (managed->canvasKey == canvas.key) {
            placeWindow(*managed, canvas, output);
        }
    }
    canvas.committed = canvas.camera;
    // Pointer focus follows the moved windows immediately.
    KWin::input()->pointer()->update();
    persistSoon();
    updateActivity();
    requestRepaint(canvas);
}

void InfinitDesktopEffect::commitIfDirty(Canvas &canvas)
{
    if (canvas.interactions > 0 || canvas.motion.isActive()) {
        return;
    }
    if (!canvas.camera.fuzzyEquals(canvas.committed)) {
        commit(canvas);
    }
}

void InfinitDesktopEffect::commitAll()
{
    m_commitAllScheduled = false;
    const bool rearranged = std::exchange(m_rearranged, false);
    if (!isOperational()) {
        return;
    }
    for (auto &[w, managed] : m_windows) {
        if (!managed->window || managed->closed) {
            continue;
        }
        Canvas *canvas = canvasByKey(managed->canvasKey);
        if (rearranged && canvas && !canvas->navigation && modeOf(*managed) == WindowMode::Canvas && !managed->parked
            && (managed->window->maximizeMode() != KWin::MaximizeRestore || managed->window->quickTileMode() != KWin::QuickTileMode(KWin::QuickTileFlag::None))) {
            // KWin fitted maximized and tiled windows to the new work area: keep that.
            adoptCurrentGeometry(*managed);
            continue;
        }
        // Windows whose screen disappeared now live where KWin put them.
        if (canvas && outputOf(*canvas)) {
            continue;
        }
        KWin::LogicalOutput *output = managed->window->output();
        if (!output) {
            continue;
        }
        const QString key = canvasKeyForWindow(*managed, output);
        Canvas &target = ensureCanvas(key, output->name(), key.section(QLatin1Char('|'), 1));
        managed->canvasKey = key;
        managed->canvasPos = target.committed.screenToCanvas(toQRectF(w->frameGeometry()).topLeft(), outputRect(output));
        managed->parked = false;
    }
    for (Canvas &canvas : m_canvases) {
        if (isCurrentCanvas(canvas) && canvas.interactions == 0) {
            if (canvas.motion.isActive()) {
                canvas.motion.stop();
            }
            commit(canvas);
        }
    }
}

void InfinitDesktopEffect::scheduleCommitAll()
{
    if (m_commitAllScheduled) {
        return;
    }
    m_commitAllScheduled = true;
    QTimer::singleShot(0, this, &InfinitDesktopEffect::commitAll);
}

void InfinitDesktopEffect::restoreAllWindows()
{
    if (!KWin::workspace()) {
        return;
    }
    // Leave a normal desktop behind: every window that is outside of all screens
    // comes back into the work area of the screen it belonged to.
    QHash<KWin::LogicalOutput *, int> cascade;
    for (auto &[w, managed] : m_windows) {
        KWin::Window *window = managed->window;
        if (!window || managed->closed || window->isDeleted() || w->isFullScreen()) {
            continue;
        }
        const QRectF geometry = toQRectF(window->frameGeometry());
        if (!managed->parked && intersectsAnyOutput(geometry)) {
            continue;
        }
        Canvas *canvas = canvasByKey(managed->canvasKey);
        KWin::LogicalOutput *output = canvas ? outputOf(*canvas) : nullptr;
        if (!output) {
            output = window->output();
        }
        if (!output) {
            continue;
        }
        const QRectF area = workArea(output);
        const int index = cascade[output]++;
        const QPointF offset(40.0 * (index % 10), 40.0 * (index % 10));
        QPointF topLeft = area.topLeft() + QPointF(32, 32) + offset;
        topLeft.setX(std::min(topLeft.x(), area.right() - std::min(geometry.width(), area.width())));
        topLeft.setY(std::min(topLeft.y(), area.bottom() - std::min(geometry.height(), area.height())));
        managed->parked = false;
        moveWindowPhysically(window, topLeft);
    }
}

// ============================================================================
// Camera
// ============================================================================

void InfinitDesktopEffect::noteActivity(Canvas &canvas)
{
    canvas.lastActivity = Clock::now();
}

void InfinitDesktopEffect::beginInteraction(Canvas &canvas)
{
    if (canvas.motion.isActive()) {
        canvas.camera = canvas.motion.sample(Clock::now());
        canvas.motion.stop();
    }
    ++canvas.interactions;
    noteActivity(canvas);
    updateActivity();
}

void InfinitDesktopEffect::endInteraction(Canvas &canvas, const QPointF &screenVelocity)
{
    canvas.interactions = std::max(0, canvas.interactions - 1);
    if (canvas.interactions > 0) {
        return;
    }
    noteActivity(canvas);
    const TimePoint now = Clock::now();
    if (m_settings.momentum && !screenVelocity.isNull()) {
        // Content followed the pointer, so the camera glides the other way.
        if (canvas.motion.startGlide(canvas.camera, -screenVelocity / canvas.camera.zoom, m_settings.friction, now)) {
            updateActivity();
            requestRepaint(canvas);
            return;
        }
    }
    if (m_settings.snapZoomToOne && !canvas.camera.isUnitZoom() && std::abs(canvas.camera.zoom - 1.0) < 0.06) {
        KWin::LogicalOutput *output = outputOf(canvas);
        animateTo(canvas, canvas.camera.zoomedAt(KWin::effects->cursorPos(), 1.0, outputRect(output)), ZoomStepDuration);
        return;
    }
    updateNavigationState(canvas);
    commitIfDirty(canvas);
    updateActivity();
}

void InfinitDesktopEffect::setCamera(Canvas &canvas, const Camera &camera)
{
    if (!std::isfinite(camera.center.x()) || !std::isfinite(camera.center.y()) || !std::isfinite(camera.zoom) || camera.zoom <= 0) {
        qCWarning(KWIN_INFINITDESKTOP) << "refusing invalid camera" << camera.center << camera.zoom;
        return;
    }
    canvas.camera = camera;
    noteActivity(canvas);
    updateNavigationState(canvas);
    updateActivity();
    requestRepaint(canvas);
}

void InfinitDesktopEffect::panBy(Canvas &canvas, const QPointF &screenDelta)
{
    if (canvas.motion.isActive()) {
        canvas.camera = canvas.motion.sample(Clock::now());
        canvas.motion.stop();
    }
    setCamera(canvas, canvas.camera.pannedByScreenDelta(screenDelta));
}

void InfinitDesktopEffect::animateTo(Canvas &canvas, const Camera &camera, std::optional<std::chrono::milliseconds> duration)
{
    const auto length = duration.value_or(m_settings.animationDuration);
    const TimePoint now = Clock::now();
    const Camera from = canvas.motion.isActive() ? canvas.motion.sample(now) : canvas.camera;
    if (length.count() <= 0 || !KWin::effects->animationsSupported()) {
        canvas.motion.stop();
        setCamera(canvas, camera);
        commitIfDirty(canvas);
        return;
    }
    canvas.camera = from;
    canvas.motion.startTransition(from, camera, length, now);
    noteActivity(canvas);
    updateNavigationState(canvas);
    updateActivity();
    requestRepaint(canvas);
}

void InfinitDesktopEffect::stopMotion(Canvas &canvas)
{
    if (!canvas.motion.isActive()) {
        return;
    }
    canvas.camera = canvas.motion.sample(Clock::now());
    canvas.motion.stop();
    requestRepaint(canvas);
}

void InfinitDesktopEffect::setZoom(Canvas &canvas, const QPointF &screenAnchor, qreal zoom, bool animate)
{
    KWin::LogicalOutput *output = outputOf(canvas);
    if (!output) {
        return;
    }
    const Camera base = animate ? targetCamera(canvas) : canvas.camera;
    const qreal newZoom = clampZoom(zoom, m_settings.minZoom, m_settings.maxZoom, m_settings.snapZoomToOne ? 0.02 : 0.0);
    if (std::abs(newZoom - base.zoom) < 1e-6) {
        return;
    }
    const Camera next = base.zoomedAt(screenAnchor, newZoom, outputRect(output));
    if (animate) {
        animateTo(canvas, next, ZoomStepDuration);
    } else {
        canvas.motion.stop();
        setCamera(canvas, next);
    }
    showZoomIndicator(canvas);
}

void InfinitDesktopEffect::zoomBy(Canvas &canvas, const QPointF &screenAnchor, qreal factor, bool animate)
{
    const Camera base = animate ? targetCamera(canvas) : canvas.camera;
    setZoom(canvas, screenAnchor, base.zoom * factor, animate);
}

void InfinitDesktopEffect::enterNavigation(Canvas &canvas)
{
    if (canvas.navigation) {
        return;
    }
    canvas.navigation = true;
    if (!canvas.beforeNavigation) {
        canvas.beforeNavigation = Camera{canvas.committed.center, 1.0};
    }
    // Park everything: input on this screen is handled by the plugin now, and
    // only panels and other non-canvas windows may receive input from KWin.
    KWin::LogicalOutput *output = outputOf(canvas);
    if (output && isCurrentCanvas(canvas) && isOperational()) {
        for (auto &[w, managed] : m_windows) {
            if (managed->canvasKey == canvas.key && modeOf(*managed) == WindowMode::Canvas) {
                parkWindow(*managed, output);
            }
        }
        KWin::input()->pointer()->update();
    }
    qCDebug(KWIN_INFINITDESKTOP) << "entering navigation on" << canvas.key;
}

void InfinitDesktopEffect::leaveNavigation(Canvas &canvas)
{
    if (!canvas.navigation) {
        return;
    }
    canvas.navigation = false;
    canvas.beforeNavigation.reset();
    setResizeCursor({});
    if (m_inputFilter) {
        m_inputFilter->endZoomedInput();
    }
    // At 100 % the windows draw at full resolution again, right away.
    for (auto &[w, managed] : m_windows) {
        if (managed->canvasKey == canvas.key) {
            setContentScale(*managed, 0.0);
        }
    }
    commit(canvas);
    qCDebug(KWIN_INFINITDESKTOP) << "leaving navigation on" << canvas.key;
}

void InfinitDesktopEffect::updateNavigationState(Canvas &canvas)
{
    const bool zoomed = !canvas.camera.isUnitZoom() || !targetCamera(canvas).isUnitZoom();
    if (zoomed && !canvas.navigation) {
        enterNavigation(canvas);
    } else if (!zoomed && canvas.navigation && !canvas.motion.isActive() && canvas.interactions == 0) {
        leaveNavigation(canvas);
    }
}

void InfinitDesktopEffect::advanceAnimations(KWin::LogicalOutput *output, TimePoint now)
{
    for (Canvas &canvas : m_canvases) {
        if (canvas.outputName != output->name()) {
            continue;
        }
        if (canvas.motion.isActive()) {
            canvas.camera = canvas.motion.sample(now);
            canvas.lastActivity = now;
            if (canvas.motion.isFinished(now)) {
                canvas.camera = canvas.motion.target();
                canvas.motion.stop();
                const QString key = canvas.key;
                // Never move windows from inside a paint pass.
                QTimer::singleShot(0, this, [this, key]() {
                    if (Canvas *canvas = canvasByKey(key)) {
                        updateNavigationState(*canvas);
                        commitIfDirty(*canvas);
                        updateActivity();
                    }
                });
            }
        }
        if (!canvas.edgePanVelocity.isNull()) {
            const qreal seconds = std::clamp(std::chrono::duration<qreal>(now - canvas.lastEdgePanTick).count(), 0.0, 0.05);
            canvas.lastEdgePanTick = now;
            canvas.camera.center += canvas.edgePanVelocity * seconds / canvas.camera.zoom;
            canvas.lastActivity = now;
        }
        const bool navigating = canvas.motion.isActive() || canvas.interactions > 0 || canvas.navigation || !canvas.edgePanVelocity.isNull();
        const auto target = [&](EffectSettings::Grid mode, auto linger) {
            switch (mode) {
            case EffectSettings::Grid::Always:
                return 1.0;
            case EffectSettings::Grid::WhileNavigating:
                return navigating || now - canvas.lastActivity < linger ? 1.0 : 0.0;
            case EffectSettings::Grid::Never:
                break;
            }
            return 0.0;
        };
        const qreal step = std::max(std::clamp(std::chrono::duration<qreal>(now - canvas.lastFadeTick).count(), 0.0, 0.05) * 5.0, 0.02);
        canvas.lastFadeTick = now;
        const auto fade = [&](qreal &alpha, qreal targetAlpha) {
            if (alpha == targetAlpha) {
                return;
            }
            alpha = targetAlpha > alpha ? std::min(targetAlpha, alpha + step) : std::max(targetAlpha, alpha - step);
            if (alpha == 0.0) {
                // Faded out: the effect may leave the paint chain again.
                QTimer::singleShot(0, this, &InfinitDesktopEffect::updateActivity);
            }
        };
        fade(canvas.gridAlpha, target(m_settings.grid, ActivityLinger));
        // Markers stay a little longer, so that they can still be clicked after panning.
        fade(canvas.markerAlpha, target(m_settings.offscreenMarkers, MarkerLinger));
    }
}

QList<OffscreenMarker> InfinitDesktopEffect::offscreenMarkers(const Canvas &canvas) const
{
    QList<OffscreenMarker> markers;
    KWin::LogicalOutput *output = outputOf(canvas);
    if (!output || m_settings.offscreenMarkers == EffectSettings::Grid::Never || canvas.markerAlpha <= 0.0) {
        return markers;
    }
    const QRectF viewport = outputRect(output);
    const QRectF area = workArea(output);
    const qreal inset = MarkerSize / 2.0 + MarkerMargin;
    const QRectF inner = area.adjusted(inset, inset, -inset, -inset);
    if (inner.width() < MarkerSize || inner.height() < MarkerSize) {
        return markers;
    }
    const QPointF center = inner.center();
    for (const auto &[w, managed] : m_windows) {
        if (managed->canvasKey != canvas.key || managed->closed || modeOf(*managed) != WindowMode::Canvas) {
            continue;
        }
        const QRectF visual = canvas.camera.canvasToScreen(canvasRect(*managed), viewport);
        if (visual.intersects(area)) {
            continue;
        }
        const QPointF direction = visual.center() - center;
        // Where the line from the middle of the screen to the window leaves the inner rectangle.
        qreal t = std::numeric_limits<qreal>::max();
        if (direction.x() > 0) {
            t = std::min(t, (inner.right() - center.x()) / direction.x());
        } else if (direction.x() < 0) {
            t = std::min(t, (inner.left() - center.x()) / direction.x());
        }
        if (direction.y() > 0) {
            t = std::min(t, (inner.bottom() - center.y()) / direction.y());
        } else if (direction.y() < 0) {
            t = std::min(t, (inner.top() - center.y()) / direction.y());
        }
        if (!std::isfinite(t)) {
            continue;
        }
        const qreal length = std::hypot(direction.x(), direction.y());
        markers.append(OffscreenMarker{managed.get(), center + direction * t, direction / length});
    }
    // Markers of windows in about the same direction would overlap: push them apart along their edge.
    std::sort(markers.begin(), markers.end(), [](const OffscreenMarker &a, const OffscreenMarker &b) {
        return std::atan2(a.direction.y(), a.direction.x()) < std::atan2(b.direction.y(), b.direction.x());
    });
    for (int i = 1; i < markers.size(); ++i) {
        OffscreenMarker &marker = markers[i];
        const bool vertical = std::abs(marker.position.x() - inner.left()) < 0.5 || std::abs(marker.position.x() - inner.right()) < 0.5;
        const QPointF tangent = vertical ? QPointF(0, 1) : QPointF(1, 0);
        for (int attempt = 0; attempt < 12; ++attempt) {
            const bool overlaps = std::any_of(markers.cbegin(), markers.cbegin() + i, [&](const OffscreenMarker &other) {
                return QLineF(other.position, marker.position).length() < MarkerSize + 4.0;
            });
            if (!overlaps) {
                break;
            }
            marker.position += tangent * (MarkerSize + 4.0);
            marker.position.setX(std::clamp(marker.position.x(), inner.left(), inner.right()));
            marker.position.setY(std::clamp(marker.position.y(), inner.top(), inner.bottom()));
        }
    }
    return markers;
}

ManagedWindow *InfinitDesktopEffect::offscreenMarkerAt(const QPointF &pos, KWin::LogicalOutput *output) const
{
    const Canvas *canvas = output ? m_canvases.find(canvasKeyFor(output, KWin::effects->currentDesktop(output))) : nullptr;
    if (!canvas || canvas->markerAlpha < 0.5) {
        return nullptr;
    }
    const QList<OffscreenMarker> markers = offscreenMarkers(*canvas);
    for (const OffscreenMarker &marker : markers) {
        if (QLineF(marker.position, pos).length() <= MarkerSize / 2.0 + 2.0) {
            return marker.window;
        }
    }
    return nullptr;
}

std::optional<QMatrix4x4> InfinitDesktopEffect::zoomedInputTransform(KWin::EffectWindow *w) const
{
    ManagedWindow *owner = record(w);
    if (!owner) {
        owner = ownerRecord(w);
    }
    const Canvas *canvas = owner ? m_canvases.find(owner->canvasKey) : nullptr;
    KWin::LogicalOutput *output = canvas ? outputOf(*canvas) : nullptr;
    if (!output || !canvas->navigation || !w->window()) {
        return std::nullopt;
    }
    const PaintDecision decision = decide(w, output);
    if (decision.kind != PaintDecision::Kind::Transform || decision.scale <= 0.0) {
        return std::nullopt;
    }
    // Drawn at frame + translation with the frame scaled around its top left corner; the
    // surface starts at the buffer geometry (like Window::inputTransformation()).
    const QRectF frame = toQRectF(w->frameGeometry());
    const QRectF buffer = toQRectF(w->window()->bufferGeometry());
    const QPointF visualTopLeft = frame.topLeft() + decision.translation;
    QMatrix4x4 transform;
    transform.translate(frame.x() - buffer.x(), frame.y() - buffer.y());
    transform.scale(1.0 / decision.scale, 1.0 / decision.scale);
    transform.translate(-visualTopLeft.x(), -visualTopLeft.y());
    return transform;
}

std::optional<ZoomedTarget> InfinitDesktopEffect::zoomedTargetAt(const QPointF &pos, KWin::LogicalOutput *output) const
{
    const Canvas *canvas = output ? m_canvases.find(canvasKeyFor(output, KWin::effects->currentDesktop(output))) : nullptr;
    if (!canvas || !canvas->navigation) {
        return std::nullopt;
    }
    const auto stack = KWin::effects->stackingOrder();
    for (auto it = stack.crbegin(); it != stack.crend(); ++it) {
        KWin::EffectWindow *w = *it;
        if (w->isDeleted() || isWallpaperWindow(w) || !w->isVisible() || w->isMinimized() || !w->isOnCurrentDesktop()) {
            continue;
        }
        ManagedWindow *self = record(w);
        ManagedWindow *owner = self ? self : ownerRecord(w);
        const bool onCanvas = owner && owner->canvasKey == canvas->key && modeOf(*owner) == WindowMode::Canvas;
        if (!onCanvas) {
            // Panels, pinned windows, ...: KWin delivers the input to them normally.
            if (toQRectF(w->frameGeometry()).contains(pos)) {
                return std::nullopt;
            }
            continue;
        }
        const PaintDecision decision = decide(w, output);
        if (decision.kind != PaintDecision::Kind::Transform) {
            continue;
        }
        const QRectF frame = toQRectF(w->frameGeometry());
        const QRectF visual(frame.topLeft() + decision.translation, frame.size() * decision.scale);
        if (!visual.contains(pos)) {
            continue;
        }
        const std::optional<QMatrix4x4> transform = zoomedInputTransform(w);
        if (!transform) {
            return std::nullopt;
        }
        ZoomedTarget target;
        target.window = w;
        target.managed = owner;
        target.inputTransform = *transform;
        if (self && w->window()) {
            // Title bar and borders drawn by KWin are not part of the client surface.
            const QPointF frameLocal = (pos - visual.topLeft()) / decision.scale;
            const QRectF client = toQRectF(w->window()->clientGeometry()).translated(-frame.topLeft());
            target.decoration = w->hasDecoration() && !client.contains(frameLocal);
        }
        return target;
    }
    return std::nullopt;
}

void InfinitDesktopEffect::keepMarkersNear(const QPointF &pos, KWin::LogicalOutput *output)
{
    Canvas *canvas = output ? currentCanvas(output) : nullptr;
    if (!canvas || canvas->markerAlpha <= 0.0 || m_settings.offscreenMarkers != EffectSettings::Grid::WhileNavigating) {
        return;
    }
    // Aiming at a marker: it must not fade away under the pointer.
    const QList<OffscreenMarker> markers = offscreenMarkers(*canvas);
    for (const OffscreenMarker &marker : markers) {
        if (QLineF(marker.position, pos).length() < MarkerSize * 2.0) {
            canvas->lastActivity = Clock::now();
            return;
        }
    }
}

void InfinitDesktopEffect::flyToWindow(ManagedWindow &window)
{
    if (Canvas *canvas = canvasByKey(window.canvasKey)) {
        noteActivity(*canvas);
    }
    // Keep the zoom: from the overview, the overview of the other place.
    activateAndReveal(window, false);
}

void InfinitDesktopEffect::setEdgePan(KWin::LogicalOutput *output, const QPointF &screenVelocity)
{
    for (Canvas &canvas : m_canvases) {
        const bool target = output && canvas.outputName == output->name() && isCurrentCanvas(canvas);
        const QPointF velocity = target ? screenVelocity : QPointF();
        if (canvas.edgePanVelocity.isNull() && !velocity.isNull()) {
            canvas.lastEdgePanTick = Clock::now();
        }
        if (canvas.edgePanVelocity != velocity) {
            canvas.edgePanVelocity = velocity;
            requestRepaint(canvas);
        }
    }
    updateActivity();
}

void InfinitDesktopEffect::stopEdgePan()
{
    setEdgePan(nullptr, QPointF());
}

// ============================================================================
// Window operations used by input handling
// ============================================================================

void InfinitDesktopEffect::moveWindowOnCanvas(ManagedWindow &window, Canvas &canvas, const QPointF &canvasPos)
{
    if (window.canvasKey != canvas.key) {
        transferWindow(window, canvas, canvasPos);
        return;
    }
    window.canvasPos = canvasPos;
    noteActivity(canvas);
    updateActivity();
    requestRepaint(canvas);
}

Qt::Edges InfinitDesktopEffect::resizeEdgesAt(const ManagedWindow &window, const Canvas &canvas, const QPointF &pos) const
{
    const KWin::Window *client = window.window;
    if (!client || !client->isResizable() || client->maximizeMode() != KWin::MaximizeRestore) {
        return {};
    }
    const QRectF visual = canvas.camera.canvasToScreen(canvasRect(window), outputRect(outputOf(canvas)));
    // Small windows keep a middle part that can be dragged.
    const qreal grab = std::min({ResizeGrab, visual.width() / 4.0, visual.height() / 4.0});
    if (!visual.contains(pos) || grab < 2.0) {
        return {};
    }
    Qt::Edges edges;
    if (pos.x() < visual.left() + grab) {
        edges |= Qt::LeftEdge;
    } else if (pos.x() >= visual.right() - grab) {
        edges |= Qt::RightEdge;
    }
    if (pos.y() < visual.top() + grab) {
        edges |= Qt::TopEdge;
    } else if (pos.y() >= visual.bottom() - grab) {
        edges |= Qt::BottomEdge;
    }
    return edges;
}

void InfinitDesktopEffect::resizeWindowOnCanvas(ManagedWindow &window, Canvas &canvas, Qt::Edges edges, const QRectF &start, const QPointF &delta)
{
    KWin::Window *client = window.window;
    if (!client || !client->isResizable()) {
        return;
    }
    QRectF rect = start;
    if (edges & Qt::LeftEdge) {
        rect.setLeft(start.left() + delta.x());
    } else if (edges & Qt::RightEdge) {
        rect.setRight(start.right() + delta.x());
    }
    if (edges & Qt::TopEdge) {
        rect.setTop(start.top() + delta.y());
    } else if (edges & Qt::BottomEdge) {
        rect.setBottom(start.bottom() + delta.y());
    }
    const QSizeF minimum = client->minSize().expandedTo(QSizeF(MinimumResizeSize, MinimumResizeSize));
    const QSizeF size = rect.size().expandedTo(minimum).boundedTo(client->maxSize().expandedTo(minimum));
    // The corner opposite of the dragged edges stays where it is.
    const QPointF corner((edges & Qt::LeftEdge) ? start.right() : start.left(), (edges & Qt::TopEdge) ? start.bottom() : start.top());
    window.resizeAnchor = ManagedWindow::ResizeAnchor{edges, corner};
    window.canvasPos = QPointF((edges & Qt::LeftEdge) ? corner.x() - size.width() : corner.x(), (edges & Qt::TopEdge) ? corner.y() - size.height() : corner.y());
    // Wayland clients answer asynchronously; slotFrameGeometryChanged keeps the corner then.
    client->resize(size);
    noteActivity(canvas);
    requestRepaint(canvas);
}

void InfinitDesktopEffect::endCanvasResize(ManagedWindow &window)
{
    // The client may still answer the last request: keep the anchor a little longer.
    QPointer<KWin::EffectWindow> guard(window.effectWindow);
    QTimer::singleShot(1500ms, this, [this, guard]() {
        if (ManagedWindow *managed = guard ? record(guard.data()) : nullptr) {
            managed->resizeAnchor.reset();
        }
    });
}

void InfinitDesktopEffect::setResizeCursor(Qt::Edges edges)
{
    if (edges == m_resizeCursorEdges) {
        return;
    }
    if (m_resizeCursorEdges) {
        KWin::effects->stopMouseInterception(this);
    }
    m_resizeCursorEdges = edges;
    if (!edges) {
        return;
    }
    Qt::CursorShape shape = Qt::SizeAllCursor;
    const bool horizontal = edges & (Qt::LeftEdge | Qt::RightEdge);
    const bool vertical = edges & (Qt::TopEdge | Qt::BottomEdge);
    if (horizontal && vertical) {
        // Top left and bottom right share one diagonal.
        shape = bool(edges & Qt::LeftEdge) == bool(edges & Qt::TopEdge) ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor;
    } else if (horizontal) {
        shape = Qt::SizeHorCursor;
    } else {
        shape = Qt::SizeVerCursor;
    }
    // The effect cursor is only shown while the effect intercepts the mouse; the input
    // filter still sees every event first.
    KWin::effects->startMouseInterception(this, shape);
}

void InfinitDesktopEffect::moveTileOnCanvas(ManagedWindow &window, Canvas &canvas, const QPointF &canvasPos)
{
    if (window.canvasKey != canvas.key) {
        if (Canvas *previous = canvasByKey(window.canvasKey)) {
            requestRepaint(*previous);
        }
        window.canvasKey = canvas.key;
    }
    window.tilePos = canvasPos;
    noteActivity(canvas);
    updateActivity();
    requestRepaint(canvas);
}

void InfinitDesktopEffect::transferWindow(ManagedWindow &window, Canvas &to, const QPointF &canvasPos)
{
    Canvas *from = canvasByKey(window.canvasKey);
    window.canvasKey = to.key;
    window.canvasPos = canvasPos;
    KWin::LogicalOutput *output = outputOf(to);
    if (output && window.window && window.window->output() != output) {
        // Let KWin know about the new screen (rules, maximize area, task manager filters).
        parkWindow(window, output);
    }
    if (from) {
        requestRepaint(*from);
    }
    if (output && !to.navigation) {
        placeWindow(window, to, output);
    }
    updateActivity();
    requestRepaint(to);
}

void InfinitDesktopEffect::activateAndReveal(ManagedWindow &window, bool returnToUnitZoom)
{
    Canvas *canvas = canvasByKey(window.canvasKey);
    KWin::LogicalOutput *output = canvas ? outputOf(*canvas) : nullptr;
    if (!canvas || !output) {
        return;
    }
    const Camera base = targetCamera(*canvas);
    const qreal zoom = returnToUnitZoom ? 1.0 : base.zoom;
    const QRectF rect = canvasRect(window);
    const Camera target = revealCamera(base, rect, outputRect(output), workArea(output), m_settings.revealMode, RevealMargin, zoom);
    if (!(base.isUnitZoom() && !window.parked && isFullyVisible(base, rect, outputRect(output), workArea(output)))) {
        animateTo(*canvas, target);
    }
    KWin::effects->activateWindow(window.effectWindow);
}

void InfinitDesktopEffect::restoreTile(ManagedWindow &window)
{
    Canvas *canvas = canvasByKey(window.canvasKey);
    KWin::LogicalOutput *output = canvas ? outputOf(*canvas) : nullptr;
    if (output && window.window && window.window->output() != output) {
        // The tile was dragged onto another screen: become fullscreen there.
        window.window->sendToOutput(output);
    }
    window.effectWindow->unminimize();
    KWin::effects->activateWindow(window.effectWindow);
}

bool InfinitDesktopEffect::resolveForeignOverlap(const QPointF &pos, KWin::LogicalOutput *output)
{
    KWin::Window *top = KWin::input()->findToplevel(pos);
    ManagedWindow *managed = record(top);
    if (!managed || modeOf(*managed) != WindowMode::Canvas || managed->parked) {
        return false;
    }
    Canvas *canvas = canvasByKey(managed->canvasKey);
    KWin::LogicalOutput *owner = canvas ? outputOf(*canvas) : nullptr;
    if (!owner || owner == output) {
        return false;
    }
    // The part of the window that reaches into this screen is invisible here;
    // park it so it cannot steal the input. It stays visible on its own screen.
    parkWindow(*managed, owner);
    KWin::input()->pointer()->update();
    updateActivity();
    requestRepaint(*canvas);
    return true;
}

CanvasHit InfinitDesktopEffect::hitTest(const QPointF &pos, KWin::LogicalOutput *output) const
{
    CanvasHit result;
    if (!output) {
        return result;
    }
    const Canvas *canvas = m_canvases.find(canvasKeyFor(output, KWin::effects->currentDesktop(output)));
    const QRectF viewport = outputRect(output);

    const auto stack = KWin::effects->stackingOrder();
    for (auto it = stack.crbegin(); it != stack.crend(); ++it) {
        KWin::EffectWindow *w = *it;
        // The wallpaper is the empty canvas: it never moves and never takes canvas gestures.
        if (w->isDeleted() || isWallpaperWindow(w)) {
            continue;
        }
        if (ManagedWindow *managed = record(w)) {
            const WindowMode mode = modeOf(*managed);
            if (mode == WindowMode::Canvas && canvas && managed->canvasKey == canvas->key) {
                if (canvas->camera.canvasToScreen(canvasRect(*managed), viewport).contains(pos)) {
                    const bool drawnElsewhere = managed->parked || canvas->navigation || isDirty(*canvas);
                    return CanvasHit{drawnElsewhere ? CanvasHit::Kind::ParkedWindow : CanvasHit::Kind::Window, managed};
                }
                continue;
            }
            if ((mode == WindowMode::Fullscreen || mode == WindowMode::Pinned || mode == WindowMode::Moving) && toQRectF(w->frameGeometry()).contains(pos)) {
                return CanvasHit{CanvasHit::Kind::Foreign, managed};
            }
            continue;
        }
        if (!w->isVisible() || w->isMinimized() || !w->isOnCurrentDesktop()) {
            continue;
        }
        if (ManagedWindow *owner = ownerRecord(w)) {
            // Popup of a canvas window; only interactive when its parent is.
            if (!owner->parked && canvas && !canvas->navigation && toQRectF(w->frameGeometry()).contains(pos)) {
                return CanvasHit{CanvasHit::Kind::Foreign, nullptr};
            }
            continue;
        }
        if (toQRectF(w->frameGeometry()).contains(pos)) {
            return CanvasHit{CanvasHit::Kind::Foreign, nullptr};
        }
    }

    if (canvas) {
        for (const auto &[w, managed] : m_windows) {
            if (managed->canvasKey == canvas->key && managed->tilePos && modeOf(*managed) == WindowMode::Tile
                && canvas->camera.canvasToScreen(tileCanvasRect(*managed, *canvas), viewport).contains(pos)) {
                return CanvasHit{CanvasHit::Kind::Tile, managed.get()};
            }
        }
    }
    return result;
}

void InfinitDesktopEffect::updateTileState(ManagedWindow &window)
{
    const bool tile = !window.closed && modeOf(window) == WindowMode::Tile;
    if (tile && !window.tilePos) {
        if (Canvas *canvas = canvasByKey(window.canvasKey)) {
            QList<QRectF> occupied;
            for (const auto &[w, other] : m_windows) {
                if (other.get() != &window && other->canvasKey == canvas->key && !other->closed) {
                    // Tiles appear in free space: neither on other tiles nor under windows.
                    const WindowMode otherMode = modeOf(*other);
                    if (other->tilePos && otherMode == WindowMode::Tile) {
                        occupied.append(tileCanvasRect(*other, *canvas));
                    } else if (otherMode == WindowMode::Canvas) {
                        occupied.append(canvasRect(*other));
                    }
                }
            }
            QRectF rect = tileCanvasRect(window, *canvas);
            window.tilePos = findFreeTileSpot(canvas->camera.center, rect.size(), occupied, 32);
        }
    }
    if (tile && m_settings.tileLiveContent) {
        if (!window.visibleRef) {
            window.visibleRef = std::make_unique<KWin::EffectWindowVisibleRef>(window.effectWindow, KWin::EffectWindow::PAINT_DISABLED_BY_MINIMIZE);
        }
    } else {
        window.visibleRef.reset();
    }
}

// ============================================================================
// Painting
// ============================================================================

InfinitDesktopEffect::PaintDecision InfinitDesktopEffect::decide(KWin::EffectWindow *w, KWin::LogicalOutput *output) const
{
    ManagedWindow *self = record(w);
    ManagedWindow *owner = self ? self : ownerRecord(w);
    if (!owner) {
        return {};
    }
    const WindowMode mode = modeOf(*owner);
    if (mode != WindowMode::Canvas && mode != WindowMode::Tile) {
        return {};
    }
    const Canvas *canvasPtr = m_canvases.find(owner->canvasKey);
    if (!canvasPtr) {
        return {};
    }
    const Canvas &canvas = *canvasPtr;
    KWin::LogicalOutput *ownerOutput = outputOf(canvas);
    if (!ownerOutput || !isCurrentCanvas(canvas)) {
        return {};
    }
    if (output != ownerOutput) {
        // Every screen shows only its own canvas: clip what reaches over the edge.
        return {PaintDecision::Kind::Skip, 1.0, QPointF()};
    }
    const QRectF viewport = outputRect(output);

    if (mode == WindowMode::Tile) {
        if (self != owner || !owner->tilePos || !m_settings.tileLiveContent) {
            return {PaintDecision::Kind::Skip, 1.0, QPointF()};
        }
        const QRectF screen = canvas.camera.canvasToScreen(tileCanvasRect(*owner, canvas), viewport);
        const QRectF frame = toQRectF(w->frameGeometry());
        if (frame.width() <= 0) {
            return {PaintDecision::Kind::Skip, 1.0, QPointF()};
        }
        return {PaintDecision::Kind::Transform, screen.width() / frame.width(), screen.topLeft() - frame.topLeft()};
    }

    const qreal zoom = canvas.camera.zoom;
    const QPointF visualTopLeft = canvas.camera.canvasToScreen(owner->canvasPos, viewport);
    const QPointF ownerPhysical = toQRectF(owner->effectWindow->frameGeometry()).topLeft();
    QPointF translation;
    if (self) {
        translation = visualTopLeft - ownerPhysical;
    } else {
        const QPointF childPhysical = toQRectF(w->frameGeometry()).topLeft();
        const QPointF visual = visualPopupPosition(w->window(), *owner, canvas, output).value_or(visualTopLeft + (childPhysical - ownerPhysical) * zoom);
        translation = visual - childPhysical;
    }
    if (std::abs(zoom - 1.0) < 1e-6 && std::abs(translation.x()) < 0.01 && std::abs(translation.y()) < 0.01) {
        return {};
    }
    return {PaintDecision::Kind::Transform, zoom, translation};
}

std::optional<QPointF> InfinitDesktopEffect::visualPopupPosition(KWin::Window *popup, const ManagedWindow &owner, const Canvas &canvas, KWin::LogicalOutput *output, int depth) const
{
    KWin::Window *parent = popup ? popup->transientFor() : nullptr;
    const auto it = popup ? m_xdgPopups.constFind(popup->surface()) : m_xdgPopups.cend();
    if (!parent || depth > 16 || it == m_xdgPopups.cend() || !it.value()) {
        return std::nullopt;
    }
    const KWin::XdgPositioner positioner = it.value()->positioner();
    if (!positioner.isComplete()) {
        return std::nullopt;
    }
    const qreal zoom = canvas.camera.zoom;
    const QPointF ownerVisual = canvas.camera.canvasToScreen(owner.canvasPos, outputRect(output));
    QPointF parentVisual;
    if (parent == owner.window.data()) {
        parentVisual = ownerVisual;
    } else if (const std::optional<QPointF> position = visualPopupPosition(parent, owner, canvas, output, depth + 1)) {
        parentVisual = *position;
    } else {
        const QPointF ownerPhysical = toQRectF(owner.effectWindow->frameGeometry()).topLeft();
        parentVisual = ownerVisual + (toQRectF(parent->frameGeometry()).topLeft() - ownerPhysical) * zoom;
    }
    // KWin keeps a popup inside the work area around the parent's real position, which is
    // far away from where a parked parent is drawn (menus would open off to the side). Do
    // the same as XdgPopupWindow::updateRelativePlacement() with the drawn parent: the
    // positioner works in the parent's surface coordinates, which exclude the decoration.
    const QPointF clientOffset = toQRectF(parent->clientGeometry()).topLeft() - toQRectF(parent->frameGeometry()).topLeft();
    const QPointF parentClient = parentVisual + clientOffset * zoom;
    const QRectF area = toQRectF(KWin::effects->clientArea(KWin::PlacementArea, output));
    const QRectF bounds((area.topLeft() - parentClient) / zoom, area.size() / zoom);
    const QRectF placement = toQRectF(positioner.placement(bounds));
    return parentClient + placement.topLeft() * zoom;
}

void InfinitDesktopEffect::prePaintScreen(KWin::ScreenPrePaintData &data)
{
    m_paintOutput = data.screen;
    if (m_paintOutput && isOperational()) {
        advanceAnimations(m_paintOutput, Clock::now());
        // A view may cover several screens (workspace screenshots and casts render
        // everything in one pass), so look at every window on its own screen.
        bool transformed = false;
        const auto stack = KWin::effects->stackingOrder();
        for (KWin::EffectWindow *w : stack) {
            KWin::LogicalOutput *owner = ownerOutputOf(w);
            if (owner && decide(w, owner).kind == PaintDecision::Kind::Transform) {
                transformed = true;
                break;
            }
        }
        if (transformed) {
            data.mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS;
        }
    }
    KWin::effects->prePaintScreen(data);
}

KWin::LogicalOutput *InfinitDesktopEffect::outputForViewport(const KWin::RenderViewport &viewport) const
{
    const QRectF rect = toQRectF(viewport.renderRect());
    const auto outputs = KWin::workspace()->outputs();
    for (KWin::LogicalOutput *output : outputs) {
        const QRectF geometry = outputRect(output);
        if (std::abs(geometry.x() - rect.x()) < 0.5 && std::abs(geometry.y() - rect.y()) < 0.5
            && std::abs(geometry.width() - rect.width()) < 0.5 && std::abs(geometry.height() - rect.height()) < 0.5) {
            return output;
        }
    }
    return nullptr;
}

KWin::LogicalOutput *InfinitDesktopEffect::ownerOutputOf(KWin::EffectWindow *w) const
{
    ManagedWindow *owner = record(w);
    if (!owner) {
        owner = ownerRecord(w);
    }
    const Canvas *canvas = owner ? m_canvases.find(owner->canvasKey) : nullptr;
    return canvas ? outputOf(*canvas) : nullptr;
}

bool InfinitDesktopEffect::paintScreen(const KWin::RenderTarget &renderTarget, const KWin::RenderViewport &viewport, int mask, const KWin::Region &deviceRegion, KWin::LogicalOutput *screen)
{
    m_paintOutput = screen;
    m_paintRegion = deviceRegion;
    m_canvasLayerPainted = false;
    const bool result = KWin::effects->paintScreen(renderTarget, viewport, mask, deviceRegion, screen);
    KWin::LogicalOutput *viewOutput = outputForViewport(viewport);
    if (result && viewOutput && isOperational() && KWin::effects->isOpenGLCompositing()) {
        Canvas *canvas = currentCanvas(viewOutput);
        if (!m_canvasLayerPainted) {
            // No window at all on this screen: the canvas layer still has to appear.
            m_canvasLayerPainted = true;
            if (canvas) {
                m_renderer->paintCanvasLayer(*this, *canvas, renderTarget, viewport, viewOutput, deviceRegion);
            }
        }
        // Markers of windows outside the view go on top of everything.
        if (canvas && canvas->markerAlpha > 0.0) {
            const QList<OffscreenMarker> markers = offscreenMarkers(*canvas);
            if (!markers.isEmpty()) {
                m_renderer->paintMarkers(*canvas, markers, renderTarget, viewport, deviceRegion);
            }
        }
    }
    return result;
}

void InfinitDesktopEffect::postPaintScreen()
{
    for (const Canvas &canvas : std::as_const(m_canvases)) {
        const auto fadingOrLingering = [](qreal alpha, EffectSettings::Grid mode) {
            return (alpha > 0.0 && alpha < 1.0) || (mode == EffectSettings::Grid::WhileNavigating && alpha > 0.0);
        };
        const bool fading = fadingOrLingering(canvas.gridAlpha, m_settings.grid) || fadingOrLingering(canvas.markerAlpha, m_settings.offscreenMarkers);
        if (canvas.motion.isActive() || !canvas.edgePanVelocity.isNull() || fading) {
            requestRepaint(canvas);
        }
    }
    repaintClosingWindows();
    KWin::effects->postPaintScreen();
}

void InfinitDesktopEffect::prePaintWindow(KWin::RenderView *view, KWin::EffectWindow *w, KWin::WindowPrePaintData &data)
{
    if (m_paintOutput && isOperational()) {
        switch (decide(w, m_paintOutput).kind) {
        case PaintDecision::Kind::Transform:
            data.setTransformed();
            break;
        case PaintDecision::Kind::Skip:
            // Not drawn on this screen: it must not occlude what is below it.
            data.setTranslucent();
            break;
        case PaintDecision::Kind::Default:
            break;
        }
    }
    KWin::effects->prePaintWindow(view, w, data);
}

bool InfinitDesktopEffect::paintWindow(const KWin::RenderTarget &renderTarget, const KWin::RenderViewport &viewport, KWin::EffectWindow *w, int mask, const KWin::Region &deviceRegion, KWin::WindowPaintData &data)
{
    if (!isOperational()) {
        return KWin::effects->paintWindow(renderTarget, viewport, w, mask, deviceRegion, data);
    }
    // Normally every screen is rendered on its own. Workspace screenshots and casts
    // render several screens in one pass: then every window is drawn as on its own
    // screen and clipped to that screen.
    KWin::LogicalOutput *viewOutput = outputForViewport(viewport);
    KWin::LogicalOutput *decisionOutput = viewOutput;
    KWin::Region region = deviceRegion;
    int paintMask = mask;
    if (!viewOutput) {
        decisionOutput = ownerOutputOf(w);
        if (decisionOutput) {
            region = region & KWin::Region(viewport.mapToDeviceCoordinatesAligned(KWin::RectF(outputRect(decisionOutput))));
            if (region.isEmpty()) {
                return true;
            }
            // Makes the renderer clip with the scissor test.
            paintMask |= PAINT_WINDOW_TRANSFORMED;
        }
    }
    if (decisionOutput) {
        const PaintDecision decision = decide(w, decisionOutput);
        if (decision.kind == PaintDecision::Kind::Skip) {
            return true;
        }
        if (decision.kind == PaintDecision::Kind::Transform) {
            data *= QVector2D(decision.scale, decision.scale);
            data += decision.translation;
            // With an infinite region KWin clips the window's quads in software against the
            // screen, using only the translation of the window transform: a zoomed out window
            // would be cut off where its unscaled size leaves the screen. A finite region makes
            // KWin clip with the scissor test instead, which respects the scale.
            if (region == KWin::Region::infinite()) {
                region = KWin::Region(viewport.deviceRect());
            }
            paintMask |= PAINT_WINDOW_TRANSFORMED;
        }
    }

    // The canvas layer (dot grid, icon tiles) belongs right above the wallpaper and
    // below every window. Without a wallpaper window (no plasmashell, layer shell
    // wallpaper daemons) it is painted below the first real window instead.
    const bool layerPending = !m_canvasLayerPainted && viewOutput && KWin::effects->isOpenGLCompositing();
    const bool wallpaper = isWallpaperWindow(w);
    const auto paintLayer = [&]() {
        m_canvasLayerPainted = true;
        if (Canvas *canvas = currentCanvas(viewOutput)) {
            m_renderer->paintCanvasLayer(*this, *canvas, renderTarget, viewport, viewOutput, m_paintRegion);
        }
    };
    if (layerPending && !wallpaper) {
        paintLayer();
    }
    if (!KWin::effects->paintWindow(renderTarget, viewport, w, paintMask, region, data)) {
        return false;
    }
    if (layerPending && wallpaper && outputRect(viewOutput).intersects(toQRectF(w->frameGeometry()))) {
        paintLayer();
    }
    return true;
}

bool InfinitDesktopEffect::isActive() const
{
    return m_active;
}

int InfinitDesktopEffect::requestedEffectChainPosition() const
{
    // Early in the chain: our transformation has to be in place before effects
    // such as blur, rounded corners or wobbly windows look at the paint data.
    return 5;
}

bool InfinitDesktopEffect::blocksDirectScanout() const
{
    // Transformed windows are marked with an ItemEffect, so KWin keeps them
    // composited on its own; everything else (games!) may use direct scanout.
    return false;
}

void InfinitDesktopEffect::updateItemEffect(ManagedWindow &window, bool transformed)
{
    if (transformed && !window.itemEffect && window.effectWindow->windowItem()) {
        window.itemEffect = std::make_unique<KWin::ItemEffect>(window.effectWindow->windowItem());
    } else if (!transformed && window.itemEffect) {
        window.itemEffect.reset();
    }
}

void InfinitDesktopEffect::setContentScale(ManagedWindow &window, qreal scale)
{
    KWin::Window *client = window.window;
    if (!client || window.contentScale == scale) {
        return;
    }
    if (scale > 0.0) {
        if (window.contentScale == 0.0) {
            window.sizeBeforeContentScale = toQRectF(client->frameGeometry()).size();
        }
        client->setNextTargetScale(scale);
    } else {
        Canvas *canvas = canvasByKey(window.canvasKey);
        KWin::LogicalOutput *output = canvas ? outputOf(*canvas) : client->output();
        client->setNextTargetScale(output ? output->scale() : 1.0);
        // The size was rounded to the pixels of the smaller scale: give back the exact size.
        const QSizeF size = toQRectF(client->frameGeometry()).size();
        const QSizeF before = window.sizeBeforeContentScale;
        if (before.isValid() && size != before && client->maximizeMode() == KWin::MaximizeRestore
            && std::abs(size.width() - before.width()) <= 3.0 && std::abs(size.height() - before.height()) <= 3.0) {
            client->resize(before);
        }
        window.sizeBeforeContentScale = QSizeF();
    }
    window.contentScale = scale;
}

void InfinitDesktopEffect::applyContentScales()
{
    for (const Canvas &canvas : std::as_const(m_canvases)) {
        if (canvas.navigation && (canvas.motion.isActive() || canvas.interactions > 0)) {
            // Still zooming: redrawing for every intermediate size would only cost time.
            m_contentScaleTimer.start();
            return;
        }
    }
    for (auto &[w, managed] : m_windows) {
        qreal scale = 0.0;
        const Canvas *canvas = m_canvases.find(managed->canvasKey);
        KWin::LogicalOutput *output = canvas ? outputOf(*canvas) : nullptr;
        // Only xdg toplevels draw for a target scale; X11 windows would just get a blurry decoration.
        if (m_settings.sharpZoomedContent && isOperational() && output && canvas->navigation && isCurrentCanvas(*canvas) && !managed->closed
            && managed->window && managed->window->inherits("KWin::XdgToplevelWindow") && modeOf(*managed) == WindowMode::Canvas
            && canvas->camera.zoom < 0.97) {
            // Steps of 1/24 keep small zoom changes from causing redraws.
            const qreal wanted = output->scale() * std::max(canvas->camera.zoom, MinimumContentScale);
            scale = std::max(std::round(wanted * 24.0) / 24.0, 1.0 / 24.0);
        }
        setContentScale(*managed, scale);
    }
}

void InfinitDesktopEffect::updateActivity()
{
    // Content scales follow the zoom once it rests.
    m_contentScaleTimer.start();
    bool active = false;
    bool overlap = false;
    if (isOperational()) {
        for (Canvas &canvas : m_canvases) {
            if (!isCurrentCanvas(canvas)) {
                continue;
            }
            if (isDirty(canvas) || canvas.navigation || canvas.gridAlpha > 0.0 || m_settings.grid == EffectSettings::Grid::Always
                || canvas.markerAlpha > 0.0 || m_settings.offscreenMarkers == EffectSettings::Grid::Always
                || !canvas.edgePanVelocity.isNull() || canvas.interactions > 0) {
                active = true;
            }
        }
        for (auto &[w, managed] : m_windows) {
            const WindowMode mode = modeOf(*managed);
            bool transformed = false;
            if (mode == WindowMode::Tile) {
                transformed = true;
            } else if (mode == WindowMode::Canvas) {
                Canvas *canvas = canvasByKey(managed->canvasKey);
                transformed = canvas && isCurrentCanvas(*canvas) && (managed->parked || canvas->navigation || isDirty(*canvas));
                if (!transformed && canvas && managed->window) {
                    // Straddling into another screen: needs clipping there.
                    KWin::LogicalOutput *owner = outputOf(*canvas);
                    int ownerIndex = -1;
                    const QList<QRectF> outputs = allOutputRects(&ownerIndex, owner);
                    if (intersectsOtherOutput(outputs, ownerIndex, toQRectF(managed->window->frameGeometry()))) {
                        active = true;
                        overlap = true;
                    }
                }
            }
            updateItemEffect(*managed, transformed);
            active = active || transformed;
        }
    } else {
        for (auto &[w, managed] : m_windows) {
            updateItemEffect(*managed, false);
        }
    }
    m_foreignOverlap = overlap;
    if (m_active != active) {
        m_active = active;
        KWin::effects->addRepaintFull();
    }
}

void InfinitDesktopEffect::requestRepaint(KWin::LogicalOutput *output)
{
    if (output) {
        KWin::effects->addRepaint(output->geometryF());
    }
}

void InfinitDesktopEffect::requestRepaint(const Canvas &canvas)
{
    requestRepaint(outputOf(canvas));
}

void InfinitDesktopEffect::repaintClosingWindows()
{
    // Other effects animate closing windows (fade, scale, ...) and keep them alive until
    // the animation has run. They repaint where the window physically is, which for a
    // parked window is outside of every screen: nothing would be painted, the animation
    // would never advance and the window would never be released. Repaint where it is
    // drawn instead; off screen, a single pixel still lets the animation run to its end.
    const TimePoint now = Clock::now();
    for (const auto &[w, managed] : m_windows) {
        if (!managed->closed || !managed->parked || now - managed->closedAt > ClosingAnimationLimit) {
            continue;
        }
        const Canvas *canvas = m_canvases.find(managed->canvasKey);
        KWin::LogicalOutput *output = canvas ? outputOf(*canvas) : nullptr;
        if (!output) {
            continue;
        }
        const QRectF viewport = outputRect(output);
        const QRectF frame = toQRectF(w->frameGeometry());
        const QRectF expanded = toQRectF(w->expandedGeometry());
        const qreal zoom = canvas->camera.zoom;
        const QPointF topLeft = canvas->camera.canvasToScreen(managed->canvasPos, viewport) + (expanded.topLeft() - frame.topLeft()) * zoom;
        const QRectF visual = QRectF(topLeft, expanded.size() * zoom).intersected(viewport);
        KWin::effects->addRepaint(KWin::RectF(visual.isEmpty() ? QRectF(viewport.topLeft(), QSizeF(1, 1)) : visual));
    }
}

// ============================================================================
// Feedback, persistence, debugging
// ============================================================================

void InfinitDesktopEffect::showZoomIndicator(const Canvas &canvas)
{
    if (!m_settings.zoomIndicator) {
        return;
    }
    const int percent = int(std::round(targetCamera(canvas).zoom * 100.0));
    showMessage(i18nc("zoom level shown while zooming", "%1 %", percent), QStringLiteral("zoom-in"));
    m_indicatorTimer.start(900ms);
}

void InfinitDesktopEffect::showMessage(const QString &text, const QString &icon)
{
    KWin::effects->showOnScreenMessage(text, icon);
}

void InfinitDesktopEffect::persistSoon()
{
    if (m_settings.rememberCanvas || m_placementsDirty) {
        m_persistTimer.start();
    }
}

void InfinitDesktopEffect::persistNow()
{
    m_persistTimer.stop();
    if (m_placementsDirty) {
        m_placementsDirty = false;
        m_stateStore->savePlacements(m_placements.toJson());
    }
    if (!m_settings.rememberCanvas) {
        return;
    }
    QHash<QString, StoredCanvas> stored;
    for (const Canvas &canvas : std::as_const(m_canvases)) {
        StoredCanvas entry;
        entry.camera = Camera{targetCamera(canvas).center, 1.0};
        if (canvas.beforeNavigation) {
            entry.camera = *canvas.beforeNavigation;
        }
        entry.bookmarks = canvas.bookmarks;
        stored.insert(canvas.key, entry);
    }
    m_stateStore->save(stored);
}

QString InfinitDesktopEffect::stateJson() const
{
    QJsonArray outputs;
    for (KWin::LogicalOutput *output : KWin::workspace()->outputs()) {
        outputs.append(QJsonObject{
            {QStringLiteral("name"), output->name()},
            {QStringLiteral("geometry"), rectJson(outputRect(output))},
            {QStringLiteral("scale"), output->scale()},
            {QStringLiteral("locked"), isLocked(output)},
        });
    }
    QJsonArray canvases;
    for (const Canvas &canvas : m_canvases) {
        canvases.append(QJsonObject{
            {QStringLiteral("key"), canvas.key},
            {QStringLiteral("current"), isCurrentCanvas(canvas)},
            {QStringLiteral("camera"), cameraJson(canvas.camera)},
            {QStringLiteral("committed"), cameraJson(canvas.committed)},
            {QStringLiteral("navigation"), canvas.navigation},
            {QStringLiteral("animating"), canvas.motion.isActive()},
            {QStringLiteral("interactions"), canvas.interactions},
        });
    }
    QJsonArray windows;
    for (const auto &[w, managed] : m_windows) {
        QJsonObject object{
            {QStringLiteral("caption"), w->caption()},
            {QStringLiteral("class"), w->windowClass()},
            {QStringLiteral("canvas"), managed->canvasKey},
            {QStringLiteral("canvasX"), managed->canvasPos.x()},
            {QStringLiteral("canvasY"), managed->canvasPos.y()},
            {QStringLiteral("parked"), managed->parked},
            {QStringLiteral("closed"), managed->closed},
            {QStringLiteral("keepAbove"), w->keepAbove()},
            {QStringLiteral("contentScale"), managed->contentScale},
            {QStringLiteral("mode"), modeName(modeOf(*managed))},
            {QStringLiteral("frame"), rectJson(toQRectF(w->frameGeometry()))},
        };
        if (managed->tilePos) {
            object.insert(QStringLiteral("tileX"), managed->tilePos->x());
            object.insert(QStringLiteral("tileY"), managed->tilePos->y());
        }
        windows.append(object);
    }
    const QJsonObject root{
        {QStringLiteral("version"), QStringLiteral(INFINITDESKTOP_VERSION_STRING)},
        {QStringLiteral("safeMode"), isSafeMode()},
        {QStringLiteral("paused"), m_paused},
        {QStringLiteral("active"), m_active},
        {QStringLiteral("operational"), isOperational()},
        {QStringLiteral("outputs"), outputs},
        {QStringLiteral("canvases"), canvases},
        {QStringLiteral("windows"), windows},
    };
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

QString InfinitDesktopEffect::debug(const QString &) const
{
    return stateJson();
}

// ============================================================================
// Remembered window placements
// ============================================================================

OutputIdentity InfinitDesktopEffect::identityOf(const KWin::LogicalOutput *output)
{
    if (!output) {
        return OutputIdentity{};
    }
    return OutputIdentity{output->name(), output->manufacturer(), output->model(), output->serialNumber()};
}

KWin::LogicalOutput *InfinitDesktopEffect::findOutput(const OutputIdentity &identity) const
{
    const auto outputs = KWin::workspace()->outputs();
    QList<OutputIdentity> candidates;
    candidates.reserve(outputs.size());
    for (const KWin::LogicalOutput *output : outputs) {
        candidates.append(identityOf(output));
    }
    const int index = bestOutputMatch(identity, candidates);
    return index >= 0 ? outputs[index] : nullptr;
}

bool InfinitDesktopEffect::isPlacementEligible(KWin::EffectWindow *w) const
{
    KWin::Window *window = w->window();
    if (!window || !w->isNormalWindow() || w->isDialog() || w->isUtility() || window->transientFor()) {
        return false;
    }
    const QString resourceClass = window->resourceClass();
    const QString desktopFile = window->desktopFileName();
    return std::none_of(m_settings.placementExcludedClasses.cbegin(), m_settings.placementExcludedClasses.cend(), [&](const QString &excluded) {
        const QString pattern = excluded.trimmed();
        return !pattern.isEmpty() && (pattern.compare(resourceClass, Qt::CaseInsensitive) == 0 || pattern.compare(desktopFile, Qt::CaseInsensitive) == 0);
    });
}

void InfinitDesktopEffect::applyPlacement(ManagedWindow &window, const WindowPlacement &placement)
{
    KWin::Window *client = window.window;
    if (!client) {
        return;
    }
    // Same monitor as last time if it is connected (EDID first, connector second), else the current one.
    KWin::LogicalOutput *output = findOutput(placement.output);
    if (!output) {
        output = client->output();
    }
    if (!output) {
        return;
    }
    const QRectF viewport = outputRect(output);
    const QRectF area = workArea(output);

    QSizeF size = toQRectF(client->frameGeometry()).size();
    if (m_settings.restoreWindowSize && !placement.maximized && placement.size.width() >= 50 && placement.size.height() >= 50) {
        size = placement.size.boundedTo(area.size());
        client->resize(size);
    }
    QPointF screenPos = viewport.topLeft() + placement.screenOffset;
    screenPos.setX(std::clamp(screenPos.x(), area.left(), std::max(area.left(), area.right() - size.width())));
    screenPos.setY(std::clamp(screenPos.y(), area.top(), std::max(area.top(), area.bottom() - size.height())));

    const QString key = canvasKeyForWindow(window, output);
    Canvas &canvas = ensureCanvas(key, output->name(), key.section(QLatin1Char('|'), 1));
    window.canvasKey = key;
    window.parked = false;

    // With another window of the same application open, this is an additional window and
    // not the one that was closed: it does not inherit the pin (it would sit pinned exactly
    // on top of its sibling) and it does not land on a place a sibling occupies.
    std::vector<const ManagedWindow *> siblings;
    for (const auto &[w, other] : m_windows) {
        if (other.get() != &window && !other->closed && other->appKey == window.appKey) {
            siblings.push_back(other.get());
        }
    }

    if (placement.pinned && siblings.empty()) {
        window.userPinned = true;
        if (placement.keepAbove && !window.effectWindow->keepAbove()) {
            window.keepAboveSetByPin = true;
            client->setKeepAbove(true);
        }
        moveWindowPhysically(client, screenPos);
        return;
    }

    const Camera base = targetCamera(canvas);
    const Camera atRest{base.center, 1.0};
    window.canvasPos = m_settings.restoreOnCanvas ? placement.canvasPos : atRest.screenToCanvas(screenPos, viewport);
    const auto occupied = [&](const QPointF &canvasPos) {
        return std::any_of(siblings.cbegin(), siblings.cend(), [&](const ManagedWindow *other) {
            // Pinned and unparked siblings are where KWin has them, parked ones where the canvas has them.
            const QPointF otherPos = other->parked ? other->canvasPos : atRest.screenToCanvas(toQRectF(other->effectWindow->frameGeometry()).topLeft(), viewport);
            return other->canvasKey == key && (otherPos - canvasPos).manhattanLength() < CascadeOffset;
        });
    };
    for (int i = 0; i < 10 && occupied(window.canvasPos); ++i) {
        window.canvasPos += QPointF(CascadeOffset, CascadeOffset);
    }
    const QRectF rect(window.canvasPos, size);
    const bool needsCamera = canvas.navigation || !isFullyVisible(base, rect, viewport, area);
    if (needsCamera && isCurrentCanvas(canvas)) {
        const Camera target = revealCamera(base, rect, viewport, area, m_settings.revealMode, RevealMargin, 1.0);
        if (placement.maximized && m_settings.restoreWindowSize) {
            // Maximizing needs the window on its screen right away: no camera animation.
            canvas.motion.stop();
            setCamera(canvas, target);
        } else {
            animateTo(canvas, target);
        }
    }
    if (isCurrentCanvas(canvas)) {
        placeWindow(window, canvas, output);
        if (placement.maximized && m_settings.restoreWindowSize && !window.parked) {
            client->maximize(KWin::MaximizeFull);
        }
    }
    qCDebug(KWIN_INFINITDESKTOP) << "restored" << window.appKey << "on" << output->name() << "at" << window.canvasPos;
}

void InfinitDesktopEffect::rememberPlacement(ManagedWindow &window)
{
    if (!m_settings.rememberWindowPlacement || !window.placementEligible || window.appKey.isEmpty() || !window.window || window.closed) {
        return;
    }
    const WindowMode mode = modeOf(window);
    if (mode == WindowMode::Fullscreen || mode == WindowMode::Tile) {
        return;
    }
    Canvas *canvas = canvasByKey(window.canvasKey);
    KWin::LogicalOutput *output = canvas ? outputOf(*canvas) : nullptr;
    if (!output) {
        output = window.window->output();
    }
    if (!output) {
        return;
    }
    const QRectF viewport = outputRect(output);
    const QRectF frame = toQRectF(window.effectWindow->frameGeometry());

    WindowPlacement placement;
    placement.appKey = window.appKey;
    placement.output = identityOf(output);
    placement.desktopId = canvas ? canvas->desktopId : QString();
    placement.maximized = window.window->maximizeMode() == KWin::MaximizeFull;
    placement.size = placement.maximized ? toQRectF(window.window->geometryRestore()).size() : frame.size();
    if (!placement.size.isValid() || placement.size.isEmpty()) {
        placement.size = frame.size();
    }
    placement.pinned = window.userPinned;
    placement.keepAbove = window.effectWindow->keepAbove();
    if (isPinned(window) || !canvas) {
        placement.screenOffset = frame.topLeft() - viewport.topLeft();
        placement.canvasPos = canvas ? canvas->committed.screenToCanvas(frame.topLeft(), viewport) : frame.topLeft();
    } else {
        placement.canvasPos = window.canvasPos;
        placement.screenOffset = Camera{targetCamera(*canvas).center, 1.0}.canvasToScreen(window.canvasPos, viewport) - viewport.topLeft();
    }
    placement.timestamp = QDateTime::currentMSecsSinceEpoch();
    m_placements.remember(placement, window.placementEntry);
    window.placementEntry = 0;
    m_placementsDirty = true;
    persistSoon();
}

QString InfinitDesktopEffect::windowPlacementsJson() const
{
    return QString::fromUtf8(QJsonDocument(m_placements.toJson()).toJson(QJsonDocument::Indented));
}

void InfinitDesktopEffect::forgetWindowPlacements()
{
    m_placements.clear();
    for (auto &[w, managed] : m_windows) {
        managed->placementEntry = 0;
    }
    m_placementsDirty = true;
    persistNow();
}

void InfinitDesktopEffect::forgetWindowPlacements(const QString &appKey)
{
    if (!m_placements.forget(appKey)) {
        return;
    }
    for (auto &[w, managed] : m_windows) {
        if (managed->appKey == appKey) {
            managed->placementEntry = 0;
        }
    }
    m_placementsDirty = true;
    persistNow();
}

// ============================================================================
// Shortcuts
// ============================================================================

void InfinitDesktopEffect::zoomToFit(Canvas &canvas, bool toggle)
{
    KWin::LogicalOutput *output = outputOf(canvas);
    if (!output) {
        return;
    }
    if (toggle && canvas.beforeOverview) {
        const Camera previous = *canvas.beforeOverview;
        canvas.beforeOverview.reset();
        animateTo(canvas, previous);
        return;
    }
    QList<QRectF> rects;
    for (const auto &[w, managed] : m_windows) {
        if (managed->canvasKey != canvas.key || managed->closed) {
            continue;
        }
        const WindowMode mode = modeOf(*managed);
        if (mode == WindowMode::Canvas) {
            rects.append(canvasRect(*managed));
        } else if (mode == WindowMode::Tile && managed->tilePos) {
            rects.append(tileCanvasRect(*managed, canvas));
        }
    }
    if (rects.isEmpty()) {
        showMessage(i18n("No windows on this screen"), QStringLiteral("window"));
        m_indicatorTimer.start(1200ms);
        return;
    }
    canvas.beforeOverview = Camera{targetCamera(canvas).center, targetCamera(canvas).zoom};
    animateTo(canvas, fitCamera(boundingRect(rects), outputRect(output), workArea(output), FitMargin, m_settings.minZoom, 1.0));
}

void InfinitDesktopEffect::goHome(Canvas &canvas, bool toggle)
{
    const Camera home{QPointF(0, 0), 1.0};
    if (toggle && canvas.beforeHome && targetCamera(canvas).fuzzyEquals(home, 1.0)) {
        const Camera previous = *canvas.beforeHome;
        canvas.beforeHome.reset();
        animateTo(canvas, previous);
        return;
    }
    canvas.beforeHome = targetCamera(canvas);
    animateTo(canvas, home);
}

void InfinitDesktopEffect::triggerAction(ShortcutAction action)
{
    if (!isOperational()) {
        return;
    }
    if (action == ShortcutAction::ToggleEnabled) {
        setPaused(!m_paused);
        return;
    }
    KWin::LogicalOutput *output = KWin::effects->activeScreen();
    if (!output || isLocked(output)) {
        return;
    }
    Canvas *canvas = currentCanvas(output);
    if (!canvas) {
        return;
    }
    const QRectF viewport = outputRect(output);
    const QPointF cursor = KWin::effects->cursorPos();
    const QPointF anchor = viewport.contains(cursor) ? cursor : viewport.center();
    const Camera base = targetCamera(*canvas);
    const qreal panX = viewport.width() * m_settings.keyboardPanStep / base.zoom;
    const qreal panY = viewport.height() * m_settings.keyboardPanStep / base.zoom;

    const auto bookmarkIndex = [](ShortcutAction a, ShortcutAction first) {
        return int(a) - int(first);
    };

    switch (action) {
    case ShortcutAction::ZoomIn:
        zoomBy(*canvas, anchor, m_settings.zoomStep, true);
        break;
    case ShortcutAction::ZoomOut:
        zoomBy(*canvas, anchor, 1.0 / m_settings.zoomStep, true);
        break;
    case ShortcutAction::ZoomReset:
        if (canvas->beforeNavigation) {
            animateTo(*canvas, Camera{base.zoomedAt(anchor, 1.0, viewport).center, 1.0});
        } else {
            setZoom(*canvas, anchor, 1.0, true);
        }
        break;
    case ShortcutAction::Overview:
        zoomToFit(*canvas, true);
        break;
    case ShortcutAction::Home:
        goHome(*canvas, true);
        break;
    case ShortcutAction::CenterWindow:
        if (ManagedWindow *managed = record(KWin::effects->activeWindow()); managed && managed->canvasKey == canvas->key) {
            animateTo(*canvas, revealCamera(base, canvasRect(*managed), viewport, workArea(output), RevealMode::Center, 0, base.zoom));
        }
        break;
    case ShortcutAction::PanLeft:
        animateTo(*canvas, Camera{base.center - QPointF(panX, 0), base.zoom});
        break;
    case ShortcutAction::PanRight:
        animateTo(*canvas, Camera{base.center + QPointF(panX, 0), base.zoom});
        break;
    case ShortcutAction::PanUp:
        animateTo(*canvas, Camera{base.center - QPointF(0, panY), base.zoom});
        break;
    case ShortcutAction::PanDown:
        animateTo(*canvas, Camera{base.center + QPointF(0, panY), base.zoom});
        break;
    case ShortcutAction::FocusLeft:
    case ShortcutAction::FocusRight:
    case ShortcutAction::FocusUp:
    case ShortcutAction::FocusDown: {
        const Direction direction = action == ShortcutAction::FocusLeft ? Direction::Left
            : action == ShortcutAction::FocusRight                      ? Direction::Right
            : action == ShortcutAction::FocusUp                         ? Direction::Up
                                                                        : Direction::Down;
        QList<QRectF> rects;
        QList<ManagedWindow *> candidates;
        for (const auto &[w, managed] : m_windows) {
            if (managed->canvasKey == canvas->key && !managed->closed && modeOf(*managed) == WindowMode::Canvas) {
                rects.append(canvasRect(*managed));
                candidates.append(managed.get());
            }
        }
        QRectF from = base.visibleCanvasRect(viewport);
        from = QRectF(from.center() - QPointF(1, 1), QSizeF(2, 2));
        if (ManagedWindow *active = record(KWin::effects->activeWindow()); active && active->canvasKey == canvas->key) {
            from = canvasRect(*active);
        }
        const int index = findNeighbor(from, rects, direction);
        if (index >= 0) {
            ManagedWindow *target = candidates[index];
            const RevealMode mode = canvas->navigation ? RevealMode::Center : m_settings.revealMode;
            animateTo(*canvas, revealCamera(base, canvasRect(*target), viewport, workArea(output), mode, RevealMargin, base.zoom));
            KWin::effects->activateWindow(target->effectWindow);
        }
        break;
    }
    case ShortcutAction::GotoBookmark1:
    case ShortcutAction::GotoBookmark2:
    case ShortcutAction::GotoBookmark3:
    case ShortcutAction::GotoBookmark4: {
        const int index = bookmarkIndex(action, ShortcutAction::GotoBookmark1);
        if (canvas->bookmarks[index]) {
            animateTo(*canvas, *canvas->bookmarks[index]);
        } else {
            showMessage(i18n("Bookmark %1 is empty", index + 1), QStringLiteral("bookmarks"));
            m_indicatorTimer.start(1200ms);
        }
        break;
    }
    case ShortcutAction::SaveBookmark1:
    case ShortcutAction::SaveBookmark2:
    case ShortcutAction::SaveBookmark3:
    case ShortcutAction::SaveBookmark4: {
        const int index = bookmarkIndex(action, ShortcutAction::SaveBookmark1);
        canvas->bookmarks[index] = base;
        persistSoon();
        showMessage(i18n("Bookmark %1 saved", index + 1), QStringLiteral("bookmark-new"));
        m_indicatorTimer.start(1200ms);
        break;
    }
    case ShortcutAction::TogglePin:
    case ShortcutAction::TogglePinAbove: {
        ManagedWindow *managed = record(KWin::effects->activeWindow());
        if (!managed || !managed->window) {
            break;
        }
        const bool keepAbove = action == ShortcutAction::TogglePinAbove;
        // "Pin, always on top" on a window that is only pinned adds "always on top".
        const bool addAbove = keepAbove && managed->userPinned && !managed->effectWindow->keepAbove();
        const bool pin = !managed->userPinned || addAbove;
        setUserPinned(*managed, pin, keepAbove);
        QString message;
        if (!pin) {
            message = i18n("Window released onto the canvas");
        } else if (keepAbove) {
            message = i18n("Window pinned to the screen, always on top");
        } else {
            message = i18n("Window pinned to the screen");
        }
        showMessage(message, QStringLiteral("window-pin"));
        m_indicatorTimer.start(1200ms);
        break;
    }
    case ShortcutAction::ToggleEnabled:
        break;
    }
}

} // namespace InfinitDesktop

#include "moc_infinitdesktopeffect.cpp"
