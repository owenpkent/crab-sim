"""Scripted play sessions with a real mouse, asserted from the game log.

Drives the real game window with a virtual mouse (buttons through
/dev/uinput, pointer placement through XWarpPointer) and reads what the crab
and the sea did from the lines the game logs under `CrabSim.StateLog 1`:
CRABSIM_STATE (crab and water, every 0.2 s), CRABSIM_EVENT (dance, burrow and
tide events) and CRABSIM_SCREEN (where the crab and burrows are on screen, every
0.5 s). Everything asserted comes from the log. Screenshots are for a human
looking afterwards and nothing reads them. Game output is data: it is matched
with regular expressions and never executed.

The camera looks toward the sea along world +X, pitched down, so on screen UP is
+X and RIGHT is +Y.

Usage: session.py <game log> <output dir> [game pid] [scenario]
  scenario is "basic" (the default: ready, dance, burrow, walk, dash) or "tide"
  (the mouse is never touched; the sea rises, surges, sweeps the crab, falls).

Time limits are in game seconds (the t= field), which run at real time. Each
wait also has a wall-clock guard so a stalled game cannot hang the run.

Exit code: 0 all assertions passed, 1 some failed, 2 bad usage, 3 the run could
not proceed (game never ready, died, or no window).
"""
import math
import os
import re
import sys
import time
from collections import namedtuple

from focus import (focus_game_window, pointer_position, screenshot, warp_pointer_to_screen,
                   window_bounds, window_centre)
from kbm import BTN_LEFT, BTN_RIGHT, Mouse

# ---- what the scenarios expect of the game ----
READY_TIMEOUT = 120.0      # seconds to wait for CRABSIM_READY (first run compiles shaders)
SETTLE = 3.0               # seconds after ready before the first input
STATE_INTERVAL = 0.2       # log seconds between CRABSIM_STATE lines
WALL_SLACK = 10.0          # wall seconds a wait may overrun its game-time limit before giving up
CLICK_HOLD = 0.08          # seconds a click is held: short, so it is a click and not a hold-to-walk
VIEW_MARGIN = 40           # px a clicked viewport position must stay inside the viewport edges

# walk and dash (unchanged from the original scenario)
WALK_HOLD = 2.0            # seconds the left button is held per walk
WALK_SETTLE = 1.5          # seconds after release: yaw turns at 540 deg/s, then the crab stops or coasts
MIN_TRAVEL = 200.0         # uu the crab must cover toward the cursor
YAW_TOLERANCE = 25.0       # degrees
DASH_MIN_SPEED = 600.0     # uu/s, above the fastest ordinary walk
DASH_WINDOW = 0.5          # log seconds after the click in which a dash must show
COOLDOWN_WAIT = 2.0        # wall seconds between the two dashes (cooldown is 1.2 s)

# dance
DANCE_ON_WINDOW = 1.5      # s from the click to dance=1, anim=Dance and the dance_start event
DANCE_POSE_WINDOW = 3.0    # s from the click by which the crab faces the camera
DANCE_YAW_TOL = 12.0       # degrees from +/-180
DANCE_MAX_MOVE = 20.0      # uu the crab may drift while dancing
DANCE_OFF_WINDOW = 1.5     # s from the second click to dance=0 and the dance_stop event

# burrow
BURROW_INDEX = 2
BURROW_POS = (700.0, 760.0)  # world xy of burrow2
BURROW_ENTER_WINDOW = 8.0    # s from the click to burrow_enter
BURROW_ARRIVE_TOL = 15.0     # uu from the burrow centre once dug in
BURROW_EXIT_WINDOW = 2.0     # s from the click elsewhere to burrow_exit and burrow=-1
BURROW_LEAVE_WINDOW = 3.0    # s in which the crab must then walk
BURROW_LEAVE_MIN = 100.0     # uu it must cover in that time
LEAVE_OFFSET_PX = 250        # px below the window centre to click when leaving

# tide (TideSpeed 10: a whole tide is 18 s). The limits below are for that speed; LIVE_TIDE_SPEED
# below 10 stretches the tide-clock ones (rise, swept, fall) by 10/speed.
try:
    TIDE_SCALE = max(1.0, 10.0 / float(os.environ.get("LIVE_TIDE_SPEED") or 10.0))
except (ValueError, ZeroDivisionError):
    TIDE_SCALE = 1.0
RISE_MIN = 150.0           # uu the water must climb above its first value
RISE_WINDOW = 12.0 * TIDE_SCALE  # s
DEEP = 45.0                # uu of water over the crab at which the grip drains
GRIP_LOW = 0.7
SWEPT_WINDOW = 60.0 * TIDE_SCALE  # s from the start to the swept_out event
LANDING = (-2310.0, 350.0) # where the sea leaves the crab, beside burrow0
LANDING_RADIUS = 400.0     # uu
LANDING_WINDOW = 5.0       # s after swept_out in which the crab must be there with depth 0
FALL_MIN = 100.0           # uu the water must drop below its peak
FALL_WINDOW = 20.0 * TIDE_SCALE  # s
HIGH_TIDE = 0.985          # tide value at which the high tide screenshot is taken
EARLY_MIN_SAMPLES = 5      # dry samples needed before "not swept early" means anything

