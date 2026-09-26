"""Find the game window, focus it, place the pointer, and screenshot it.

Unreal routes input to the focused window, so a live test run from a terminal
can look like a game that ignores the mouse when all that happened is that the
desktop kept focus on the terminal. That failure is silent and comes and goes
with window placement, so the test asks for focus instead of hoping.

ctypes against libX11 rather than xdotool or wmctrl: the live test already
needs X11, and this adds nothing to install. Window lookup and screenshots
shell out to xwininfo, xprop, xwd and ffmpeg, which are already here.
"""
import contextlib
import ctypes
import ctypes.util
import os
import re
import subprocess
import time

SubstructureNotifyMask = 1 << 19
SubstructureRedirectMask = 1 << 20
ClientMessage = 33
RevertToParent = 2
CurrentTime = 0


class _XClientMessageEvent(ctypes.Structure):
    _fields_ = [
        ("type", ctypes.c_int),
        ("serial", ctypes.c_ulong),
        ("send_event", ctypes.c_int),
        ("display", ctypes.c_void_p),
        ("window", ctypes.c_ulong),
        ("message_type", ctypes.c_ulong),
        ("format", ctypes.c_int),
        ("data", ctypes.c_long * 5),
    ]


class _XEvent(ctypes.Union):
    _fields_ = [("type", ctypes.c_int), ("xclient", _XClientMessageEvent), ("pad", ctypes.c_long * 24)]


@contextlib.contextmanager
def _display():
    """Yield (libX11, Display*), or (None, None) when X11 cannot be reached."""
    x11, display = None, None
    path = ctypes.util.find_library("X11")
    if path:
        try:
            x11 = ctypes.cdll.LoadLibrary(path)
        except OSError:
            x11 = None
    if x11 is not None:
        x11.XOpenDisplay.restype = ctypes.c_void_p
        x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
        display = x11.XOpenDisplay(None)
    if not display:
        yield None, None
        return
    try:
        x11.XDefaultRootWindow.restype = ctypes.c_ulong
        x11.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
        x11.XFlush.argtypes = [ctypes.c_void_p]
        x11.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
        yield x11, display
    finally:
        x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
        x11.XCloseDisplay(display)


def _run(cmd, timeout=10):
    """stdout of a command, or "" when it is missing, fails or hangs."""
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout).stdout
    except (OSError, subprocess.SubprocessError):
        return ""


def _window_pid(window_id):
    match = re.search(r"=\s*(\d+)", _run(["xprop", "-id", hex(window_id), "_NET_WM_PID"]))
    return int(match.group(1)) if match else None


def game_window_id(pid=None):
    """The game's X window id, or None.

    With `pid`, prefers the window whose _NET_WM_PID is that process, so a
    second CrabSim window (an editor, a play session) is never picked by
    mistake. Without a match on pid, or without a pid, falls back to a window
    of the UnrealEditor class or a title starting "CrabSim", the biggest first.
    Tiny helper windows are ignored.
    """
    candidates = []
    for line in _run(["xwininfo", "-root", "-tree"]).splitlines():
        if "UnrealEditor" not in line and '"CrabSim' not in line:
            continue
        try:
            wid = int(line.split()[0], 16)
        except (ValueError, IndexError):
            continue
        size = re.search(r"\s(\d+)x(\d+)\+", line)
        width, height = (int(size.group(1)), int(size.group(2))) if size else (0, 0)
        if width < 400 or height < 300:
            continue
        candidates.append((wid, '"CrabSim' in line, width * height))
    if pid:
        for wid, _, _ in candidates:
            if _window_pid(wid) == pid:
                return wid
    candidates.sort(key=lambda c: (c[1], c[2]), reverse=True)
    return candidates[0][0] if candidates else None


def active_window_id():
    """The window the desktop currently considers active, or None."""
    for token in _run(["xprop", "-root", "_NET_ACTIVE_WINDOW"]).split():
        if token.startswith("0x"):
            try:
                return int(token, 16)
            except ValueError:
                return None
    return None


def window_bounds(window_id):
    """(left, top, width, height) of a window in screen pixels, or None."""
    fields = {}
    for line in _run(["xwininfo", "-id", hex(window_id)]).splitlines():
        text = line.strip()
        for key, name in (("Absolute upper-left X:", "left"), ("Absolute upper-left Y:", "top"),
                          ("Width:", "width"), ("Height:", "height")):
            if text.startswith(key):
                fields[name] = int(text.split(":")[1])
    if len(fields) != 4:
        return None
    return (fields["left"], fields["top"], fields["width"], fields["height"])


