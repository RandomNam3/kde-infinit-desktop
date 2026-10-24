#!/usr/bin/env python3
# Test helper: captures the KWin workspace through org.kde.KWin.ScreenShot2.
# Usage: screenshot.py out.png [screen-name]
import os
import sys
import threading

import dbus
from PIL import Image


def main():
    out = sys.argv[1]
    screen = sys.argv[2] if len(sys.argv) > 2 else None
    bus = dbus.SessionBus()
    obj = bus.get_object("org.kde.KWin", "/org/kde/KWin/ScreenShot2")
    iface = dbus.Interface(obj, "org.kde.KWin.ScreenShot2")
    read_fd, write_fd = os.pipe()
    chunks = []

    def reader():
        with os.fdopen(read_fd, "rb") as f:
            while True:
                data = f.read(1 << 20)
                if not data:
                    break
                chunks.append(data)

    thread = threading.Thread(target=reader)
    thread.start()
    options = {"native-resolution": dbus.Boolean(True, variant_level=1), "include-cursor": dbus.Boolean(True, variant_level=1)}
    if screen:
        meta = iface.CaptureScreen(screen, options, dbus.types.UnixFd(write_fd), timeout=20)
    else:
        meta = iface.CaptureWorkspace(options, dbus.types.UnixFd(write_fd), timeout=20)
    os.close(write_fd)
    thread.join()
    data = b"".join(chunks)
    width, height, stride = int(meta["width"]), int(meta["height"]), int(meta["stride"])
    # QImage formats: ARGB32 (5, 6) is BGRA in memory, RGBA8888 / RGBX8888 (16, 17, 18) is RGBA.
    raw = "RGBA" if int(meta.get("format", 6)) in (16, 17, 18) else "BGRA"
    image = Image.frombuffer("RGBA", (width, height), data, "raw", raw, stride, 1)
    image.convert("RGB").save(out)
    print(f"{out}: {width}x{height}")


if __name__ == "__main__":
    main()
