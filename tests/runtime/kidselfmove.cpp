// X11 test window that moves itself, like Steam or launchers under Wine/Proton: no
// decoration (Motif hints), and dragging its top 40 pixels moves it with XMoveWindow,
// following the pointer as the client sees it in root coordinates.
// usage: kid-selfmove [title] [--skip-taskbar]   (prints PRESS, SELFMOVE <count>, RELEASE)
// --skip-taskbar: not in the task bar, like the splash and helper windows of launchers.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <cstdio>
#include <cstring>

int main(int argc, char **argv)
{
    const char *title = argc > 1 ? argv[1] : "SelfMove";
    Display *display = XOpenDisplay(nullptr);
    if (!display) {
        std::fprintf(stderr, "cannot open display\n");
        return 1;
    }
    const int screen = DefaultScreen(display);
    const Window root = RootWindow(display, screen);
    XSetWindowAttributes attributes{};
    attributes.background_pixel = 0x3050a0;
    attributes.event_mask = ButtonPressMask | ButtonReleaseMask | PointerMotionMask | ExposureMask;
    const Window window = XCreateWindow(display, root, 200, 200, 480, 320, 0, CopyFromParent, InputOutput, CopyFromParent,
                                        CWBackPixel | CWEventMask, &attributes);
    XStoreName(display, window, title);
    XClassHint hint{const_cast<char *>("kid-selfmove"), const_cast<char *>("kid-selfmove")};
    XSetClassHint(display, window, &hint);
    // _MOTIF_WM_HINTS: flags = decorations, decorations = none
    const long motif[5] = {2, 0, 0, 0, 0};
    const Atom motifAtom = XInternAtom(display, "_MOTIF_WM_HINTS", False);
    XChangeProperty(display, window, motifAtom, motifAtom, 32, PropModeReplace, reinterpret_cast<const unsigned char *>(motif), 5);
    if (argc > 2 && std::strcmp(argv[2], "--skip-taskbar") == 0) {
        const Atom skipTaskbar = XInternAtom(display, "_NET_WM_STATE_SKIP_TASKBAR", False);
        XChangeProperty(display, window, XInternAtom(display, "_NET_WM_STATE", False), XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char *>(&skipTaskbar), 1);
    }
    XMapWindow(display, window);
    XFlush(display);

    bool dragging = false;
    int pressRootX = 0, pressRootY = 0, startX = 0, startY = 0, moves = 0;
    for (;;) {
        XEvent event;
        XNextEvent(display, &event);
        switch (event.type) {
        case ButtonPress:
            if (event.xbutton.button == Button1 && event.xbutton.y < 40) {
                Window child;
                XTranslateCoordinates(display, window, root, 0, 0, &startX, &startY, &child);
                pressRootX = event.xbutton.x_root;
                pressRootY = event.xbutton.y_root;
                dragging = true;
                moves = 0;
                std::printf("PRESS %d %d\n", event.xbutton.x, event.xbutton.y);
            }
            break;
        case MotionNotify:
            if (dragging) {
                XMoveWindow(display, window, startX + event.xmotion.x_root - pressRootX, startY + event.xmotion.y_root - pressRootY);
                XFlush(display);
                std::printf("SELFMOVE %d\n", ++moves);
            }
            break;
        case ButtonRelease:
            if (event.xbutton.button == Button1 && dragging) {
                dragging = false;
                std::printf("RELEASE %d\n", moves);
            }
            break;
        default:
            break;
        }
        std::fflush(stdout);
    }
}
