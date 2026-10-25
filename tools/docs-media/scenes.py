#!/usr/bin/env python3
# KDE Infinit Desktop - scenes for the documentation images and animations.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage (inside the session started by session.sh): scenes.py OUTDIR [all|scene,scene,...]
#
# Animations are recorded in stop motion: the camera is moved in small steps over D-Bus
# (or the input helper does one step) and every step is captured with KWin's screenshot
# interface. The result is smooth, whatever the speed of the software renderer.

import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time

import dbus
from PIL import Image

OUT = sys.argv[1] if len(sys.argv) > 1 else "/out"
WANTED = (sys.argv[2] if len(sys.argv) > 2 else "all").split(",")
GERMAN = os.environ.get("MEDIA_LANG", "de") == "de"
WIDTH = 960  # width of the animations

bus = dbus.SessionBus()
effect = dbus.Interface(bus.get_object("org.kde.KWin", "/InfinitDesktop"), "org.kde.InfinitDesktop")
screenshot = dbus.Interface(bus.get_object("org.kde.KWin", "/org/kde/KWin/ScreenShot2"), "org.kde.KWin.ScreenShot2")
scripting = dbus.Interface(bus.get_object("org.kde.KWin", "/Scripting"), "org.kde.kwin.Scripting")


# ---------------------------------------------------------------- helpers
def state():
    return json.loads(str(effect.state()))


def window(caption_part):
    for w in state()["windows"]:
        if caption_part in w["caption"]:
            return w
    return None


def camera():
    return [c for c in state()["canvases"] if c["current"]][0]["camera"]


def set_camera(x, y, zoom, settle=0.06):
    effect.setCamera(float(x), float(y), float(zoom))
    time.sleep(settle)


def kwin_js(code, settle=0.4):
    with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as f:
        f.write(code)
        path = f.name
    sid = scripting.loadScript(path, "media%d" % int(time.time() * 1000), signature="ss")
    dbus.Interface(bus.get_object("org.kde.KWin", "/Scripting/Script%d" % sid), "org.kde.kwin.Script").run()
    time.sleep(settle)


def place(caption_part, x, y, w, h):
    kwin_js('for (const w of workspace.windowList()) if (w.caption.indexOf(%s) >= 0) w.frameGeometry = {x: %d, y: %d, width: %d, height: %d};'
            % (json.dumps(caption_part), x, y, w, h))


def activate(caption_part):
    kwin_js('for (const w of workspace.windowList()) if (w.caption.indexOf(%s) >= 0) workspace.activeWindow = w;' % json.dumps(caption_part))


def wait_for(caption_part, timeout=15.0):
    end = time.time() + timeout
    while time.time() < end:
        if window(caption_part):
            return True
        time.sleep(0.2)
    print("[media] window %r did not appear" % caption_part, flush=True)
    return False


