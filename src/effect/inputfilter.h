/*
    KDE Infinit Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#pragma once

#include "camera.h"
#include "velocitytracker.h"

#include "input.h"

#include <QList>
#include <QPointer>
#include <QTimer>

#include <chrono>
#include <memory>
#include <optional>

class QAction;

namespace KWin
{
class EffectWindow;
class LogicalOutput;
class Window;
}

namespace InfinitDesktop
{

class InfinitDesktopEffect;
struct Canvas;
struct ManagedWindow;
struct ZoomedTarget;

/**
 * Turns pointer, touchpad, touch and keyboard input into canvas navigation.
 *
 * Installed with the GlobalShortcut weight, i.e. after the lock screen, screen
 * edges, drag and drop and the window switcher, but before KWin's own gestures
 * and before interactive window moves, so a slow drag against a screen edge
 * can be stopped there and a three finger swipe can pan instead of switching
 * virtual desktops.
 *
 * The filter never keeps pointers to canvases between events (only keys) and
 * guards windows with QPointer, so nothing dangles when screens or windows go
 * away in the middle of a gesture.
 */
class CanvasInputFilter : public KWin::InputEventFilter
{
public:
    explicit CanvasInputFilter(InfinitDesktopEffect *effect);
    ~CanvasInputFilter() override;

    bool pointerMotion(KWin::PointerMotionEvent *event) override;
    bool pointerButton(KWin::PointerButtonEvent *event) override;
    bool pointerAxis(KWin::PointerAxisEvent *event) override;
    bool keyboardKey(KWin::KeyboardKeyEvent *event) override;

    bool swipeGestureBegin(KWin::PointerSwipeGestureBeginEvent *event) override;
    bool swipeGestureUpdate(KWin::PointerSwipeGestureUpdateEvent *event) override;
    bool swipeGestureEnd(KWin::PointerSwipeGestureEndEvent *event) override;
    bool swipeGestureCancelled(KWin::PointerSwipeGestureCancelEvent *event) override;

    bool pinchGestureBegin(KWin::PointerPinchGestureBeginEvent *event) override;
    bool pinchGestureUpdate(KWin::PointerPinchGestureUpdateEvent *event) override;
    bool pinchGestureEnd(KWin::PointerPinchGestureEndEvent *event) override;
    bool pinchGestureCancelled(KWin::PointerPinchGestureCancelEvent *event) override;

    bool touchDown(KWin::TouchDownEvent *event) override;
    bool touchMotion(KWin::TouchMotionEvent *event) override;
    bool touchUp(KWin::TouchUpEvent *event) override;
    bool touchCancel() override;

    /// Gives the pointer back to KWin after using windows while zoomed out.
    void endZoomedInput();
    /// A window used while zoomed out asked KWin to move it (client side title bar, e.g.
    /// browser tabs), or moved itself (X11 clients like Steam): from now on the plugin moves
    /// it on the canvas. False if the pointer is not held on that window while its canvas is
    /// zoomed out.
    bool takeOverClientMove(ManagedWindow &window);
    /// The plugin is moving @p w on the canvas (a drag of the pointer, maybe taken over).
    bool isMovingWindow(const KWin::EffectWindow *w) const;

private:
    enum class DragKind {
        None,
        PendingDesktopPan, ///< button pressed on the empty desktop, not moved far enough yet
        Pan, ///< the canvas follows the pointer
        Window, ///< pressed on a window that only the plugin draws (zoomed out or parked)
        Resize, ///< pressed on the edge of a zoomed out window (or Meta + right button on it)
        Tile, ///< pressed on a fullscreen tile
    };
    struct Drag
    {
        DragKind kind = DragKind::None;
        Qt::MouseButton button = Qt::NoButton;
        QString canvasKey;
        QPointF pressPos;
        Camera startCamera;
        QPointer<KWin::EffectWindow> window;
        QPointF grabOffset; ///< canvas offset between the pointer and the window/tile origin
        Qt::Edges edges; ///< resized edges
        QRectF startRect; ///< canvas rectangle of the resized window at the press
        bool titleBar = false; ///< pressed on the title bar of a window used while zoomed out
        bool moved = false;
        bool passRelease = false; ///< the press reached the desktop, so must the release
        bool takenOver = false; ///< a move the client started (takeOverClientMove())
        QPointF handPos; ///< taken over: where the user's hand has the pointer
        VelocityTracker tracker;
    };