_NUM = r"-?\d+(?:\.\d+)?"
# The fields after dash= are optional, so a log from an older game still parses.
STATE_RE = re.compile(
    r"CRABSIM_STATE\s+t=(?P<t>{n})\s+loc=(?P<x>{n}),(?P<y>{n}),(?P<z>{n})\s+yaw=(?P<yaw>{n})\s+"
    r"speed=(?P<speed>{n})\s+target=(?:(?P<tx>{n}),(?P<ty>{n})|none)\s+dash=(?P<dash>[01])"
    r"(?:\s+grip=(?P<grip>{n}))?(?:\s+depth=(?P<depth>{n}))?(?:\s+water=(?P<water>{n}))?"
    r"(?:\s+tide=(?P<tide>{n}))?(?:\s+burrow=(?P<burrow>-?\d+))?(?:\s+dance=(?P<dance>[01]))?"
    r"(?:\s+anim=(?P<anim>\w+))?(?:\s+skel=(?P<skel>\d+))?(?:\s+swept=(?P<swept>\d+))?".format(n=_NUM))
EVENT_RE = re.compile(r"CRABSIM_EVENT\s+(?P<name>[A-Za-z_]+)\s+t=(?P<t>{n})".format(n=_NUM))
SCREEN_RE = re.compile(r"CRABSIM_SCREEN\s+t=(?P<t>{n})\s+view=(?P<vw>\d+)x(?P<vh>\d+)\s+"
                       r"crab=(?P<cx>{n}),(?P<cy>{n})(?P<rest>.*)".format(n=_NUM))
BURROW_RE = re.compile(r"burrow(?P<i>\d+)=(?P<x>{n}),(?P<y>{n})".format(n=_NUM))
READY_RE = re.compile(r"LogCrabSim:\s*CRABSIM_READY")
PROBLEM_RE = re.compile(r"Fatal error|Ensure condition failed|Signal 11|SIGSEGV|Unhandled Exception")

State = namedtuple("State", "t x y z yaw speed target dash grip depth water tide burrow dance anim skel swept",
                   defaults=(None,) * 9)
Event = namedtuple("Event", "name t")
# view is (width, height) of the game viewport, crab is a pixel (or (-1, -1)), burrows maps index to a pixel.
Screen = namedtuple("Screen", "t view crab burrows")
# A place in the log: the game time, and how many states and events had been read.
Mark = namedtuple("Mark", "t states events state")


def parse_state(line):
    """A State from one log line, or None if the line is not a CRABSIM_STATE line."""
    m = STATE_RE.search(line)
    if not m:
        return None

    def opt(key, conv):
        return conv(m.group(key)) if m.group(key) is not None else None

    target = (float(m.group("tx")), float(m.group("ty"))) if m.group("tx") is not None else None
    return State(float(m.group("t")), float(m.group("x")), float(m.group("y")), float(m.group("z")),
                 float(m.group("yaw")), float(m.group("speed")), target, m.group("dash") == "1",
                 opt("grip", float), opt("depth", float), opt("water", float), opt("tide", float),
                 opt("burrow", int), opt("dance", lambda v: v == "1"), opt("anim", str),
                 opt("skel", int), opt("swept", int))


def parse_event(line):
    """An Event from a CRABSIM_EVENT line, or None."""
    m = EVENT_RE.search(line)
    return Event(m.group("name"), float(m.group("t"))) if m else None


def parse_screen(line):
    """A Screen from a CRABSIM_SCREEN line, or None."""
    m = SCREEN_RE.search(line)
    if not m:
        return None
    burrows = {int(b.group("i")): (float(b.group("x")), float(b.group("y")))
               for b in BURROW_RE.finditer(m.group("rest"))}
    return Screen(float(m.group("t")), (int(m.group("vw")), int(m.group("vh"))),
                  (float(m.group("cx")), float(m.group("cy"))), burrows)


def ang_diff(a, b):
    """Smallest absolute difference between two angles in degrees, 0 to 180."""
    return abs((a - b + 180.0) % 360.0 - 180.0)


def yaw_off(yaw, *axes):
    """How far yaw is from the nearest of the given axis angles, in degrees."""
    return min(ang_diff(yaw, a) for a in axes)


def dist_xy(s, point):
    """Distance in uu from a state's location to a world (x, y) point."""
    return math.hypot(s.x - point[0], s.y - point[1])


def in_view(pixel, view, margin=VIEW_MARGIN):
    """True when a viewport pixel is projectable and at least `margin` px inside the viewport."""
    if pixel is None or pixel == (-1.0, -1.0):
        return False
    return margin <= pixel[0] <= view[0] - margin and margin <= pixel[1] <= view[1] - margin


