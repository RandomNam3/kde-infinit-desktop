/*
    KDE Infinit Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "inputfilter.h"
#include "infinitdesktopeffect.h"
#include "launcherpositioner.h"
#include "logging.h"
#include "organizer.h"

#include "core/output.h"
#include "effect/effecthandler.h"
#include "input_event.h"
#include "main.h"
#include "options.h"
#include "pointer_input.h"
#include "wayland/seat.h"
#include "wayland_server.h"
#include "window.h"

#include <QAction>
#include <QGuiApplication>
#include <QLineF>
#include <QStyleHints>

#include <cmath>

using namespace std::chrono_literals;

namespace InfinitDesktop
{

namespace
{

constexpr qreal DragThreshold = 6.0;
constexpr qreal TouchThreshold = 14.0;
constexpr qreal WheelPanStep = 80.0;
constexpr auto ScrollIdle = 160ms;

QRectF rectOf(const KWin::LogicalOutput *output)
{
    if (!output) {
        return QRectF();
    }
    const KWin::RectF geometry = output->geometryF();
    return QRectF(geometry.x(), geometry.y(), geometry.width(), geometry.height());
}

} // namespace

CanvasInputFilter::CanvasInputFilter(InfinitDesktopEffect *effect)
    : KWin::InputEventFilter(KWin::InputFilterOrder::GlobalShortcut)
    , m_effect(effect)
{
    m_scrollTimer.setSingleShot(true);
    m_scrollTimer.setInterval(ScrollIdle);
    QObject::connect(&m_scrollTimer, &QTimer::timeout, &m_scrollTimer, [this]() {
        finishScrollPan(false);
    });
}

CanvasInputFilter::~CanvasInputFilter() = default;

bool CanvasInputFilter::gesturesEnabled() const
{
    return m_effect->isOperational() && !m_effect->isPaused();
}

Qt::KeyboardModifiers CanvasInputFilter::relevantModifiers(Qt::KeyboardModifiers modifiers)
{
    return modifiers & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
}

QPointF CanvasInputFilter::clampInto(const QPointF &pos, const QRectF &rect)
{
    return QPointF(std::clamp(pos.x(), rect.left(), rect.right() - 1.0), std::clamp(pos.y(), rect.top(), rect.bottom() - 1.0));
}

// ============================================================================
// Pointer
// ============================================================================

bool CanvasInputFilter::pointerMotion(KWin::PointerMotionEvent *event)
{
    if (m_shortcutPass.action && !event->warp) {
        m_shortcutPass = {};
    }
    if (Organizer *organizer = m_effect->organizer(); organizer && organizer->overlayActive()) {
        // Canvas menu, selection or map: they get the pointer.
        return organizer->pointerMotion(event->position);
    }
    if (m_drag.kind == DragKind::Window && m_drag.takenOver) {
        if (event->warp) {
            // KWin starting another move the client asked for puts the pointer where the
            // client thinks it is (its parked window): back to the hand.
            if (event->position != m_drag.handPos) {
                KWin::input()->warpPointer(m_drag.handPos);
            }
            return true;
        }
        m_drag.handPos = event->position;
    }
    if (m_drag.kind != DragKind::None) {
        return handleDragMotion(event);
    }
    if (m_forward.buttons && event->warp) {
        // Held on a window used while zoomed out. X11 clients have KWin warp the pointer to
        // where they think it is (on the parked window) right before KWin starts their move:
        // not a position of the user's hand, see takeOverClientMove().
        return true;
    }
    if (m_effect->moveState()) {
        return handleInteractiveMove(event);
    }
    if (m_effect->isOperational() && m_effect->hasForeignOverlap()) {
        if (KWin::LogicalOutput *output = m_effect->outputContaining(event->position)) {
            m_effect->resolveForeignOverlap(event->position, output);
        }
    }
    updateResizeCursor(event->position);
    if (m_effect->isOperational()) {
        m_effect->keepMarkersNear(event->position, m_effect->outputContaining(event->position));
    }
    return forwardMotion(event->position, event->timestamp);
}

std::optional<ZoomedTarget> CanvasInputFilter::forwardTargetAt(const QPointF &pos) const
{
    if (!m_effect->isOperational() || !m_effect->settings().interactWhileZoomed) {
        return std::nullopt;
    }
    KWin::LogicalOutput *output = m_effect->outputContaining(pos);
    Canvas *canvas = output && !m_effect->isLocked(output) ? m_effect->currentCanvas(output) : nullptr;
    if (!canvas || !canvas->navigation) {
        return std::nullopt;
    }
    std::optional<ZoomedTarget> target = m_effect->zoomedTargetAt(pos, output);
    // The resize zones along the edges belong to the plugin.
    if (target && target->window == target->managed->effectWindow && m_effect->resizeEdgesAt(*target->managed, *canvas, pos)) {
        return std::nullopt;
    }
    return target;
}

void CanvasInputFilter::forwardEnter(const ZoomedTarget &target, const QPointF &pos)
{
    KWin::Window *window = target.window->window();
    if (!window || !window->surface()) {
        return;
    }
    m_forward.window = target.window;
    // An X11 window first goes where Xwayland can deliver the pointer to it.
    m_effect->setZoomedInputWindow(target.managed ? target.managed->effectWindow : nullptr);
    KWin::waylandServer()->seat()->notifyPointerEnter(window->surface(), pos, target.inputTransform);
}

void CanvasInputFilter::forwardLeave(const QPointF &pos)
{
    m_forward.window.clear();
    m_forward.buttons = {};
    m_forward.nativeButtons.clear();
    m_effect->setZoomedInputWindow(nullptr);
    // Back to whatever KWin has under the pointer (the desktop, a panel, ...).
    KWin::SeatInterface *seat = KWin::waylandServer()->seat();
    KWin::Window *focus = KWin::input()->pointer()->focus();
    if (focus && focus->surface()) {
        seat->notifyPointerEnter(focus->surface(), pos, focus->inputTransformation());
    } else {
        seat->notifyPointerLeave();
    }
}

void CanvasInputFilter::endZoomedInput()
{
    if (m_forward.window || m_forward.buttons) {
        forwardLeave(KWin::effects->cursorPos());
    }
}

bool CanvasInputFilter::forwardMotion(const QPointF &pos, std::chrono::microseconds time)
{
    KWin::SeatInterface *seat = KWin::waylandServer()->seat();
    const auto send = [&]() {
        seat->setTimestamp(time);
        seat->notifyPointerMotion(pos);
        seat->notifyPointerFrame();
    };
    if (m_forward.buttons) {
        // Implicit grab: the window keeps the pointer while a button is held.
        m_forward.position = pos;
        KWin::EffectWindow *w = m_forward.window;
        const std::optional<QMatrix4x4> transform = w ? m_effect->zoomedInputTransform(w) : std::nullopt;
        if (!transform) {
            qCDebug(KWIN_INFINITDESKTOP) << "window used while zoomed out is gone, ending its input";
            endZoomedInput();
            return false;
        }
        seat->setFocusedPointerSurfaceTransformation(*transform);
        send();
        return true;
    }
    const std::optional<ZoomedTarget> target = forwardTargetAt(pos);
    if (!target || target->decoration) {
        if (m_forward.window) {
            forwardLeave(pos);
        }
        return false;
    }
    if (m_forward.window != target->window) {
        forwardEnter(*target, pos);
    } else {
        if (target->managed) {
            // Keeps an X11 window from ending up physically under the pointer.
            m_effect->setZoomedInputWindow(target->managed->effectWindow);
        }
        seat->setFocusedPointerSurfaceTransformation(target->inputTransform);
    }
    send();
    return true;
}

bool CanvasInputFilter::takeOverClientMove(ManagedWindow &window)
{
    if (!m_forward.buttons || m_forward.window != window.effectWindow || m_drag.kind != DragKind::None) {
        qCDebug(KWIN_INFINITDESKTOP) << "not taking over the move: button held" << bool(m_forward.buttons) << "on"
                                     << (m_forward.window ? m_forward.window->caption() : QString()) << "drag" << int(m_drag.kind);
        return false;
    }
    Canvas *canvas = m_effect->canvasByKey(window.canvasKey);
    KWin::LogicalOutput *output = canvas ? m_effect->outputOf(*canvas) : nullptr;
    if (!canvas || !canvas->navigation || !output) {
        qCDebug(KWIN_INFINITDESKTOP) << "not taking over the move: canvas not zoomed out";
        return false;
    }
    // Where the user holds the window. X11 clients had KWin warp the pointer onto the parked
    // window (to the screen edge) before the move started: it goes back there below.
    const QPointF pos = m_forward.position;
    // Usually the left button; otherwise the lowest one held.
    Qt::MouseButton button = Qt::LeftButton;
    while (!(m_forward.buttons & button)) {
        button = Qt::MouseButton(button << 1);
    }
    // The client gets its release now; the real one ends the canvas move.
    KWin::SeatInterface *seat = KWin::waylandServer()->seat();
    for (quint32 native : std::as_const(m_forward.nativeButtons)) {
        seat->notifyPointerButton(native, KWin::PointerButtonState::Released);
    }
    seat->notifyPointerFrame();
    forwardLeave(pos);
    m_effect->holdZoomedInputWindow(window.effectWindow);

    m_drag = Drag{};
    m_drag.kind = DragKind::Window;
    m_drag.button = button;
    m_drag.canvasKey = canvas->key;
    m_drag.pressPos = pos;
    m_drag.window = window.effectWindow;
    m_drag.moved = true;
    m_drag.takenOver = true;
    m_drag.handPos = pos;
    m_drag.grabOffset = canvas->camera.screenToCanvas(pos, rectOf(output)) - window.canvasPos;
    qCDebug(KWIN_INFINITDESKTOP) << "taking over the move of" << window.effectWindow->caption() << "at" << pos << "(pointer at" << KWin::effects->cursorPos() << ")";
    if (KWin::effects->cursorPos() != pos) {
        // Handled by the drag above: the window stays where it is.
        KWin::input()->warpPointer(pos);
    }
    return true;
}

bool CanvasInputFilter::isMovingWindow(const KWin::EffectWindow *w) const
{
    return w && m_drag.kind == DragKind::Window && m_drag.window.data() == w;
}

void CanvasInputFilter::updateResizeCursor(const QPointF &pos)
{
    Qt::Edges edges;
    if (m_effect->isOperational()) {
        KWin::LogicalOutput *output = m_effect->outputContaining(pos);
        Canvas *canvas = output && !m_effect->isLocked(output) ? m_effect->currentCanvas(output) : nullptr;
        if (canvas && canvas->navigation) {
            const CanvasHit hit = m_effect->hitTest(pos, output);
            if ((hit.kind == CanvasHit::Kind::Window || hit.kind == CanvasHit::Kind::ParkedWindow) && hit.window) {
                edges = m_effect->resizeEdgesAt(*hit.window, *canvas, pos);
            }
        }
    }
    m_effect->setResizeCursor(edges);
}

void CanvasInputFilter::startPan(Canvas &canvas, const QPointF &pos, Qt::MouseButton button, bool passRelease)
{
    m_effect->beginInteraction(canvas);
    m_drag = Drag{};
    m_drag.kind = DragKind::Pan;
    m_drag.button = button;
    m_drag.canvasKey = canvas.key;
    m_drag.pressPos = pos;
    m_drag.startCamera = canvas.camera;
    m_drag.passRelease = passRelease;
}

bool CanvasInputFilter::handleDragMotion(KWin::PointerMotionEvent *event)
{
    const QPointF pos = event->position;
    Canvas *canvas = m_effect->canvasByKey(m_drag.canvasKey);
    if (!canvas) {
        cancelDrag();
        return false;
    }
    m_drag.tracker.addSample(pos, event->timestamp);
    const qreal distance = QLineF(pos, m_drag.pressPos).length();

    switch (m_drag.kind) {
    case DragKind::None:
        return false;
    case DragKind::PendingDesktopPan:
        if (distance < DragThreshold) {
            // Small movements still reach the desktop (hover effects of the wallpaper).
            return false;
        }
        m_effect->beginInteraction(*canvas);
        m_drag.kind = DragKind::Pan;
        m_drag.startCamera = canvas->camera;
        m_drag.pressPos = pos;
        return true;
    case DragKind::Pan:
        m_effect->setCamera(*canvas, m_drag.startCamera.pannedByScreenDelta(pos - m_drag.pressPos));
        return true;
    case DragKind::Window:
    case DragKind::Tile: {
        if (!m_drag.moved && distance < DragThreshold) {
            return true;
        }
        m_drag.moved = true;
        ManagedWindow *window = m_drag.window ? m_effect->findManaged(m_drag.window.data()) : nullptr;
        if (!window) {
            cancelDrag();
            return true;
        }
        KWin::LogicalOutput *source = m_effect->outputOf(*canvas);
        KWin::LogicalOutput *under = m_effect->outputContaining(pos);
        QPointF effectivePos = pos;
        if (under && source && under != source) {
            const EffectSettings &settings = m_effect->settings();
            const qreal speed = m_drag.tracker.speed(event->timestamp);
            const bool allowed = (!settings.flingToOtherScreen || speed >= settings.flingThreshold) && !m_effect->isLocked(under);
            if (allowed) {
                Canvas *target = m_effect->currentCanvas(under);
                // Keep the grab point at the same screen distance under the new zoom.
                m_drag.grabOffset *= canvas->camera.zoom / target->camera.zoom;
                canvas = target;
                m_drag.canvasKey = target->key;
            } else {
                effectivePos = clampInto(pos, rectOf(source));
                m_drag.handPos = effectivePos;
                if (!event->warp) {
                    KWin::input()->warpPointer(effectivePos);
                }
            }
        }
        KWin::LogicalOutput *output = m_effect->outputOf(*canvas);
        if (!output) {
            return true;
        }
        const QPointF canvasPoint = canvas->camera.screenToCanvas(effectivePos, rectOf(output));
        if (m_drag.kind == DragKind::Window) {
            m_effect->moveWindowOnCanvas(*window, *canvas, canvasPoint - m_drag.grabOffset);
        } else {
            m_effect->moveTileOnCanvas(*window, *canvas, canvasPoint - m_drag.grabOffset);
        }
        return true;
    }
    case DragKind::Resize: {
        ManagedWindow *window = m_drag.window ? m_effect->findManaged(m_drag.window.data()) : nullptr;
        if (!window) {
            cancelDrag();
            return true;
        }
        m_drag.moved = true;
        // Stays on its own screen: the pointer is kept there like for slow window drags.
        KWin::LogicalOutput *output = m_effect->outputOf(*canvas);
        const QPointF effectivePos = output ? clampInto(pos, rectOf(output)) : pos;
        if (effectivePos != pos && !event->warp) {
            KWin::input()->warpPointer(effectivePos);
        }
        m_effect->resizeWindowOnCanvas(*window, *canvas, m_drag.edges, m_drag.startRect, (effectivePos - m_drag.pressPos) / canvas->camera.zoom);
        return true;
    }
    }
    return false;
}

bool CanvasInputFilter::finishDrag(KWin::PointerButtonEvent *event)
{
    Drag drag = std::move(m_drag);
    m_drag = Drag{};
    Canvas *canvas = m_effect->canvasByKey(drag.canvasKey);

    switch (drag.kind) {
    case DragKind::None:
    case DragKind::PendingDesktopPan:
        // A plain click on the desktop: the desktop got the press, it gets the release.
        return false;
    case DragKind::Pan:
        if (canvas) {
            drag.tracker.addSample(event->position, event->timestamp);
            // Only a flick at the end glides; circling the mouse before releasing does not.
            m_effect->endInteraction(*canvas, drag.tracker.flingVelocity(event->timestamp));
        }
        return !drag.passRelease;
    case DragKind::Window: {
        ManagedWindow *window = drag.window ? m_effect->findManaged(drag.window.data()) : nullptr;
        if (!window || !canvas) {
            return true;
        }
        if (!drag.moved) {
            if (canvas->navigation && drag.titleBar) {
                // Title bar of a window used while zoomed out: a click activates it, a double
                // click returns to 100 % on it.
                const auto interval = std::chrono::milliseconds(QGuiApplication::styleHints()->mouseDoubleClickInterval());
                if (m_lastTitleClick.window == window->effectWindow && event->timestamp - m_lastTitleClick.time <= interval) {
                    m_lastTitleClick = {};
                    m_effect->activateAndReveal(*window, true);
                } else {
                    m_lastTitleClick.window = window->effectWindow;
                    m_lastTitleClick.time = event->timestamp;
                    KWin::effects->activateWindow(window->effectWindow);
                }
            } else if (canvas->navigation && !m_effect->settings().clickZoomedWindowFocuses) {
                KWin::effects->activateWindow(window->effectWindow);
            } else {
                m_effect->activateAndReveal(*window, true);
            }
        } else if (!canvas->navigation) {
            m_effect->commit(*canvas);
        } else if (!m_forward.window) {
            // Released after a move: like a fresh hover (the X11 window kept its input spot).
            m_effect->setZoomedInputWindow(nullptr);
            forwardMotion(event->position, event->timestamp);
        }
        if (drag.moved) {
            // Re-look the window up: committing may have changed the bookkeeping.
            if (ManagedWindow *moved = drag.window ? m_effect->findManaged(drag.window.data()) : nullptr) {
                m_effect->windowArranged(*moved);
            }
        }
        return true;
    }
    case DragKind::Resize: {
        if (ManagedWindow *window = drag.window ? m_effect->findManaged(drag.window.data()) : nullptr) {
            m_effect->endCanvasResize(*window);
        }
        updateResizeCursor(event->position);
        return true;
    }
    case DragKind::Tile: {
        ManagedWindow *window = drag.window ? m_effect->findManaged(drag.window.data()) : nullptr;
        if (window && !drag.moved) {
            m_effect->restoreTile(*window);
        }
        return true;
    }
    }
    return false;
}

void CanvasInputFilter::cancelDrag()
{
    Drag drag = std::move(m_drag);
    m_drag = Drag{};
    if (drag.kind == DragKind::Pan) {
        if (Canvas *canvas = m_effect->canvasByKey(drag.canvasKey)) {
            m_effect->endInteraction(*canvas);
        }
    } else if (drag.kind == DragKind::Resize) {
        if (ManagedWindow *window = drag.window ? m_effect->findManaged(drag.window.data()) : nullptr) {
            m_effect->endCanvasResize(*window);
        }
    }
}

bool CanvasInputFilter::pointerButton(KWin::PointerButtonEvent *event)
{
    if (!handleButton(event)) {
        return false;
    }
    // KWin compares all held buttons, and ignores releases.
    if (event->state == KWin::PointerButtonState::Pressed && event->buttons == event->button) {
        return consumeUsed(event->modifiers, event->button);
    }
    return true;
}

bool CanvasInputFilter::handleButton(KWin::PointerButtonEvent *event)
{
    if (Organizer *organizer = m_effect->organizer(); organizer && organizer->overlayActive()) {
        m_swallowRelease = Qt::NoButton;
        return organizer->pointerButton(event);
    }
    if (event->state == KWin::PointerButtonState::Released) {
        if (m_swallowRelease != Qt::NoButton && event->button == m_swallowRelease) {
            m_swallowRelease = Qt::NoButton;
            return true;
        }
        if (m_drag.kind != DragKind::None && event->button == m_drag.button) {
            return finishDrag(event);
        }
        if (m_forward.buttons & event->button) {
            // Released on a window used while zoomed out (wherever the pointer is now).
            m_forward.buttons &= ~event->button;
            m_forward.nativeButtons.removeAll(event->nativeButton);
            KWin::SeatInterface *seat = KWin::waylandServer()->seat();
            seat->setTimestamp(event->timestamp);
            seat->notifyPointerButton(event->nativeButton, KWin::PointerButtonState::Released);
            seat->notifyPointerFrame();
            if (!m_forward.buttons) {
                forwardMotion(event->position, event->timestamp);
            }
            return true;
        }
        return false;
    }

    m_metaAlone = false;
    if (m_drag.kind != DragKind::None) {
        // Another button during a drag of ours: swallow it, unless the desktop
        // already got the first press (then it should see this one too).
        return m_drag.kind != DragKind::PendingDesktopPan;
    }
    if (!m_effect->isOperational()) {
        return false;
    }
    const QPointF pos = event->position;
    KWin::LogicalOutput *output = m_effect->outputContaining(pos);
    if (!output || m_effect->isLocked(output)) {
        return false;
    }
    Canvas *canvas = m_effect->currentCanvas(output);
    if (!canvas) {
        return false;
    }

    // From now on, what the user sees must be where KWin delivers the input.
    if (m_scroll.active) {
        finishScrollPan(false);
    }
    if (event->button == Qt::LeftButton) {
        if (ManagedWindow *target = m_effect->offscreenMarkerAt(pos, output)) {
            // A marker at the screen edge: fly to its window.
            m_effect->stopMotion(*canvas);
            m_effect->flyToWindow(*target);
            m_swallowRelease = event->button;
            return true;
        }
    }
    if (canvas->interactions == 0 && (canvas->motion.isActive() || (!canvas->navigation && m_effect->isDirty(*canvas)))) {
        // The canvas is still moving (gliding, animating). KWin has already picked the
        // pointer focus for this press from the old window positions, so it cannot reach
        // the window the user sees. Like touching a scrolling list, this press only
        // stops the motion; the next one goes to the right window.
        m_effect->stopMotion(*canvas);
        if (!canvas->navigation) {
            m_effect->commitIfDirty(*canvas);
        }
        m_swallowRelease = event->button;
        return true;
    }
    if (m_effect->hasForeignOverlap() && m_effect->resolveForeignOverlap(pos, output)) {
        // KWin had already chosen the hidden window of the other screen as the target.
        m_swallowRelease = event->button;
        return true;
    }

    const CanvasHit hit = m_effect->hitTest(pos, output);
    const EffectSettings &settings = m_effect->settings();
    const Qt::KeyboardModifiers modifiers = relevantModifiers(event->modifiers);
    const QRectF viewport = rectOf(output);

    const auto startWindowDrag = [&](DragKind kind) {
        m_drag = Drag{};
        m_drag.kind = kind;
        m_drag.button = event->button;
        m_drag.canvasKey = canvas->key;
        m_drag.pressPos = pos;
        m_drag.window = hit.window->effectWindow;
        const QPointF origin = kind == DragKind::Tile ? hit.window->tilePos.value_or(QPointF()) : hit.window->canvasPos;
        m_drag.grabOffset = canvas->camera.screenToCanvas(pos, viewport) - origin;
        m_drag.tracker.addSample(pos, event->timestamp);
    };

    const auto startResize = [&](Qt::Edges edges) {
        m_drag = Drag{};
        m_drag.kind = DragKind::Resize;
        m_drag.button = event->button;
        m_drag.canvasKey = canvas->key;
        m_drag.pressPos = pos;
        m_drag.window = hit.window->effectWindow;
        m_drag.edges = edges;
        m_drag.startRect = m_effect->canvasRect(*hit.window);
    };

    if (canvas->navigation) {
        // Zoomed: every canvas window is only drawn by the plugin, so we handle the input.
        if (hit.kind == CanvasHit::Kind::Foreign) {
            return false;
        }
        const bool onWindow = (hit.kind == CanvasHit::Kind::Window || hit.kind == CanvasHit::Kind::ParkedWindow) && hit.window;
        if (onWindow && event->button == Qt::RightButton && modifiers == Qt::KeyboardModifiers(KWin::options->commandAllModifier())) {
            // Like KWin's modifier + right button: resize from the nearest corner.
            const QRectF visual = canvas->camera.canvasToScreen(m_effect->canvasRect(*hit.window), viewport);
            // A corner has a resize zone exactly when the window can be resized here.
            if (m_effect->resizeEdgesAt(*hit.window, *canvas, visual.topLeft() + QPointF(1, 1))) {
                startResize((pos.x() < visual.center().x() ? Qt::LeftEdge : Qt::RightEdge) | (pos.y() < visual.center().y() ? Qt::TopEdge : Qt::BottomEdge));
            }
            return true;
        }
        if (settings.interactWhileZoomed) {
            if (onWindow && event->button == Qt::LeftButton && modifiers == Qt::KeyboardModifiers(KWin::options->commandAllModifier())) {
                // Meta + drag moves a window, wherever it is grabbed.
                startWindowDrag(DragKind::Window);
                return true;
            }
            if (const std::optional<ZoomedTarget> target = forwardTargetAt(pos)) {
                if (target->decoration) {
                    // Title bar: drag to move on the canvas, click to activate, double click for 100 %.
                    if (event->button == Qt::LeftButton && onWindow && hit.window == target->managed) {
                        startWindowDrag(DragKind::Window);
                        m_drag.titleBar = true;
                    }
                    return true;
                }
                // Using the window at this zoom: it gets the click, and the keyboard.
                if (target->window == target->managed->effectWindow && KWin::effects->activeWindow() != target->window) {
                    KWin::effects->activateWindow(target->window);
                }
                if (m_forward.window != target->window) {
                    forwardEnter(*target, pos);
                }
                KWin::SeatInterface *seat = KWin::waylandServer()->seat();
                seat->setTimestamp(event->timestamp);
                seat->notifyPointerButton(event->nativeButton, KWin::PointerButtonState::Pressed);
                seat->notifyPointerFrame();
                m_forward.buttons |= event->button;
                m_forward.nativeButtons.append(event->nativeButton);
                m_forward.position = pos;
                return true;
            }
        }
        if (event->button == Qt::LeftButton) {
            const Qt::Edges edges = onWindow ? m_effect->resizeEdgesAt(*hit.window, *canvas, pos) : Qt::Edges();
            if (edges) {
                startResize(edges);
            } else if (onWindow) {
                startWindowDrag(DragKind::Window);
            } else if (hit.kind == CanvasHit::Kind::Tile && hit.window) {
                startWindowDrag(DragKind::Tile);
            } else {
                startPan(*canvas, pos, event->button, false);
            }
            return true;
        }
        if (event->button == Qt::MiddleButton && hit.kind == CanvasHit::Kind::Nothing) {
            startPan(*canvas, pos, event->button, false);
            return true;
        }
        if (event->button == Qt::RightButton && hit.kind == CanvasHit::Kind::Nothing && m_effect->organizer()) {
            // Right click on the empty canvas opens the canvas menu (notes, groups, areas, map).
            m_effect->organizer()->showCanvasMenu(output, pos);
            return true;
        }
        return hit.kind != CanvasHit::Kind::Nothing;
    }

    if (gesturesEnabled() && event->button == Qt::LeftButton && settings.panModifiers != Qt::NoModifier && modifiers == settings.panModifiers) {
        startPan(*canvas, pos, event->button, false);
        return true;
    }
    if (hit.kind == CanvasHit::Kind::ParkedWindow && hit.window) {
        // Only a preview at the screen edge: a click brings it in, a drag moves it.
        if (event->button == Qt::LeftButton) {
            startWindowDrag(DragKind::Window);
        }
        return true;
    }
    if (hit.kind == CanvasHit::Kind::Tile && hit.window) {
        if (event->button == Qt::LeftButton) {
            startWindowDrag(DragKind::Tile);
        }
        return true;
    }
    if (event->button == Qt::RightButton && hit.kind == CanvasHit::Kind::Nothing && modifiers == Qt::MetaModifier && m_effect->organizer()) {
        // At 100 % the desktop keeps its own menu; Meta + right click opens the canvas menu.
        m_effect->organizer()->showCanvasMenu(output, pos);
        return true;
    }
    if (hit.kind == CanvasHit::Kind::Nothing && gesturesEnabled() && modifiers == Qt::NoModifier
        && ((event->button == Qt::LeftButton && settings.dragDesktopToPan) || (event->button == Qt::MiddleButton && settings.middleButtonPan))) {
        // Let the desktop see the press; it becomes a pan only once the pointer moves.
        m_drag = Drag{};
        m_drag.kind = DragKind::PendingDesktopPan;
        m_drag.button = event->button;
        m_drag.canvasKey = canvas->key;
        m_drag.pressPos = pos;
        m_drag.passRelease = true;
        m_drag.tracker.addSample(pos, event->timestamp);
        return false;
    }
    return false;
}

bool CanvasInputFilter::pointerAxis(KWin::PointerAxisEvent *event)
{
    return handleAxis(event) && consumeUsed(event->modifiers);
}

bool CanvasInputFilter::handleAxis(KWin::PointerAxisEvent *event)
{
    m_metaAlone = false;
    if (Organizer *organizer = m_effect->organizer(); organizer && organizer->overlayActive()) {
        return organizer->pointerAxis(event);
    }
    if (m_drag.kind == DragKind::Pan) {
        return true;
    }
    if (!m_effect->isOperational()) {
        return false;
    }
    const QPointF pos = event->position;
    KWin::LogicalOutput *output = m_effect->outputContaining(pos);
    if (!output || m_effect->isLocked(output)) {
        return false;
    }
    Canvas *canvas = m_effect->currentCanvas(output);
    if (!canvas) {
        return false;
    }
    const EffectSettings &settings = m_effect->settings();
    const Qt::KeyboardModifiers modifiers = relevantModifiers(event->modifiers);
    const bool wheel = event->source == KWin::PointerAxisSource::Wheel || event->source == KWin::PointerAxisSource::WheelTilt
        || event->source == KWin::PointerAxisSource::Unknown;
    const bool vertical = event->orientation == Qt::Vertical;
    // Positive notches scroll towards the user (down / right).
    const qreal notches = event->deltaV120 != 0 ? event->deltaV120 / 120.0 : event->delta / 15.0;
    const qreal panDirection = settings.invertScrollPan ? -1.0 : 1.0;

    const auto zoomWheel = [&]() {
        const qreal steps = settings.invertWheelZoom ? notches : -notches;
        m_effect->zoomBy(*canvas, pos, std::pow(settings.zoomStep, steps), true);
        return true;
    };
    const auto zoomSmooth = [&]() {
        const qreal sign = settings.invertWheelZoom ? -1.0 : 1.0;
        m_effect->zoomBy(*canvas, pos, std::exp(-event->delta * 0.004 * sign), false);
        return true;
    };
    const auto panFinger = [&]() {
        if (event->delta == 0.0) {
            // Fingers lifted ("axis stop"): glide with the scrolling speed.
            finishScrollPan(true);
            return true;
        }
        const QPointF delta = vertical ? QPointF(0, event->delta) : QPointF(event->delta, 0);
        scrollPan(*canvas, -delta * settings.panSpeed * panDirection, event->timestamp);
        return true;
    };
    const auto panWheel = [&](bool horizontal) {
        const QPointF delta = horizontal ? QPointF(notches * WheelPanStep, 0) : QPointF(0, notches * WheelPanStep);
        scrollPan(*canvas, -delta, event->timestamp);
        return true;
    };

    if (canvas->navigation) {
        const bool zoomModifier = settings.zoomModifiers != Qt::NoModifier && modifiers == settings.zoomModifiers;
        if (!zoomModifier) {
            // A window used while zoomed out scrolls itself; Meta + wheel still zooms everywhere.
            if (const std::optional<ZoomedTarget> target = forwardTargetAt(pos); target && !target->decoration) {
                if (m_forward.window != target->window) {
                    forwardEnter(*target, pos);
                }
                KWin::SeatInterface *seat = KWin::waylandServer()->seat();
                seat->setTimestamp(event->timestamp);
                seat->notifyPointerAxis(event->orientation, event->delta, event->deltaV120, event->source, event->inverted);
                seat->notifyPointerFrame();
                return true;
            }
        }
        const CanvasHit hit = m_effect->hitTest(pos, output);
        if (hit.kind == CanvasHit::Kind::Foreign) {
            return false;
        }
        if (!wheel) {
            if (settings.zoomModifiers != Qt::NoModifier && modifiers == settings.zoomModifiers && vertical) {
                return zoomSmooth();
            }
            return panFinger();
        }
        if (!vertical || modifiers == Qt::ShiftModifier) {
            return panWheel(true);
        }
        if (modifiers == Qt::ControlModifier) {
            return panWheel(false);
        }
        return zoomWheel();
    }

    if (!gesturesEnabled()) {
        return false;
    }
    if (settings.zoomModifiers != Qt::NoModifier && modifiers == settings.zoomModifiers) {
        if (!vertical) {
            return false;
        }
        return wheel ? zoomWheel() : zoomSmooth();
    }
    if (m_effect->hitTest(pos, output).kind != CanvasHit::Kind::Nothing) {
        return false;
    }
    if (wheel) {
        if (modifiers == Qt::NoModifier && vertical && settings.wheelZoomOnDesktop) {
            return zoomWheel();
        }
        if (settings.shiftWheelPans) {
            if ((modifiers == Qt::ShiftModifier && vertical) || (modifiers == Qt::NoModifier && !vertical)) {
                return panWheel(true);
            }
            if (modifiers == Qt::ControlModifier && vertical) {
                return panWheel(false);
            }
        }
        return false;
    }
    if (modifiers == Qt::NoModifier && settings.touchpadScrollPans) {
        return panFinger();
    }
    return false;
}

bool CanvasInputFilter::consumeUsed(Qt::KeyboardModifiers modifiers, std::optional<Qt::MouseButton> button)
{
#if KWIN_BUILD_GLOBALSHORTCUTS
    // Plasma opens the launcher when Meta is pressed and released on its own. KWin's global
    // shortcut filter, which comes right after this one, cancels that when a button or the
    // wheel is used with a modifier held. So an event the plugin used with a modifier held
    // (Meta + wheel zooms) goes on to it, and a shortcut that does nothing stops it there.
    // The shortcuts are removed with the next motion or key.
    if (relevantModifiers(modifiers) == Qt::NoModifier || !KWin::kwinApp()->supportsGlobalShortcuts()) {
        return true;
    }
    if (m_shortcutPass.action && m_shortcutPass.modifiers != modifiers) {
        m_shortcutPass = {};
    }
    if (!m_shortcutPass.action) {
        m_shortcutPass.action = std::make_unique<QAction>();
        m_shortcutPass.modifiers = modifiers;
    }
    if (!button) {
        if (!m_shortcutPass.axis) {
            for (KWin::PointerAxisDirection direction : {KWin::PointerAxisUp, KWin::PointerAxisDown, KWin::PointerAxisLeft, KWin::PointerAxisRight}) {
                KWin::input()->registerAxisShortcut(modifiers, direction, m_shortcutPass.action.get());
            }
            m_shortcutPass.axis = true;
        }
    } else if (!(m_shortcutPass.buttons & *button)) {
        KWin::input()->registerPointerShortcut(modifiers, *button, m_shortcutPass.action.get());
        m_shortcutPass.buttons |= *button;
    }
    return false;
#else
    Q_UNUSED(modifiers)
    Q_UNUSED(button)
    return true;
#endif
}

void CanvasInputFilter::scrollPan(Canvas &canvas, const QPointF &screenDelta, std::chrono::microseconds time)
{
    if (!m_scroll.active || m_scroll.canvasKey != canvas.key) {
        finishScrollPan(false);
        m_effect->beginInteraction(canvas);
        m_scroll.active = true;
        m_scroll.canvasKey = canvas.key;
        m_scroll.offset = QPointF();
        m_scroll.tracker.reset();
        m_scroll.tracker.addSample(m_scroll.offset, time);
    }
    m_scroll.offset += screenDelta;
    m_scroll.tracker.addSample(m_scroll.offset, time);
    m_scroll.lastTime = time;
    m_effect->panBy(canvas, screenDelta);
    m_scrollTimer.start();
}

void CanvasInputFilter::finishScrollPan(bool withVelocity)
{
    if (!m_scroll.active) {
        return;
    }
    m_scroll.active = false;
    m_scrollTimer.stop();
    if (Canvas *canvas = m_effect->canvasByKey(m_scroll.canvasKey)) {
        const QPointF velocity = withVelocity ? m_scroll.tracker.velocity(m_scroll.lastTime) : QPointF();
        m_effect->endInteraction(*canvas, velocity);
    }
}

// ============================================================================
// Interactive window moves (KWin moves the window, we decide about screen changes)
// ============================================================================

QPointF CanvasInputFilter::edgePanVelocity(KWin::LogicalOutput *output, const QPointF &pos, bool neighbourEdgesBlocked) const
{
    const EffectSettings &settings = m_effect->settings();
    if (!settings.edgePan || !output) {
        return QPointF();
    }
    const QRectF rect = rectOf(output);
    const qreal margin = settings.edgePanMargin;
    const auto strength = [margin](qreal depth) {
        return std::clamp(depth / margin, 0.0, 1.0);
    };
    // An edge with a neighbouring screen only scrolls while crossing over is blocked.
    const auto usable = [&](const QPointF &probe) {
        return neighbourEdgesBlocked || !m_effect->outputContaining(probe);
    };

    qreal vx = 0;
    qreal vy = 0;
    if (pos.x() < rect.left() + margin && usable(QPointF(rect.left() - 2, pos.y()))) {
        vx = -strength(rect.left() + margin - pos.x());
    } else if (pos.x() > rect.right() - 1 - margin && usable(QPointF(rect.right() + 2, pos.y()))) {
        vx = strength(pos.x() - (rect.right() - 1 - margin));
    }
    if (pos.y() < rect.top() + margin && usable(QPointF(pos.x(), rect.top() - 2))) {
        vy = -strength(rect.top() + margin - pos.y());
    } else if (pos.y() > rect.bottom() - 1 - margin && usable(QPointF(pos.x(), rect.bottom() + 2))) {
        vy = strength(pos.y() - (rect.bottom() - 1 - margin));
    }
    return QPointF(vx, vy) * settings.edgePanSpeed;
}

bool CanvasInputFilter::handleInteractiveMove(KWin::PointerMotionEvent *event)
{
    auto &move = m_effect->moveState();
    KWin::Window *window = move->window;
    if (!window || !window->isInteractiveMove() || !gesturesEnabled()) {
        m_effect->stopEdgePan();
        return false;
    }
    const QPointF pos = event->position;
    if (m_trackedMove != window) {
        m_trackedMove = window;
        m_moveTracker.reset();
    }
    if (!event->warp) {
        // Hand speed, independent of the warps that hold the pointer at an edge.
        // Relative devices: accumulate their deltas. Absolute devices (tablets,
        // remote desktop, VMs) report no delta, but their raw position already is
        // where the hand is, even beyond the edge we warp the pointer back to.
        if (event->delta.isNull()) {
            move->virtualPosition = pos;
        } else {
            move->virtualPosition += event->delta;
        }
        m_moveTracker.addSample(move->virtualPosition, event->timestamp);
    }

    const EffectSettings &settings = m_effect->settings();
    KWin::LogicalOutput *from = move->output;
    KWin::LogicalOutput *to = m_effect->outputContaining(pos);

    if (from && to && to != from) {
        const qreal speed = m_moveTracker.speed(event->timestamp);
        if (!settings.flingToOtherScreen || speed >= settings.flingThreshold) {
            qCDebug(KWIN_INFINITDESKTOP) << "window flung to" << to->name() << "at" << speed << "px/s";
            move->output = to;
            m_effect->setEdgePan(to, edgePanVelocity(to, pos, settings.flingToOtherScreen));
            return false;
        }
        // Too slow: the window stays on its screen, the canvas scrolls instead.
        const QPointF clamped = clampInto(pos, rectOf(from));
        qCDebug(KWIN_INFINITDESKTOP) << "screen change blocked at" << pos << "speed" << speed << "px/s, warping to" << clamped
                                     << "warping supported:" << KWin::input()->supportsPointerWarping();
        KWin::input()->warpPointer(clamped);
        m_effect->setEdgePan(from, edgePanVelocity(from, clamped, true));
        return true;
    }
    if (from) {
        m_effect->setEdgePan(from, edgePanVelocity(from, pos, settings.flingToOtherScreen));
    }
    return false;
}

// ============================================================================
// Keyboard
// ============================================================================

bool CanvasInputFilter::keyboardKey(KWin::KeyboardKeyEvent *event)
{
    m_shortcutPass = {};
    const bool metaKey = event->key == Qt::Key_Meta || event->key == Qt::Key_Super_L || event->key == Qt::Key_Super_R;
    if (event->state == KWin::KeyboardKeyState::Released) {
        if (metaKey) {
            if (m_metaAlone && m_effect->launcher()) {
                // Super tapped on its own: Plasma opens the launcher right after this.
                m_effect->launcher()->noteLauncherRequest(KWin::effects->cursorPos());
            }
            m_metaAlone = false;
        }
        return false;
    }
    if (event->state != KWin::KeyboardKeyState::Pressed) {
        return false;
    }
    if (metaKey) {
        m_metaAlone = true;
        return false;
    }
    m_metaAlone = false;
    if (Organizer *organizer = m_effect->organizer(); organizer && organizer->overlayActive() && organizer->keyboardKey(event)) {
        return true;
    }
    if (event->key == Qt::Key_F1 && (event->modifiers & Qt::AltModifier) && m_effect->launcher()) {
        m_effect->launcher()->noteLauncherRequest(KWin::effects->cursorPos());
    }

    if (!m_effect->isOperational()) {
        return false;
    }
    if (m_drag.kind == DragKind::Pan && event->key == Qt::Key_Escape) {
        if (Canvas *canvas = m_effect->canvasByKey(m_drag.canvasKey)) {
            m_effect->setCamera(*canvas, m_drag.startCamera);
        }
        cancelDrag();
        return true;
    }

    KWin::LogicalOutput *output = KWin::effects->activeScreen();
    Canvas *canvas = output ? m_effect->currentCanvas(output) : nullptr;
    // Windows used while zoomed out need Escape and Enter themselves.
    if (!canvas || !canvas->navigation || m_effect->settings().interactWhileZoomed) {
        return false;
    }
    switch (event->key) {
    case Qt::Key_Escape:
        m_effect->animateTo(*canvas, canvas->beforeNavigation.value_or(Camera{canvas->camera.center, 1.0}));
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        const CanvasHit hit = m_effect->hitTest(KWin::effects->cursorPos(), output);
        if ((hit.kind == CanvasHit::Kind::Window || hit.kind == CanvasHit::Kind::ParkedWindow) && hit.window) {
            m_effect->activateAndReveal(*hit.window, true);
            return true;
        }
        if (ManagedWindow *active = m_effect->findManaged(KWin::effects->activeWindow())) {
            m_effect->activateAndReveal(*active, true);
            return true;
        }
        return false;
    }
    default:
        return false;
    }
}

// ============================================================================
// Touchpad gestures
// ============================================================================

bool CanvasInputFilter::swipeGestureBegin(KWin::PointerSwipeGestureBeginEvent *event)
{
    const EffectSettings &settings = m_effect->settings();
    if (!gesturesEnabled() || settings.swipeFingers == 0 || event->fingerCount != settings.swipeFingers) {
        return false;
    }
    KWin::LogicalOutput *output = m_effect->pointerOutput();
    if (!output || m_effect->isLocked(output)) {
        return false;
    }
    Canvas *canvas = m_effect->currentCanvas(output);
    if (!canvas) {
        return false;
    }
    m_effect->beginInteraction(*canvas);
    m_swipe.active = true;
    m_swipe.canvasKey = canvas->key;
    m_swipe.offset = QPointF();
    m_swipe.startCamera = canvas->camera;
    m_swipe.tracker.reset();
    m_swipe.tracker.addSample(m_swipe.offset, event->time);
    return true;
}

bool CanvasInputFilter::swipeGestureUpdate(KWin::PointerSwipeGestureUpdateEvent *event)
{
    if (!m_swipe.active) {
        return false;
    }
    Canvas *canvas = m_effect->canvasByKey(m_swipe.canvasKey);
    if (!canvas) {
        m_swipe.active = false;
        return true;
    }
    const EffectSettings &settings = m_effect->settings();
    // Content follows the fingers, like dragging a sheet of paper.
    m_swipe.offset += event->delta * settings.panSpeed * (settings.invertScrollPan ? -1.0 : 1.0);
    m_swipe.tracker.addSample(m_swipe.offset, event->time);
    m_effect->setCamera(*canvas, m_swipe.startCamera.pannedByScreenDelta(m_swipe.offset));
    return true;
}

bool CanvasInputFilter::swipeGestureEnd(KWin::PointerSwipeGestureEndEvent *event)
{
    if (!m_swipe.active) {
        return false;
    }
    m_swipe.active = false;
    if (Canvas *canvas = m_effect->canvasByKey(m_swipe.canvasKey)) {
        m_effect->endInteraction(*canvas, m_swipe.tracker.velocity(event->time));
    }
    return true;
}

bool CanvasInputFilter::swipeGestureCancelled(KWin::PointerSwipeGestureCancelEvent *)
{
    if (!m_swipe.active) {
        return false;
    }
    m_swipe.active = false;
    if (Canvas *canvas = m_effect->canvasByKey(m_swipe.canvasKey)) {
        m_effect->endInteraction(*canvas);
    }
    return true;
}

bool CanvasInputFilter::pinchGestureBegin(KWin::PointerPinchGestureBeginEvent *event)
{
    const EffectSettings &settings = m_effect->settings();
    if (!m_effect->isOperational() || !settings.pinchZoom || event->fingerCount < 2) {
        return false;
    }
    KWin::LogicalOutput *output = m_effect->pointerOutput();
    if (!output || m_effect->isLocked(output)) {
        return false;
    }
    Canvas *canvas = m_effect->currentCanvas(output);
    if (!canvas) {
        return false;
    }
    const QPointF cursor = KWin::effects->cursorPos();
    const CanvasHit hit = m_effect->hitTest(cursor, output);
    const bool ours = canvas->navigation ? hit.kind != CanvasHit::Kind::Foreign : (gesturesEnabled() && hit.kind == CanvasHit::Kind::Nothing);
    if (!ours) {
        return false;
    }
    m_effect->beginInteraction(*canvas);
    m_pinch.active = true;
    m_pinch.canvasKey = canvas->key;
    m_pinch.anchor = cursor;
    m_pinch.offset = QPointF();
    m_pinch.startCamera = canvas->camera;
    return true;
}

bool CanvasInputFilter::pinchGestureUpdate(KWin::PointerPinchGestureUpdateEvent *event)
{
    if (!m_pinch.active) {
        return false;
    }
    Canvas *canvas = m_effect->canvasByKey(m_pinch.canvasKey);
    KWin::LogicalOutput *output = canvas ? m_effect->outputOf(*canvas) : nullptr;
    if (!canvas || !output) {
        m_pinch.active = false;
        return true;
    }
    const EffectSettings &settings = m_effect->settings();
    const qreal zoom = clampZoom(m_pinch.startCamera.zoom * event->scale, settings.minZoom, settings.maxZoom, 0.0);
    m_pinch.offset += event->delta;
    const Camera camera = m_pinch.startCamera.zoomedAt(m_pinch.anchor, zoom, rectOf(output)).pannedByScreenDelta(m_pinch.offset);
    m_effect->setCamera(*canvas, camera);
    return true;
}

bool CanvasInputFilter::pinchGestureEnd(KWin::PointerPinchGestureEndEvent *)
{
    if (!m_pinch.active) {
        return false;
    }
    m_pinch.active = false;
    if (Canvas *canvas = m_effect->canvasByKey(m_pinch.canvasKey)) {
        m_effect->endInteraction(*canvas);
        m_effect->showZoomIndicator(*canvas);
    }
    return true;
}

bool CanvasInputFilter::pinchGestureCancelled(KWin::PointerPinchGestureCancelEvent *)
{
    if (!m_pinch.active) {
        return false;
    }
    m_pinch.active = false;
    if (Canvas *canvas = m_effect->canvasByKey(m_pinch.canvasKey)) {
        m_effect->setCamera(*canvas, m_pinch.startCamera);
        m_effect->endInteraction(*canvas);
    }
    return true;
}

// ============================================================================
// Touchscreen
// ============================================================================

bool CanvasInputFilter::touchDown(KWin::TouchDownEvent *event)
{
    if (m_touch.pending || m_touch.active) {
        // A second finger: while panning keep it, otherwise give the gesture to others.
        if (m_touch.active) {
            return true;
        }
        m_touch = {};
        return false;
    }
    if (!gesturesEnabled() || !m_effect->settings().touchscreenPan) {
        return false;
    }
    KWin::LogicalOutput *output = m_effect->outputContaining(event->pos);
    if (!output || m_effect->isLocked(output)) {
        return false;
    }
    Canvas *canvas = m_effect->currentCanvas(output);
    if (!canvas) {
        return false;
    }
    const CanvasHit hit = m_effect->hitTest(event->pos, output);
    if (canvas->navigation ? hit.kind == CanvasHit::Kind::Foreign : hit.kind != CanvasHit::Kind::Nothing) {
        return false;
    }
    m_touch.pending = true;
    m_touch.active = false;
    m_touch.id = event->id;
    m_touch.canvasKey = canvas->key;
    m_touch.startPos = event->pos;
    m_touch.tracker.reset();
    m_touch.tracker.addSample(event->pos, event->time);
    return false;
}

bool CanvasInputFilter::touchMotion(KWin::TouchMotionEvent *event)
{
    if (event->id != m_touch.id || !(m_touch.pending || m_touch.active)) {
        return false;
    }
    Canvas *canvas = m_effect->canvasByKey(m_touch.canvasKey);
    if (!canvas) {
        m_touch = {};
        return false;
    }
    m_touch.tracker.addSample(event->pos, event->time);
    if (m_touch.pending) {
        if (QLineF(event->pos, m_touch.startPos).length() < TouchThreshold) {
            return false;
        }
        m_touch.pending = false;
        m_touch.active = true;
        m_effect->beginInteraction(*canvas);
        m_touch.startCamera = canvas->camera;
        m_touch.startPos = event->pos;
    }
    m_effect->setCamera(*canvas, m_touch.startCamera.pannedByScreenDelta(event->pos - m_touch.startPos));
    return true;
}

bool CanvasInputFilter::touchUp(KWin::TouchUpEvent *event)
{
    if (event->id != m_touch.id) {
        return m_touch.active;
    }
    if (m_touch.active) {
        if (Canvas *canvas = m_effect->canvasByKey(m_touch.canvasKey)) {
            m_effect->endInteraction(*canvas, m_touch.tracker.flingVelocity(event->time));
        }
    }
    m_touch = {};
    return false;
}

bool CanvasInputFilter::touchCancel()
{
    if (m_touch.active) {
        if (Canvas *canvas = m_effect->canvasByKey(m_touch.canvasKey)) {
            m_effect->endInteraction(*canvas);
        }
    }
    m_touch = {};
    return false;
}

} // namespace InfinitDesktop