class Input:
    """kid-input in its line mode: buttons stay held between commands."""

    def __init__(self):
        self.proc = subprocess.Popen(["/usr/local/bin/kid-input", "-"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)

    def __call__(self, *args):
        self.proc.stdin.write(" ".join(str(a) for a in args) + "\n")
        self.proc.stdin.flush()
        self.proc.stdout.readline()


inp = Input()

# evdev key codes (US positions); with a German layout y and z trade places.
KEYS = {c: k for c, k in zip("qwertyuiopasdfghjklzxcvbnm", [16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 30, 31, 32, 33, 34, 35, 36, 37, 38, 44, 45, 46, 47, 48, 49, 50])}
if GERMAN:
    KEYS["y"], KEYS["z"] = KEYS["z"], KEYS["y"]
KEY_SPACE, KEY_ENTER, KEY_SHIFT, KEY_META, KEY_ALT, KEY_DOT, KEY_COMMA, KEY_PAGEDOWN = 57, 28, 42, 125, 56, 52, 51, 109


def type_char(ch):
    if ch == " ":
        inp("key", KEY_SPACE)
    elif ch == "\n":
        inp("key", KEY_ENTER)
    elif ch == ":":
        inp("keydown", KEY_SHIFT, "key", KEY_DOT, "keyup", KEY_SHIFT)
    elif ch == ",":
        inp("key", KEY_COMMA)
    elif ch.isupper():
        inp("keydown", KEY_SHIFT, "key", KEYS[ch.lower()], "keyup", KEY_SHIFT)
    else:
        inp("key", KEYS[ch])


def type_text(text):
    """Like type_char, plus the Markdown characters # * - and digits (German or US layout)."""
    for ch in text:
        if ch == "#":
            inp("key", 43) if GERMAN else inp("keydown", KEY_SHIFT, "key", 4, "keyup", KEY_SHIFT)
        elif ch == "*":
            inp("keydown", KEY_SHIFT, "key", 27 if GERMAN else 9, "keyup", KEY_SHIFT)
        elif ch == "-":
            inp("key", 53 if GERMAN else 12)
        elif ch.isdigit():
            inp("key", 2 + (int(ch) + 9) % 10)
        else:
            type_char(ch)


def capture():
    r, w = os.pipe()
    chunks = []

    def reader():
        with os.fdopen(r, "rb") as f:
            while True:
                data = f.read(1 << 20)
                if not data:
                    break
                chunks.append(data)

    t = threading.Thread(target=reader)
    t.start()
    options = {"native-resolution": dbus.Boolean(True, variant_level=1), "include-cursor": dbus.Boolean(True, variant_level=1)}
    meta = screenshot.CaptureScreen("Virtual-0", options, dbus.types.UnixFd(w), timeout=30)
    os.close(w)
    t.join()
    data = b"".join(chunks)
    width, height, stride = int(meta["width"]), int(meta["height"]), int(meta["stride"])
    raw = "RGBA" if int(meta.get("format", 6)) in (16, 17, 18) else "BGRA"
    return Image.frombuffer("RGBA", (width, height), data, "raw", raw, stride, 1).convert("RGB")


class Clip:
    def __init__(self, name, fps=18):
        self.name, self.fps, self.count = name, fps, 0
        self.dir = tempfile.mkdtemp(prefix="clip-%s-" % name)
        self.last = None

    def frame(self, hold=1):
        image = capture().resize((WIDTH, WIDTH * 9 // 16), Image.LANCZOS)
        for _ in range(hold):
            self.count += 1
            image.save(os.path.join(self.dir, "%04d.png" % self.count))
        self.last = image

    def hold(self, frames):
        for _ in range(frames):
            self.count += 1
            self.last.save(os.path.join(self.dir, "%04d.png" % self.count))

    def save(self):
        # Animated WebP: shown inline like a GIF (GitHub, browsers), a fraction of the size.
        target = os.path.join(OUT, self.name + ".webp")
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-framerate", str(self.fps), "-i", os.path.join(self.dir, "%04d.png"),
                        "-c:v", "libwebp_anim", "-lossless", "0", "-quality", "78", "-compression_level", "6", "-loop", "0", target], check=True)
        shutil.rmtree(self.dir, ignore_errors=True)
        print("[media] %s: %d frames, %d kB" % (target, self.count, os.path.getsize(target) // 1024), flush=True)


def still(name, width=1600, crop=None):
    image = capture()
    if crop:
        image = image.crop(crop)
    if width:
        image = image.resize((width, width * image.height // image.width), Image.LANCZOS)
    target = os.path.join(OUT, name + ".png")
    image.save(target, optimize=True)
    print("[media] %s" % target, flush=True)


def work_area():
    """KWin's usable area of the screen (without the panel): x, y, width, height."""
    kwin_js('const a = workspace.clientArea(KWin.MaximizeArea, workspace.activeScreen, workspace.currentDesktop);'
            'print("MEDIAAREA " + a.x + " " + a.y + " " + a.width + " " + a.height);')
    with open("/tmp/kwin.log", errors="replace") as log:
        lines = [line for line in log if "MEDIAAREA" in line]
    return [float(v) for v in lines[-1].split("MEDIAAREA ")[1].split()] if lines else [0.0, 0.0, 1920.0, 1080.0]


def marker_position(w, cam):
    """Where the plugin puts the marker of window @p w (see InfinitDesktopEffect::offscreenMarkers)."""
    ax, ay, aw, ah = work_area()
    inset = 20 + 10  # half the marker size + margin
    left, top, right, bottom = ax + inset, ay + inset, ax + aw - inset, ay + ah - inset
    cx, cy = (left + right) / 2, (top + bottom) / 2
    zoom = cam["zoom"]
    wx = 960 + (w["canvasX"] + w["frame"]["width"] / 2 - cam["x"]) * zoom
    wy = 540 + (w["canvasY"] + w["frame"]["height"] / 2 - cam["y"]) * zoom
    dx, dy = wx - cx, wy - cy
    # where the line from the centre to the window leaves the inner rectangle
    candidates = []
    if dx > 0:
        candidates.append((right - cx) / dx)
    elif dx < 0:
        candidates.append((left - cx) / dx)
    if dy > 0:
        candidates.append((bottom - cy) / dy)
    elif dy < 0:
        candidates.append((top - cy) / dy)
    t = min(candidates)
    return int(cx + dx * t), int(cy + dy * t)


def ease(t):
    return 0.5 - 0.5 * math.cos(math.pi * t)


def camera_path(clip, start, end, frames, hold_end=0):
    for i in range(1, frames + 1):
        t = ease(i / frames)
        # zoom interpolated logarithmically, like the plugin's own animations
        zoom = math.exp(math.log(start[2]) + (math.log(end[2]) - math.log(start[2])) * t)
        set_camera(start[0] + (end[0] - start[0]) * t, start[1] + (end[1] - start[1]) * t, zoom)
        clip.frame()
    clip.hold(hold_end)


def screen_point(canvas_x, canvas_y):
    c = camera()
    return int(960 + (canvas_x - c["x"]) * c["zoom"]), int(540 + (canvas_y - c["y"]) * c["zoom"])


HOME = (0.0, 0.0, 1.0)


def layout():
    """Windows spread over more than one screen; the camera at home."""
    set_camera(*HOME, settle=0.3)
    place("Kate", 110, 110, 860, 640)
    place("Konsole", 1030, 150, 780, 470)
    place("Systemeinstellungen" if GERMAN else "System Settings", 2250, 120, 1020, 700)
    place("KDE Infinit Desktop", 260, 1300, 1120, 760)
    set_camera(*HOME, settle=0.8)


# ---------------------------------------------------------------- scenes
def scene_pan():
    """The canvas moves, the wallpaper and the panel stay."""
    layout()
    clip = Clip("canvas-pan")
    clip.frame(hold=12)
    camera_path(clip, HOME, (1500, 0, 1.0), 40, hold_end=12)
    camera_path(clip, (1500, 0, 1.0), (1500, 900, 1.0), 30, hold_end=10)
    camera_path(clip, (1500, 900, 1.0), HOME, 40, hold_end=10)
    clip.save()


def scene_zoom():
    """Zooming out shows everything, live and sharp."""
    layout()
    clip = Clip("zoom-overview")
    clip.frame(hold=10)
    camera_path(clip, HOME, (700, 450, 0.42), 36)
    time.sleep(1.5)  # applications redraw for the smaller size
    clip.frame(hold=24)
    camera_path(clip, (700, 450, 0.42), HOME, 36, hold_end=8)
    clip.save()


def scene_overview_still():
    layout()
    set_camera(700, 450, 0.42, settle=1.8)
    still("overview")


def scene_work_zoomed():
    """Typing a note and moving a window while zoomed out."""
    layout()
    set_camera(700, 450, 0.42, settle=1.8)
    clip = Clip("work-while-zoomed", fps=14)
    clip.frame(hold=8)
    kate = window("Kate")
    x, y = screen_point(kate["canvasX"] + 500, kate["canvasY"] + 560)
    inp("abs", x + 30, y + 30, "sleep", 60, "abs", x, y, "sleep", 60)
    clip.frame(hold=3)
    inp("click", "left", "sleep", 150)
    inp("keydown", 29, "key", 107, "keyup", 29, "sleep", 100)  # Ctrl+End: end of the text
    clip.frame(hold=3)
    text = "\n- Notiz eintragen, ohne auf 100 % zu gehen" if GERMAN else "\n- Take a note without going back to 100 %"
    for i, ch in enumerate(text):
        if ch == "%":
            inp("keydown", KEY_SHIFT, "key", 6, "keyup", KEY_SHIFT)
        elif ch.isdigit():
            inp("key", 2 + (int(ch) + 9) % 10)
        elif ch == "-":
            inp("key", 53 if GERMAN else 12)
        else:
            type_char(ch)
        if i % 2 == 1:
            clip.frame()
    time.sleep(0.4)
    clip.frame(hold=10)
    # move the terminal by its title bar
    konsole = window("Konsole")
    tx, ty = screen_point(konsole["canvasX"] + 390, konsole["canvasY"] + 15)
    inp("abs", tx, ty, "sleep", 80, "down", "left", "sleep", 50)
    for i in range(1, 21):
        inp("abs", tx + i * 6, ty + i * 9, "sleep", 15)
        clip.frame()
    inp("up", "left", "sleep", 200)
    clip.frame(hold=12)
    clip.save()


def scene_resize_zoomed():
    layout()
    set_camera(700, 450, 0.42, settle=1.8)
    clip = Clip("resize-while-zoomed", fps=16)
    clip.frame(hold=6)
    konsole = window("Konsole")
    f = konsole["frame"]
    x, y = screen_point(konsole["canvasX"] + f["width"] - 3, konsole["canvasY"] + f["height"] - 3)
    inp("abs", x - 20, y - 20, "sleep", 60, "abs", x, y, "sleep", 120)
    clip.frame(hold=6)
    inp("down", "left", "sleep", 50)
    for i in range(1, 26):
        inp("abs", x + i * 5, y + i * 4, "sleep", 20)
        time.sleep(0.05)
        clip.frame()
    inp("up", "left", "sleep", 400)
    time.sleep(0.6)
    clip.frame(hold=14)
    clip.save()


def scene_markers():
    """Windows outside of the view get markers at the screen edge; a click flies there."""
    layout()
    clip = Clip("offscreen-markers", fps=16)
    clip.frame(hold=6)
    camera_path(clip, HOME, (-500, -150, 1.0), 26)  # pan away: the other windows leave the view
    # the marker of the settings window, computed like the plugin does (offscreenMarkers)
    c = camera()
    settings = window("Systemeinstellungen" if GERMAN else "System Settings")
    mx, my = marker_position(settings, c)
    # towards the marker quickly (the markers fade out a while after the last movement)
    for i in range(1, 6):
        inp("abs", 960 + (mx - 960) * i // 5, 540 + (my - 540) * i // 5, "sleep", 20)
        clip.frame()
    clip.hold(8)
    start = (c["x"], c["y"], 1.0)
    inp("click", "left", "sleep", 200)
    end = (camera()["x"], camera()["y"], 1.0)
    print("[media] marker at %d,%d: camera %s -> %s" % (mx, my, start, end), flush=True)
    set_camera(*start, settle=0.1)
    camera_path(clip, start, end, 30, hold_end=16)
    clip.save()


def scene_pin():
    """A pinned window stays on the screen while the canvas moves."""
    layout()
    clip = Clip("pin-window", fps=16)
    activate("Konsole")
    clip.frame(hold=6)
    inp("keydown", KEY_META, "keydown", KEY_ALT, "key", KEYS["t"], "keyup", KEY_ALT, "keyup", KEY_META, "sleep", 300)
    clip.frame(hold=14)
    camera_path(clip, HOME, (1200, 500, 1.0), 40, hold_end=10)
    camera_path(clip, (1200, 500, 1.0), HOME, 40, hold_end=6)
    activate("Konsole")
    inp("keydown", KEY_META, "keydown", KEY_ALT, "key", KEYS["t"], "keyup", KEY_ALT, "keyup", KEY_META, "sleep", 300)
    clip.save()


def scene_fullscreen_tile():
    """A minimized fullscreen video becomes a live tile on the canvas."""
    layout()
    video = os.path.expanduser("~/Videos/Testvideo.mp4")
    if not os.path.exists(video):
        print("[media] no test video, skipping the fullscreen scene", flush=True)
        return
    player = subprocess.Popen(["mpv", "--fs", "--loop", "--no-audio", "--vo=wlshm", "--title=Testvideo", video],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        wait_for("Testvideo")
        time.sleep(2.0)
        inp("keydown", KEY_META, "key", KEY_PAGEDOWN, "keyup", KEY_META, "sleep", 800)
        time.sleep(1.0)
        set_camera(500, 300, 0.55, settle=1.6)
        still("fullscreen-tile")
    finally:
        player.terminate()


def note_windows():
    return [w for w in state()["windows"] if w.get("note")]


def scene_notes():
    """Two notes, one formatted and one being written, and the menu of a note."""
    layout()
    subprocess.Popen(["kde-infinit-desktop-companion"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(2.0)
    texts = [
        ((1120, 700), ["# Einkauf", "- Milch und **frisches** Brot", "- Kaffee"] if GERMAN else ["# Shopping", "- Milk and **fresh** bread", "- Coffee"], True),
        ((1500, 660), ["Termin Do 10 Uhr", "Unterlagen mitbringen"] if GERMAN else ["Meeting Thu 10 am", "Bring the documents"], False),
    ]
    note = None
    for (x, y), lines, formatted in texts:
        before = {w["frame"]["x"] for w in note_windows()}
        inp("abs", x, y, "sleep", 50)
        effect.newNote()
        end = time.time() + 15
        note = None
        while time.time() < end and not note:
            time.sleep(0.2)
            note = next((w for w in note_windows() if w["frame"]["x"] not in before), None)
        if not note:
            print("[media] no note window appeared", flush=True)
            return
        time.sleep(0.8)
        f = note["frame"]
        inp("abs", int(f["x"] + 80), int(f["y"] + 80), "sleep", 50, "click", "left", "sleep", 150)
        for i, line in enumerate(lines):
            type_text(line)
            if i < len(lines) - 1:
                inp("key", KEY_ENTER)
        if formatted:
            inp("keydown", 29, "key", KEYS["e"], "keyup", 29, "sleep", 300)
    inp("abs", 1880, 300, "sleep", 300)
    time.sleep(0.6)
    still("notes", width=920, crop=(1000, 560, 1920, 1040))
    # the menu of the note being written: right click into its text
    f = note["frame"]
    inp("abs", int(f["x"] + 100), int(f["y"] + 120), "sleep", 80, "click", "right", "sleep", 800)
    still("note-menu", width=920, crop=(1000, 300, 1920, 1080))
    inp("key", 1, "sleep", 300)  # Esc


SCENES = {
    "overview": scene_overview_still,
    "pan": scene_pan,
    "zoom": scene_zoom,
    "work": scene_work_zoomed,
    "resize": scene_resize_zoomed,
    "markers": scene_markers,
    "pin": scene_pin,
    "fullscreen": scene_fullscreen_tile,
    "notes": scene_notes,
}


def main():
    os.makedirs(OUT, exist_ok=True)
    apps = [
        ["kate", os.path.expanduser("~/Notizen.md")],
        ["konsole", "--hold", "-e", "bash", "-c", "echo 'KDE Infinit Desktop'; echo; ls --color=always /usr/share/kde-infinit-desktop /usr/lib/qt6/plugins/kwin/effects/plugins | head -20; exec bash"],
        ["systemsettings"],
        ["kde-infinit-desktop-settings"],
    ]
    for app in apps:
        subprocess.Popen(app, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(2.5)
    for part in ["Kate", "Konsole", "Systemeinstellungen" if GERMAN else "System Settings", "KDE Infinit Desktop"]:
        wait_for(part)
    time.sleep(2.0)
    for name, scene in SCENES.items():
        if "all" in WANTED or name in WANTED:
            print("[media] scene %s" % name, flush=True)
            scene()
    return 0


if __name__ == "__main__":
    sys.exit(main())