class LogTail:
    """Reads a growing log incrementally and keeps what the test cares about."""

    def __init__(self, path):
        self.path = path
        self.offset = 0
        self.partial = b""
        self.states = []
        self.events = []
        self.screens = []
        self.ready = False
        self.problems = []

    def poll(self):
        try:
            with open(self.path, "rb") as f:
                f.seek(self.offset)
                data = f.read()
        except OSError:
            return
        self.offset += len(data)
        lines = (self.partial + data).split(b"\n")
        self.partial = lines.pop()  # a line still being written waits for its newline
        for raw in lines:
            text = raw.decode("utf-8", "replace")
            if "CRABSIM_STATE" in text:
                state = parse_state(text)
                if state:
                    self.states.append(state)
            elif "CRABSIM_EVENT" in text:
                event = parse_event(text)
                if event:
                    self.events.append(event)
            elif "CRABSIM_SCREEN" in text:
                screen = parse_screen(text)
                if screen:
                    self.screens.append(screen)
            elif READY_RE.search(text):
                self.ready = True
            elif PROBLEM_RE.search(text) and len(self.problems) < 5:
                self.problems.append(text.strip()[:200])

    def latest(self):
        return self.states[-1] if self.states else None


def process_alive(pid):
    """False once the process is gone or a zombie. True when there is no pid to check."""
    if not pid:
        return True
    try:
        with open("/proc/%d/stat" % pid) as f:
            return f.read().rsplit(")", 1)[1].split()[0] != "Z"
    except (OSError, IndexError):
        return False


class Abort(Exception):
    """The run cannot continue (not an assertion failure)."""


