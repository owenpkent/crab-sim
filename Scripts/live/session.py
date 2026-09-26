"""One scripted play session with a real mouse, asserted from the game log.

Drives the real game window with a virtual mouse (buttons through
/dev/uinput, pointer placement through XWarpPointer) and reads what the crab
did from the CRABSIM_STATE lines the pawn logs under `CrabSim.StateLog 1`.
Positions, yaw and speed come from the log only. Screenshots are for a human
looking afterwards and nothing reads them.

The camera looks along world +X, so on screen UP is +X and RIGHT is +Y.

Usage: session.py <game log> <output dir> [game pid]

Exit code: 0 all assertions passed, 1 some failed, 3 the run could not
proceed (game never ready, died, or no window).
"""
import os
import re
import sys
import time
from collections import namedtuple

from focus import (focus_game_window, pointer_position, screenshot, warp_pointer_to_screen,
                   window_centre)
from kbm import BTN_LEFT, BTN_RIGHT, Mouse

# ---- what the scenario expects of the game ----
READY_TIMEOUT = 120.0      # seconds to wait for CRABSIM_READY (first run compiles shaders)
SETTLE = 3.0               # seconds after ready before the first input
WALK_HOLD = 2.0            # seconds the left button is held per walk
WALK_SETTLE = 1.5          # seconds after release: yaw turns at 540 deg/s, then the crab stops or coasts
MIN_TRAVEL = 200.0         # uu the crab must cover toward the cursor
MAX_DRIFT = 150.0          # uu it may stray across that axis
YAW_TOLERANCE = 25.0       # degrees
DASH_MIN_SPEED = 600.0     # uu/s, above the fastest ordinary walk
DASH_WINDOW = 0.5          # log seconds after the click in which a dash must show
STATE_INTERVAL = 0.2       # log seconds between CRABSIM_STATE lines
COOLDOWN_WAIT = 2.0        # wall seconds between the two dashes (cooldown is 1.2 s)

_NUM = r"-?\d+(?:\.\d+)?"
STATE_RE = re.compile(
    r"CRABSIM_STATE\s+t=(?P<t>{n})\s+loc=(?P<x>{n}),(?P<y>{n}),(?P<z>{n})\s+yaw=(?P<yaw>{n})\s+"
    r"speed=(?P<speed>{n})\s+target=(?:(?P<tx>{n}),(?P<ty>{n})|none)\s+dash=(?P<dash>[01])".format(n=_NUM))
READY_RE = re.compile(r"LogCrabSim:\s*CRABSIM_READY")
PROBLEM_RE = re.compile(r"Fatal error|Ensure condition failed|Signal 11|SIGSEGV|Unhandled Exception")

State = namedtuple("State", "t x y z yaw speed target dash")


def parse_state(line):
    """A State from one log line, or None if the line is not a CRABSIM_STATE line."""
    m = STATE_RE.search(line)
    if not m:
        return None
    target = (float(m.group("tx")), float(m.group("ty"))) if m.group("tx") is not None else None
    return State(float(m.group("t")), float(m.group("x")), float(m.group("y")), float(m.group("z")),
                 float(m.group("yaw")), float(m.group("speed")), target, m.group("dash") == "1")


def ang_diff(a, b):
    """Smallest absolute difference between two angles in degrees, 0 to 180."""
    return abs((a - b + 180.0) % 360.0 - 180.0)


def yaw_off(yaw, *axes):
    """How far yaw is from the nearest of the given axis angles, in degrees."""
    return min(ang_diff(yaw, a) for a in axes)


class LogTail:
    """Reads a growing log incrementally and keeps what the test cares about."""

    def __init__(self, path):
        self.path = path
        self.offset = 0
        self.partial = b""
        self.states = []
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
            state = parse_state(text) if "CRABSIM_STATE" in text else None
            if state:
                self.states.append(state)
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

    # ---- the pointer ----
    def place(self, dx, dy):
        """Warp the pointer dx, dy pixels from the middle of the game window."""
        centre = window_centre(self.window)
        if not centre:
            return False
        want = (centre[0] + dx, centre[1] + dy)
        warp_pointer_to_screen(*want)
        time.sleep(0.1)
        got = pointer_position()
        return got is not None and abs(got[0] - want[0]) <= 2 and abs(got[1] - want[1]) <= 2

    def place_and_settle(self, dx, dy, settle=0.4):
        """Place the pointer, then wait long enough for the game to have ticked with it there."""
        if not self.place(dx, dy):
            self.note("NOTE: the pointer did not land where it was asked to")
        t0 = self.now_t()
        time.sleep(settle)
        return self.fresh(t0 + STATE_INTERVAL)

    # ---- scenario steps ----
    def describe(self, label, s):
        if s is None:
            self.note("%s: no state" % label)
            return
        target = "none" if s.target is None else "%.0f,%.0f" % s.target
        self.note("%s: t=%.2f loc=%.0f,%.0f yaw=%.1f speed=%.0f target=%s dash=%d"
                  % (label, s.t, s.x, s.y, s.yaw, s.speed, target, int(s.dash)))

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
        """Wait for the crab to have no target, so the dash starts from rest."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.tail.poll()
            s = self.tail.latest()
            if s is not None and s.target is None:
                return True
            time.sleep(0.2)
        self.note("NOTE: the crab still has a target; dashing anyway")
        return False

    # ---- the scenario ----
    def scenario(self):
        # a. ready, focus, pointer in the window
        self.check(self.wait_ready(READY_TIMEOUT), "CRABSIM_READY appeared in the log")
        if not self.tail.ready:
            raise Abort("the game never reported ready")
        time.sleep(SETTLE)
        self.mouse = Mouse()
        self.window, focused = focus_game_window(self.pid)
        if not self.window:
            raise Abort("no game window found")
        self.check(focused, "the game window has focus, so the mouse reaches it")
        self.check(self.place(0, 0), "the pointer can be placed in the middle of the game window")
        first = self.fresh(0.0, timeout=10.0)
        self.check(first is not None, "CRABSIM_STATE lines are being logged")
        if first is None:
            raise Abort("no CRABSIM_STATE lines")
        self.describe("first", first)
        time.sleep(0.5)
        self.shot("ready")

        # b. hold left to the right of centre: the crab walks toward +Y
        print("-- walk right (+Y)", flush=True)
        start, end = self.walk(300, 0)
        if start and end:
            dx, dy = end.x - start.x, end.y - start.y
            self.check(dy > MIN_TRAVEL, "moved toward +Y: dY=%.0f (need > %.0f)" % (dy, MIN_TRAVEL))
            self.check(abs(dx) < MAX_DRIFT, "did not stray in X: dX=%.0f (need |dX| < %.0f)" % (dx, MAX_DRIFT))
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
        time.sleep(1.0)
        self.tail.poll()
        self.check(self.alive(), "the game is still running at the end")
        self.check(not self.tail.problems, "no crash, ensure or fatal error in the game log"
                   + ("" if not self.tail.problems else ": " + "; ".join(self.tail.problems)))


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 3
    log_path, out_dir = sys.argv[1], sys.argv[2]
    pid = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else None
    os.makedirs(out_dir, exist_ok=True)

    run = Run(log_path, out_dir, pid)
    aborted = None
    try:
        run.scenario()
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
    print("\n%d passed, %d failed" % (run.passed, failed))
    if aborted:
        return 3
    return 1 if run.failures else 0


if __name__ == "__main__":
    sys.exit(main())