def window_centre(window_id):
    """(x, y) of the middle of a window in screen pixels, or None."""
    bounds = window_bounds(window_id)
    if not bounds:
        return None
    left, top, width, height = bounds
    return (left + width // 2, top + height // 2)


def pointer_position():
    """Where the X pointer is, in screen pixels, or None."""
    with _display() as (x11, display):
        if not x11:
            return None
        root_return, child_return = ctypes.c_ulong(), ctypes.c_ulong()
        root_x, root_y, win_x, win_y = (ctypes.c_int() for _ in range(4))
        mask = ctypes.c_uint()
        x11.XQueryPointer.argtypes = [
            ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_uint)]
        x11.XQueryPointer(display, x11.XDefaultRootWindow(display), ctypes.byref(root_return),
                          ctypes.byref(child_return), ctypes.byref(root_x), ctypes.byref(root_y),
                          ctypes.byref(win_x), ctypes.byref(win_y), ctypes.byref(mask))
        return (root_x.value, root_y.value)


def warp_pointer_to_screen(x, y):
    """Put the pointer at an absolute screen position.

    XWarpPointer treats a destination window of None as "move by this offset",
    so screen coordinates need the root window passed explicitly. Getting that
    wrong moves the pointer somewhere else entirely and the mistake is silent.
    Unlike a relative uinput move this is exact: no pointer acceleration.
    """
    with _display() as (x11, display):
        if not x11:
            return False
        x11.XWarpPointer.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong,
                                     ctypes.c_int, ctypes.c_int, ctypes.c_uint, ctypes.c_uint,
                                     ctypes.c_int, ctypes.c_int]
        x11.XWarpPointer(display, 0, x11.XDefaultRootWindow(display), 0, 0, 0, 0, int(x), int(y))
        x11.XSync(display, 0)
    return True


def focus(window_id):
    """Raise the window and ask for focus, both the polite way and the blunt one."""
    with _display() as (x11, display):
        if not x11:
            return False
        root = x11.XDefaultRootWindow(display)

        x11.XInternAtom.restype = ctypes.c_ulong
        x11.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        atom = x11.XInternAtom(display, b"_NET_ACTIVE_WINDOW", False)

        x11.XRaiseWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        x11.XRaiseWindow(display, ctypes.c_ulong(window_id))

        # The EWMH request a pager would send. Source indication 2 (pager) is
        # what gets past a window manager's focus-stealing prevention.
        event = _XEvent()
        event.xclient.type = ClientMessage
        event.xclient.window = ctypes.c_ulong(window_id)
        event.xclient.message_type = ctypes.c_ulong(atom)
        event.xclient.format = 32
        event.xclient.data[0] = 2
        event.xclient.data[1] = CurrentTime
        x11.XSendEvent.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_long,
                                   ctypes.POINTER(_XEvent)]
        x11.XSendEvent(display, ctypes.c_ulong(root), False,
                       ctypes.c_long(SubstructureNotifyMask | SubstructureRedirectMask), ctypes.byref(event))

        # And directly, for a window manager that ignores the request above.
        x11.XSetInputFocus.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
        x11.XSetInputFocus(display, ctypes.c_ulong(window_id), RevertToParent, CurrentTime)
        x11.XSync(display, 0)
    return True


def focus_game_window(pid=None, attempts=10):
    """Focus the game window, retrying while the desktop hands focus back.

    Returns (window id, focused) so the caller can report it. A window id of
    None means the window was never found, which is a different failure.
    """
    window = None
    for _ in range(attempts):
        window = window or game_window_id(pid)
        if not window:
            time.sleep(1.0)
            continue
        focus(window)
        time.sleep(0.6)
        if active_window_id() == window:
            return window, True
    return window, False


def screenshot(out_dir, name, window_id):
    """Capture the game window to <out_dir>/<name>.png. Best effort: never raises.

    Needs xwd and ffmpeg. For a human looking at the run afterwards; nothing
    in the test reads pixels.
    """
    if not window_id:
        return False
    xwd = os.path.join(out_dir, name + ".xwd")
    try:
        subprocess.run(["xwd", "-silent", "-id", hex(window_id), "-out", xwd], check=True, timeout=15)
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", xwd, os.path.join(out_dir, name + ".png")],
                       check=True, timeout=30)
        return True
    except (OSError, subprocess.SubprocessError):
        return False
    finally:
        try:
            os.remove(xwd)
        except OSError:
            pass