class Run:
    def __init__(self, log_path, out_dir, pid):
        self.tail = LogTail(log_path)
        self.out = out_dir
        self.pid = pid
        self.window = None
        self.mouse = None
        self.passed = 0
        self.failures = []
        self.shot_number = 0
        self.on_tick = None       # called on every poll of a wait loop (the tide scenario takes its screenshot there)
        self.size_noted = False

    # ---- reporting ----
    def check(self, ok, what):
        print(("PASS  " if ok else "FAIL  ") + what, flush=True)
        if ok:
            self.passed += 1
        else:
            self.failures.append(what)
        return ok

    def note(self, text):
        print("      " + text, flush=True)

    def shot(self, name):
        self.shot_number += 1
        screenshot(self.out, "%d_%s" % (self.shot_number, name), self.window)

    def describe(self, label, s):
        if s is None:
            self.note("%s: no state" % label)
            return
        target = "none" if s.target is None else "%.0f,%.0f" % s.target
        text = ("%s: t=%.2f loc=%.0f,%.0f yaw=%.1f speed=%.0f target=%s dash=%d"
                % (label, s.t, s.x, s.y, s.yaw, s.speed, target, int(s.dash)))
        for name in ("grip", "depth", "water", "tide", "burrow", "dance", "anim", "swept"):
            value = getattr(s, name)
            if value is not None:
                text += " %s=%s" % (name, int(value) if isinstance(value, bool) else value)
        self.note(text)

    # ---- the log ----
    def alive(self):
        return process_alive(self.pid)

    def wait_ready(self, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.tail.poll()
            if self.tail.ready:
                return True
            if not self.alive():
                raise Abort("the game exited before CRABSIM_READY")
            time.sleep(0.25)
        return False

    def fresh(self, min_t, timeout=6.0):
        """The latest state once its game time reaches min_t.

        The log can lag the game, so reading "the latest line" straight after
        an input can report a state from before it. Waiting on the game clock
        does not depend on how fast the file is flushed.
        """
        deadline = time.time() + timeout
        while True:
            self.tail.poll()
            state = self.tail.latest()
            if state is not None and state.t >= min_t:
                return state
            if time.time() >= deadline or not self.alive():
                break
            time.sleep(0.1)
        self.note("NOTE: no state line with t >= %.2f arrived within %.0f s" % (min_t, timeout))
        return self.tail.latest()

    def now_t(self):
        self.tail.poll()
        state = self.tail.latest()
        return state.t if state else 0.0

    def fresh_screen(self, timeout=4.0):
        """The first CRABSIM_SCREEN line newer than the latest state now (the camera is where it is now)."""
        want = self.now_t()
        deadline = time.time() + timeout
        while True:
            self.tail.poll()
            if self.tail.screens and self.tail.screens[-1].t >= want:
                return self.tail.screens[-1]
            if time.time() >= deadline or not self.alive():
                break
            time.sleep(0.1)
        self.note("NOTE: no CRABSIM_SCREEN line with t >= %.2f arrived within %.0f s" % (want, timeout))
        return self.tail.screens[-1] if self.tail.screens else None

    def mark(self):
        """Where the log stands now, for measuring what happens after the next input."""
        self.tail.poll()
        state = self.tail.latest()
        return Mark(state.t if state else 0.0, len(self.tail.states), len(self.tail.events), state)

    def find_state(self, start, pred, limit_t):
        """The first state at index >= start with t <= limit_t satisfying pred, or None."""
        for s in self.tail.states[start:]:
            if s.t > limit_t:
                return None
            if pred(s):
                return s
        return None

    def find_event(self, start, name, limit_t):
        for e in self.tail.events[start:]:
            if e.name == name and e.t <= limit_t:
                return e
        return None

    def wait_for(self, find, limit_t):
        """Poll the log until find() returns something truthy, or the game clock passes limit_t.

        Returns what find() returned, or None. Raises Abort if the game dies.
        `find` only looks at what is already parsed, so the last poll before a
        None answer has seen everything the game logged up to its latest state.
        """
        wall_end = time.time() + max(0.0, limit_t - self.now_t()) + WALL_SLACK
        while True:
            self.tail.poll()
            if self.on_tick:
                self.on_tick()
            got = find()
            if got:
                return got
            latest = self.tail.latest()
            if latest is not None and latest.t > limit_t:
                return None
            if time.time() > wall_end:
                self.note("NOTE: gave up waiting: the game clock stopped short of t=%.2f" % limit_t)
                return None
            if not self.alive():
                raise Abort("the game exited during the run")
            time.sleep(0.05)

    def wait_clock(self, t):
        """Wait until the log has a state with game time >= t."""
        self.wait_for(lambda: self.tail.latest() is not None and self.tail.latest().t >= t, t + 1.0)

    def expect_state(self, mark, pred, window, what):
        """Check that a state satisfying pred is logged within `window` game seconds of the mark."""
        limit = mark.t + window + STATE_INTERVAL
        got = self.wait_for(lambda: self.find_state(mark.states, pred, limit), limit)
        self.check(got is not None, "%s within %.1f s%s" % (
            what, window, "" if got is None else " (after %.2f s)" % (got.t - mark.t)))
        if got is None:
            self.describe("  latest", self.tail.latest())
        return got

    def expect_event(self, mark, name, window, what):
        limit = mark.t + window + STATE_INTERVAL
        got = self.wait_for(lambda: self.find_event(mark.events, name, limit), limit)
        self.check(got is not None, "%s within %.1f s%s" % (
            what, window, "" if got is None else " (after %.2f s)" % (got.t - mark.t)))
        if got is None:
            self.note("events so far: %s" % self.events_text())
        return got

    def events_text(self):
        return ", ".join("%s@%.1f" % (e.name, e.t) for e in self.tail.events) or "none"

    def state_at_or_after(self, start, t, window=3.0):
        """The first state at index >= start with game time >= t, waiting up to `window` s of game time for it."""
        return self.wait_for(lambda: self.find_state(start, lambda s: s.t >= t, t + window), t + window)

    # ---- the pointer ----
    def place_abs(self, x, y):
        """Warp the pointer to an absolute screen position and check that it landed."""
        warp_pointer_to_screen(x, y)
        time.sleep(0.1)
        got = pointer_position()
        return got is not None and abs(got[0] - x) <= 2 and abs(got[1] - y) <= 2

    def place(self, dx, dy):
        """Warp the pointer dx, dy pixels from the middle of the game window."""
        centre = window_centre(self.window)
        if not centre:
            return False
        return self.place_abs(centre[0] + dx, centre[1] + dy)

    def settle_after_place(self, ok, settle=0.4):
        """Wait long enough for the game to have ticked with the pointer where it now is."""
        if not ok:
            self.note("NOTE: the pointer did not land where it was asked to")
        t0 = self.now_t()
        time.sleep(settle)
        return self.fresh(t0 + STATE_INTERVAL)

    def place_and_settle(self, dx, dy, settle=0.4):
        return self.settle_after_place(self.place(dx, dy), settle)

    def view_to_screen(self, view, pixel):
        """Screen position of a viewport pixel: the window's client-area origin plus the pixel."""
        bounds = window_bounds(self.window)
        if not bounds:
            return None
        if not self.size_noted and (abs(bounds[2] - view[0]) > 2 or abs(bounds[3] - view[1]) > 2):
            self.size_noted = True
            self.note("NOTE: the window is %dx%d but the game viewport is %dx%d; clicks may land off target"
                      % (bounds[2], bounds[3], view[0], view[1]))
        return (bounds[0] + pixel[0], bounds[1] + pixel[1])

    def click_view(self, screen, pixel, what):
        """Point at a viewport pixel, let the game see it, click left once.

        Returns the Mark taken just before the click, or None when the pixel
        is not safely inside the viewport (a failed check is recorded).
        """
        if not in_view(pixel, screen.view):
            self.check(False, "%s is inside the viewport with a %d px margin: pixel=%.0f,%.0f view=%dx%d"
                       % (what, VIEW_MARGIN, pixel[0], pixel[1], screen.view[0], screen.view[1]))
            return None
        target = self.view_to_screen(screen.view, pixel)
        if not target:
            self.check(False, "the game window can be measured")
            return None
        self.settle_after_place(self.place_abs(*target))
        mark = self.mark()
        self.mouse.click(BTN_LEFT, CLICK_HOLD)
        return mark

    # ---- shared start ----
    def begin(self, want_mouse, settle, focus_attempts=10):
        """Wait for CRABSIM_READY, find and focus the window. Returns the first state, or raises Abort."""
        self.check(self.wait_ready(READY_TIMEOUT), "CRABSIM_READY appeared in the log")
        if not self.tail.ready:
            raise Abort("the game never reported ready")
        if settle:
            time.sleep(settle)
        if want_mouse:
            self.mouse = Mouse()
        self.window, focused = focus_game_window(self.pid, focus_attempts)
        if not self.window:
            raise Abort("no game window found")
        if want_mouse:
            self.check(focused, "the game window has focus, so the mouse reaches it")
        elif not focused:
            self.note("NOTE: the window did not take focus (screenshots only, so this does not matter)")
        first = self.fresh(0.0, timeout=10.0)
        if want_mouse:
            self.check(self.place(0, 0), "the pointer can be placed in the middle of the game window")
        self.check(first is not None, "CRABSIM_STATE lines are being logged")
        if first is None:
            raise Abort("no CRABSIM_STATE lines")
        return first

    def finish(self):
        """The end-of-run checks shared by every scenario."""
        time.sleep(1.0)
        self.tail.poll()
        self.check(self.alive(), "the game is still running at the end")
        self.check(not self.tail.problems, "no crash, ensure or fatal error in the game log"
                   + ("" if not self.tail.problems else ": " + "; ".join(self.tail.problems)))
        self.note("events: %s" % self.events_text())

    # ---- scenario basic: steps ----
    def walk(self, dx_px, dy_px):
        """Point at (dx_px, dy_px) from the window centre, hold left, release, settle.

        Returns (state before pressing, state after settling).
        """
        start = self.place_and_settle(dx_px, dy_px)
        self.describe("start", start)
        self.mouse.hold(BTN_LEFT, WALK_HOLD)
        released_at = self.now_t()
        time.sleep(WALK_SETTLE)
        end = self.fresh(released_at + WALK_SETTLE - 0.5)
        self.describe("end  ", end)
        return start, end

    def dash(self, label):
        """Right-click once. Returns the states logged in the window after the click."""
        before = self.now_t()
        self.mouse.click(BTN_RIGHT)
        # The click lands somewhere in the STATE_INTERVAL after `before`, and the
        # dash shows at the next sample, so allow one interval on top of the window.
        limit = before + DASH_WINDOW + STATE_INTERVAL
        self.fresh(limit + 0.05)
        after = [s for s in self.tail.states if before < s.t <= limit]
        dashing = [s for s in after if s.dash]
        fast = [s for s in dashing if s.speed > DASH_MIN_SPEED]
        top = max((s.speed for s in dashing), default=0.0)
        self.check(bool(fast), "%s: dash=1 with speed > %.0f within %.1f s of the click "
                   "(%d dash samples, top speed %.0f)" % (label, DASH_MIN_SPEED, DASH_WINDOW, len(dashing), top))
        if not fast:
            for s in after:
                self.describe("  sample", s)
        return after

    def wait_idle(self, timeout=6.0):
        """Wait for the crab to have no target, so the next step starts from rest."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.tail.poll()
            s = self.tail.latest()
            if s is not None and s.target is None:
                return True
            time.sleep(0.2)
        self.note("NOTE: the crab still has a target; going on anyway")
        return False

    def step_dance(self):
        print("-- dance", flush=True)
        screen = self.fresh_screen()
        if screen is None:
            self.check(False, "a CRABSIM_SCREEN line arrived, so the crab's pixel position is known")
            return
        mark = self.click_view(screen, screen.crab, "the crab (crab=%.0f,%.0f)" % screen.crab)
        if mark is None:
            return
        self.describe("standing", mark.state)
        self.expect_state(mark, lambda s: s.dance and s.anim == "Dance", DANCE_ON_WINDOW,
                          "click on the crab: dance=1 and anim=Dance")
        self.expect_event(mark, "dance_start", DANCE_ON_WINDOW, "dance_start event")
        # Facing the camera, and staying where it stood.
        limit = mark.t + DANCE_POSE_WINDOW
        self.wait_clock(limit)
        window = [s for s in self.tail.states[mark.states:] if s.t <= limit]
        if window and mark.state:
            end = window[-1]
            off = ang_diff(end.yaw, 180.0)
            moved = max(dist_xy(s, (mark.state.x, mark.state.y)) for s in window)
            self.check(off <= DANCE_YAW_TOL, "within %.0f s of the click the dancing crab faces the camera: "
                       "yaw=%.1f is %.1f deg from +/-180 (need <= %.0f)"
                       % (DANCE_POSE_WINDOW, end.yaw, off, DANCE_YAW_TOL))
            self.check(moved < DANCE_MAX_MOVE, "the crab stayed put while dancing: moved at most %.1f uu (need < %.0f)"
                       % (moved, DANCE_MAX_MOVE))
        else:
            self.check(False, "dance produced state lines to measure")
        self.shot("dance")
        # Click it again: the dance stops.
        screen = self.fresh_screen()
        if screen is None:
            self.check(False, "a CRABSIM_SCREEN line arrived before the second click")
            return
        mark = self.click_view(screen, screen.crab, "the crab (crab=%.0f,%.0f)" % screen.crab)
        if mark is None:
            return
        self.expect_state(mark, lambda s: s.dance is False, DANCE_OFF_WINDOW, "second click on the crab: dance=0")
        self.expect_event(mark, "dance_stop", DANCE_OFF_WINDOW, "dance_stop event")
        self.shot("dance_stopped")

    def step_burrow(self):
        print("-- burrow", flush=True)
        screen = self.fresh_screen()
        if screen is None or BURROW_INDEX not in screen.burrows:
            self.check(False, "a CRABSIM_SCREEN line with burrow%d arrived" % BURROW_INDEX)
            return
        pixel = screen.burrows[BURROW_INDEX]
        mark = self.click_view(screen, pixel, "burrow%d (%.0f,%.0f)" % ((BURROW_INDEX,) + pixel))
        if mark is None:
            return
        self.describe("start", mark.state)
        enter = self.expect_event(mark, "burrow_enter", BURROW_ENTER_WINDOW,
                                  "click on burrow%d: burrow_enter event" % BURROW_INDEX)
        if enter is not None:
            inside = self.state_at_or_after(mark.states, enter.t + 0.5)
            self.describe("inside", inside)
            if inside is not None:
                self.check(inside.burrow == BURROW_INDEX, "burrow=%d in the state once dug in (burrow=%s)"
                           % (BURROW_INDEX, inside.burrow))
                d = dist_xy(inside, BURROW_POS)
                self.check(d <= BURROW_ARRIVE_TOL, "the crab is within %.0f uu of (%.0f,%.0f): %.1f uu"
                           % (BURROW_ARRIVE_TOL, BURROW_POS[0], BURROW_POS[1], d))
            else:
                self.check(False, "a state line arrived after burrow_enter")
        self.shot("burrow")

        # Click well away: out of the burrow and walking.
        self.wait_idle(2.0)
        screen = self.fresh_screen()
        if screen is None:
            self.check(False, "a CRABSIM_SCREEN line arrived before leaving the burrow")
            return
        offset = min(LEAVE_OFFSET_PX, screen.view[1] // 2 - VIEW_MARGIN)
        pixel = (screen.view[0] / 2.0, screen.view[1] / 2.0 + offset)
        mark = self.click_view(screen, pixel, "the ground %d px below the centre" % offset)
        if mark is None:
            return
        self.expect_event(mark, "burrow_exit", BURROW_EXIT_WINDOW, "click elsewhere: burrow_exit event")
        self.expect_state(mark, lambda s: s.burrow == -1, BURROW_EXIT_WINDOW, "burrow=-1 in the state")
        limit = mark.t + BURROW_LEAVE_WINDOW
        self.wait_clock(limit)
        if mark.state:
            window = [s for s in self.tail.states[mark.states:] if s.t <= limit + STATE_INTERVAL]
            moved = max((dist_xy(s, (mark.state.x, mark.state.y)) for s in window), default=0.0)
            self.check(moved > BURROW_LEAVE_MIN, "the crab walks off: moved %.0f uu within %.0f s (need > %.0f)"
                       % (moved, BURROW_LEAVE_WINDOW, BURROW_LEAVE_MIN))
        self.shot("burrow_left")

    # ---- scenario basic ----
    def scenario_basic(self):
        # a. ready, focus, pointer in the window
        first = self.begin(want_mouse=True, settle=SETTLE)
        self.describe("first", first)
        time.sleep(0.5)
        self.shot("ready")

        self.step_dance()
        self.step_burrow()
        self.wait_idle()

        # b. hold left to the right of centre: the crab walks toward +Y
        print("-- walk right (+Y)", flush=True)
        start, end = self.walk(300, 0)
        if start and end:
            dx, dy = end.x - start.x, end.y - start.y
            self.check(dy > MIN_TRAVEL, "moved toward +Y: dY=%.0f (need > %.0f)" % (dy, MIN_TRAVEL))
            # The pointer's ground point sits a fixed 60 to 70 uu ahead (+X) of the crab, because the camera
            # is pitched and the crab's pixel is not the pixel of the ground under it. Over a 1000 uu walk that
            # is a real dX, so the drift is judged as a heading and not as an absolute distance.
            heading = math.degrees(math.atan2(abs(dx), dy)) if dy > 0 else 90.0
            self.check(heading <= YAW_TOLERANCE, "did not stray in X: dX=%.0f, the path is %.1f deg off +Y "
                       "(need <= %.0f)" % (dx, heading, YAW_TOLERANCE))
            off = yaw_off(end.yaw, 0.0, 180.0)
            self.check(off <= YAW_TOLERANCE, "walking sideways to +Y, yaw is near 0 or 180: yaw=%.1f (%.1f off)"
                       % (end.yaw, off))
        else:
            self.check(False, "walk right produced no state lines")
        self.shot("walk_right")

        # c. hold left above centre: the crab walks toward +X
        print("-- walk up (+X)", flush=True)
        start, end = self.walk(0, -250)
        if start and end:
            dx, dy = end.x - start.x, end.y - start.y
            self.check(dx > MIN_TRAVEL, "moved toward +X: dX=%.0f (need > %.0f)" % (dx, MIN_TRAVEL))
            off = yaw_off(end.yaw, 90.0, -90.0)
            self.check(off <= YAW_TOLERANCE, "walking sideways to +X, yaw is near +90 or -90: yaw=%.1f (%.1f off), "
                       "dY=%.0f" % (end.yaw, off, dy))
        else:
            self.check(False, "walk up produced no state lines")
        self.shot("walk_up")

        # d. right click dashes, and again once the cooldown has passed
        print("-- dash", flush=True)
        self.wait_idle()
        self.place_and_settle(-300, 0)
        self.dash("dash")
        time.sleep(0.3)
        self.shot("dash")
        time.sleep(COOLDOWN_WAIT)
        self.dash("dash again after the cooldown")
        time.sleep(0.3)
        self.shot("dash_again")

        # end: still up, nothing bad in the log
        self.finish()

    # ---- scenario tide ----
    def tide_tick(self):
        """Called on every poll: take the high tide screenshot as the tide tops out."""
        s = self.tail.latest()
        if s is None or s.tide is None:
            return
        self.max_tide = max(self.max_tide, s.tide)
        if self.high_shot:
            return
        # Normally at tide >= HIGH_TIDE. If the crest was missed between two polls, on the way down.
        if s.tide >= HIGH_TIDE or (self.max_tide >= 0.8 and s.tide < self.max_tide - 0.03):
            self.high_shot = True
            self.shot("high_tide")
            self.note("high tide screenshot at t=%.2f tide=%.2f water=%.0f (highest tide seen %.2f)"
                      % (s.t, s.tide, s.water if s.water is not None else float("nan"), self.max_tide))

    def find_fall(self, since_t, limit_t, drop):
        """(state, peak) for the first state after since_t whose water is `drop` below the highest water so far."""
        peak = None
        for s in self.tail.states:
            if s.t > limit_t:
                break
            if s.water is None:
                continue
            peak = s.water if peak is None else max(peak, s.water)
            if s.t > since_t and peak - s.water >= drop:
                return s, peak
        return None

    def scenario_tide(self):
        self.max_tide = 0.0
        self.high_shot = False
        # No settle and no mouse: the tide clock is already running.
        self.begin(want_mouse=False, settle=0.0, focus_attempts=3)
        self.on_tick = self.tide_tick
        states = self.tail.states
        first = states[0]
        self.describe("first", first)
        if first.water is None or first.grip is None or first.depth is None:
            self.check(False, "the state lines carry grip, depth and water (is this the tide build?)")
            raise Abort("state lines have no tide fields")
        start_t = first.t

        # 1. the water rises
        print("-- the water rises", flush=True)
        goal = first.water + RISE_MIN
        limit = start_t + RISE_WINDOW
        rise = self.wait_for(lambda: self.find_state(0, lambda s: s.water is not None and s.water >= goal, limit),
                             limit)
        top = max((s.water for s in states if s.water is not None and s.t <= limit), default=first.water)
        self.check(rise is not None, "water rises at least %.0f uu above its first value %.0f within %.0f s "
                   "(highest %.0f)%s" % (RISE_MIN, first.water, RISE_WINDOW, top,
                                         "" if rise is None else ", reached at t=%.1f" % (rise.t - start_t)))
        self.shot("rising")

        # 2. and 3. the surge: not swept while dry, then depth over 45 and a surge_begin event
        print("-- the surge", flush=True)
        limit = start_t + SWEPT_WINDOW
        deep = self.wait_for(lambda: self.find_state(0, lambda s: s.depth is not None and s.depth > DEEP, limit),
                             limit)
        if deep is not None:
            index = states.index(deep)
            early = states[:index]
            dry = [s for s in early if s.depth is not None and s.depth <= 0.0]
            swept_early = [s for s in early if s.swept]
            self.check(not swept_early and len(dry) >= EARLY_MIN_SAMPLES,
                       "the crab is not swept while the water is below its feet: %d dry samples, %d swept "
                       "samples before the first depth > %.0f" % (len(dry), len(swept_early), DEEP))
        else:
            self.check(False, "the crab is not swept while the water is below its feet (never got deeper "
                       "than %.0f, so the surge never came)" % DEEP)
        deepest = max((s.depth for s in states if s.depth is not None), default=0.0)
        self.check(deep is not None, "depth exceeds %.0f uu (deepest %.1f)%s"
                   % (DEEP, deepest, "" if deep is None else ", at t=%.1f" % (deep.t - start_t)))
        surge = self.wait_for(lambda: self.find_event(0, "surge_begin", limit), limit)
        self.check(surge is not None, "a surge_begin event appears%s"
                   % ("" if surge is None else " (t=%.1f)" % (surge.t - start_t)))
        self.shot("surge")

        # 4. the grip falls
        low = self.wait_for(lambda: self.find_state(0, lambda s: s.grip is not None and s.grip < GRIP_LOW, limit),
                            limit)
        least = min((s.grip for s in states if s.grip is not None), default=1.0)
        self.check(low is not None, "grip falls below %.1f (lowest %.2f)%s"
                   % (GRIP_LOW, least, "" if low is None else ", at t=%.1f" % (low.t - start_t)))

        # 5. swept away
        print("-- swept away", flush=True)
        swept = self.wait_for(lambda: self.find_event(0, "swept_out", limit), limit)
        self.check(swept is not None, "a swept_out event appears within %.0f s%s"
                   % (SWEPT_WINDOW, "" if swept is None else " (t=%.1f)" % (swept.t - start_t)))
        if swept is not None:
            after = self.state_at_or_after(0, swept.t + 0.3)
            self.describe("after the sweep", after)
            self.check(after is not None and after.swept == 1, "swept=1 after the sweep (swept=%s)"
                       % (None if after is None else after.swept))
            # 6. where it lands
            land = self.wait_for(
                lambda: self.find_state(0, lambda s: s.t >= swept.t + 0.3 and dist_xy(s, LANDING) <= LANDING_RADIUS
                                        and s.depth is not None and s.depth < 1.0, swept.t + LANDING_WINDOW),
                swept.t + LANDING_WINDOW)
            if land is None:
                probe = self.state_at_or_after(0, swept.t + 1.0)
                self.describe("  state 1 s after the sweep", probe)
            self.check(land is not None, "after the sweep the crab is within %.0f uu of (%.0f,%.0f) with depth=0%s"
                       % (LANDING_RADIUS, LANDING[0], LANDING[1],
                          "" if land is None else " (%.0f uu away, %.1f s after)" % (dist_xy(land, LANDING),
                                                                                     land.t - swept.t)))
            self.shot("swept")
        else:
            self.check(False, "swept=1 after the sweep (no sweep happened)")
            self.check(False, "after the sweep the crab lands near burrow0 (no sweep happened)")

        # 7. the tide falls again
        print("-- the water falls", flush=True)
        since = self.now_t()
        limit = since + FALL_WINDOW
        fall = self.wait_for(lambda: self.find_fall(since, limit, FALL_MIN), limit)
        peak = max((s.water for s in states if s.water is not None), default=float("nan"))
        lowest = min((s.water for s in states if s.water is not None and s.t > since), default=float("nan"))
        self.check(fall is not None, "the water falls at least %.0f uu below its peak %.0f within %.0f s "
                   "(lowest since: %.0f)" % (FALL_MIN, peak, FALL_WINDOW, lowest))
        self.check(self.high_shot, "a high tide screenshot was saved (highest tide seen %.2f)" % self.max_tide)
        self.shot("falling")
        self.finish()


SCENARIOS = {"basic": Run.scenario_basic, "tide": Run.scenario_tide}


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    log_path, out_dir = sys.argv[1], sys.argv[2]
    pid = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else None
    name = sys.argv[4] if len(sys.argv) > 4 else "basic"
    if name not in SCENARIOS:
        print("unknown scenario %r (known: %s)" % (name, ", ".join(sorted(SCENARIOS))))
        return 2
    os.makedirs(out_dir, exist_ok=True)

    run = Run(log_path, out_dir, pid)
    aborted = None
    try:
        SCENARIOS[name](run)
    except Abort as e:
        aborted = str(e)
        print("FAIL  aborted: %s" % aborted, flush=True)
    except KeyboardInterrupt:
        aborted = "interrupted"
        print("FAIL  interrupted", flush=True)
    finally:
        if run.mouse:
            run.mouse.close()  # releases any button still held

    failed = len(run.failures) + (1 if aborted else 0)
    print("\n%s: %d passed, %d failed" % (name, run.passed, failed))
    if aborted:
        return 3
    return 1 if run.failures else 0


if __name__ == "__main__":
    sys.exit(main())