    bool gesturesEnabled() const;
    static Qt::KeyboardModifiers relevantModifiers(Qt::KeyboardModifiers modifiers);
    static QPointF clampInto(const QPointF &pos, const QRectF &rect);

    bool handleButton(KWin::PointerButtonEvent *event);
    bool handleAxis(KWin::PointerAxisEvent *event);
    /// What the filter returns for an event the plugin used: true, or false when KWin's global
    /// shortcut filter is to stop it (a modifier is held, see the implementation).
    bool consumeUsed(Qt::KeyboardModifiers modifiers, std::optional<Qt::MouseButton> button = std::nullopt);
    void startPan(Canvas &canvas, const QPointF &pos, Qt::MouseButton button, bool passRelease);
    bool handleDragMotion(KWin::PointerMotionEvent *event);
    void updateResizeCursor(const QPointF &pos);

    // Using windows while zoomed out: the plugin gives the pointer to the window drawn under
    // it, with an input transformation from the zoomed position to the window's surface.
    std::optional<ZoomedTarget> forwardTargetAt(const QPointF &pos) const;
    bool forwardMotion(const QPointF &pos, std::chrono::microseconds time);
    void forwardEnter(const ZoomedTarget &target, const QPointF &pos);
    void forwardLeave(const QPointF &pos);
    bool finishDrag(KWin::PointerButtonEvent *event);
    void cancelDrag();
    bool handleInteractiveMove(KWin::PointerMotionEvent *event);
    QPointF edgePanVelocity(KWin::LogicalOutput *output, const QPointF &pos, bool neighbourEdgesBlocked) const;

    void scrollPan(Canvas &canvas, const QPointF &screenDelta, std::chrono::microseconds time);
    void finishScrollPan(bool withVelocity);

    InfinitDesktopEffect *const m_effect;
    Drag m_drag;
    VelocityTracker m_moveTracker;
    QPointer<KWin::Window> m_trackedMove;

    struct
    {
        bool active = false;
        QString canvasKey;
        QPointF offset;
        Camera startCamera;
        VelocityTracker tracker;
    } m_swipe;

    struct
    {
        bool active = false;
        QString canvasKey;
        QPointF anchor;
        QPointF offset;
        Camera startCamera;
    } m_pinch;

    struct
    {
        bool active = false;
        QString canvasKey;
        QPointF offset;
        std::chrono::microseconds lastTime{0};
        VelocityTracker tracker;
    } m_scroll;
    QTimer m_scrollTimer;

    struct
    {
        bool pending = false;
        bool active = false;
        qint32 id = -1;
        QString canvasKey;
        QPointF startPos;
        Camera startCamera;
        VelocityTracker tracker;
    } m_touch;

    bool m_metaAlone = false;
    /// A press that only stopped a moving canvas: its release is swallowed too.
    Qt::MouseButton m_swallowRelease = Qt::NoButton;

    struct
    {
        QPointer<KWin::EffectWindow> window; ///< window (or popup) that has the pointer while zoomed out
        Qt::MouseButtons buttons; ///< pressed on it: it keeps the pointer until they are released
        QList<quint32> nativeButtons; ///< the same buttons as evdev codes
        QPointF position; ///< last pointer position on it while a button is held (no warps)
    } m_forward;
    struct
    {
        QPointer<KWin::EffectWindow> window;
        std::chrono::microseconds time{0};
    } m_lastTitleClick;
    /// Shortcuts that do nothing, registered with KWin for events used with a modifier held
    /// (see consumeUsed()). Deleting the action unregisters them.
    struct
    {
        std::unique_ptr<QAction> action;
        Qt::KeyboardModifiers modifiers;
        bool axis = false;
        Qt::MouseButtons buttons;
    } m_shortcutPass;
};

} // namespace InfinitDesktop
