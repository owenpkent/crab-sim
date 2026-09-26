"""A scripted tour of the real game, for the screen recorder (Scripts/record.sh).

Drives the real window with the same virtual mouse and window helpers as
session.py (buttons through /dev/uinput, pointer placement through
XWarpPointer, crab and burrow pixels from the CRABSIM_SCREEN log lines), but for
a person to watch: real speed, and the pointer glides between targets on
eased curves with small pauses instead of jumping. Nothing here is a test. The
checks it prints only say whether each beat happened.

The tour, in order: the beach at low tide; scuttle (hold left, steer the
cursor round the crab); dance (click the crab, click it again); dash (right
click, twice); burrow (click it, the crab walks in and digs in, click
elsewhere to come out); then the crab waves at the incoming tide until the
surge stops the dance, the surge takes its grip and the sea sweeps it out to
the dunes.

Usage: tour.py <game log> <output dir> [game pid] [--sync]
  --sync   work with record.sh: once the game is ready and the window is
           focused, write <output dir>/window.txt ("<id> <left> <top> <width>
           <height>") and wait for <output dir>/go, whose first field is the
           unix time the video starts. Without it the tour just starts.

Writes <output dir>/events.txt: what the tour did and what the game logged,
each with its time in the video ("video=12.3 ..."). The game must run with
CrabSim.StateLog 1 (the tour reads its log) and CrabSim.TideSpeed set so the
whole rise fits (record.sh does both).

Environment: TOUR_HOLD (seconds kept after the sweep, default 3.5),
TOUR_TIDE_CAP (seconds to wait for the sweep after coming out of the burrow,
default 90).

Exit code: 0 every beat happened, 1 some did not, 2 bad usage, 3 the run could
not proceed (game never ready, died, or no window).
"""
import calendar
import math
import os
import re
import sys
import time

from focus import pointer_position, warp_pointer_to_screen, window_bounds
from kbm import BTN_LEFT, BTN_RIGHT
from session import Abort, Run, CLICK_HOLD, SETTLE, in_view

# ---- pacing (seconds) ----
# The tide clock starts with the game, about 6 s before the video, and burrow2 floods
# about 43 s into the video at TideSpeed 1, so everything up to the burrow is kept brisk.
GO_TIMEOUT = 60.0          # how long to wait for record.sh to start the video
INTRO = 1.4                # the beach and the crab, before anything moves
GLIDE_HZ = 60.0            # pointer updates per second while gliding
SCUTTLE_LEAD = 1.2         # cursor travels to the left of the crab
SCUTTLE_STEER = 3.8        # cursor sweeps round the crab with the button held
SCUTTLE_SETTLE = 1.0      # after release the crab stops
DASH_GAP = 1.3             # between the two dashes (the cooldown is 1.2 s)
BURROW_DWELL = 3.0         # dug in, before the click that brings the crab out
MOVE_PAUSE = 0.4           # the cursor rests on a target this long before the click
HOLD = float(os.environ.get("TOUR_HOLD") or 3.5)
TIDE_CAP = float(os.environ.get("TOUR_TIDE_CAP") or 90.0)

# ---- where the cursor goes, in viewport pixels from the crab ----
# The cursor's path round the crab while the button is held: left, over the top, right, and to a
# rest point right of it. The crab walks toward the cursor's ground point, so it ends up there.
SCUTTLE_PATH = [(-200, 20), (-20, -110), (210, -30), (240, 30)]
DASH_OFFSET = 330          # px left, then right, of the crab
BURROW_INDEX = 2
BURROW_MARGIN = 90         # a burrow this close to the viewport edge is walked toward first
EXIT_CLICK = (0, 300)       # from the middle of the view: straight down the screen, up the beach and away from the sea and the creek
REST_SPOT = (300, 200)     # where the cursor waits for the tide, from the crab

LOG_TIME_RE = re.compile(r"^\[(\d{4})\.(\d\d)\.(\d\d)-(\d\d)\.(\d\d)\.(\d\d):(\d{3})\]")
LOG_EVENT_RE = re.compile(r"CRABSIM_EVENT\s+(\w+)")


def ease(u):
    """Smootherstep: 0 to 1 with no jerk at either end."""
    u = min(1.0, max(0.0, u))
    return u * u * u * (u * (6.0 * u - 15.0) + 10.0)


def spline(points, per_segment=24):
    """A smooth curve through the points (Catmull-Rom), as a dense list of (x, y)."""
    if len(points) < 3:
        return list(points)
    pts = [points[0]] + list(points) + [points[-1]]
    out = [points[0]]
    for i in range(1, len(pts) - 2):
        p0, p1, p2, p3 = pts[i - 1], pts[i], pts[i + 1], pts[i + 2]
        for k in range(1, per_segment + 1):
            t = k / per_segment
            t2, t3 = t * t, t * t * t
            out.append(tuple(0.5 * ((2 * p1[j]) + (-p0[j] + p2[j]) * t + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t2
                                    + (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t3) for j in (0, 1)))
    return out


class Route:
    """A polyline the pointer follows: the point a fraction of the way along it, by length."""

    def __init__(self, points):
        self.points = list(points)
        self.lengths = [math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(self.points, self.points[1:])]
        self.total = sum(self.lengths)

    def at(self, fraction):
        if fraction <= 0.0 or self.total <= 0.0:
            return self.points[0]
        if fraction >= 1.0:
            return self.points[-1]
        want, done = fraction * self.total, 0.0
        for i, length in enumerate(self.lengths):
            if done + length >= want:
                a, b = self.points[i], self.points[i + 1]
                u = (want - done) / length if length else 0.0
                return (a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u)
            done += length
        return self.points[-1]


class Tour(Run):
    def __init__(self, log_path, out_dir, pid, sync):
        super().__init__(log_path, out_dir, pid)
        self.log_path = log_path
        self.sync = sync
        self.bounds = None        # (left, top, width, height) of the client area, screen pixels
        self.t0 = time.time()     # the unix time the video starts
        self.notes = []           # (video seconds, text)

    # ---- time in the video ----
    def video(self):
        return time.time() - self.t0

    def beat(self, text):
        self.notes.append((self.video(), "tour: " + text))
        print("BEAT  video=%.1f %s" % (self.video(), text), flush=True)

    def pause(self, seconds):
        time.sleep(seconds)

    # ---- the pointer ----
    def screen_point(self, pixel):
        """Screen position of a viewport pixel."""
        return (self.bounds[0] + pixel[0], self.bounds[1] + pixel[1])

    def pointer(self):
        got = pointer_position()
        return got if got else (self.bounds[0] + self.bounds[2] // 2, self.bounds[1] + self.bounds[3] // 2)

    def glide(self, pixels, seconds):
        """Move the pointer from where it is through viewport pixels, eased and smooth, taking `seconds`."""
        start = self.pointer()
        route = Route(spline([start] + [self.screen_point(p) for p in pixels]))
        began = time.monotonic()
        last = None
        while True:
            u = (time.monotonic() - began) / seconds
            x, y = route.at(ease(u))
            spot = (int(round(x)), int(round(y)))
            if spot != last:
                warp_pointer_to_screen(*spot)
                last = spot
            if u >= 1.0:
                break
            time.sleep(max(0.0, 1.0 / GLIDE_HZ - 0.005))
        return last

    def glide_to(self, pixel, seconds):
        return self.glide([pixel], seconds)

    def crab_pixel(self):
        """The crab's viewport pixel, and the whole CRABSIM_SCREEN line it came from."""
        screen = self.fresh_screen()
        if screen is None or screen.crab == (-1.0, -1.0):
            raise Abort("no CRABSIM_SCREEN line with the crab in view")
        return screen.crab, screen

    def near_crab(self, dx, dy):
        (cx, cy), _ = self.crab_pixel()
        return (cx + dx, cy + dy)

    def click_left(self):
        mark = self.mark()
        self.mouse.click(BTN_LEFT, CLICK_HOLD)
        return mark

    # ---- the beats ----
    def beat_intro(self):
        self.beat("the beach at low tide")
        self.glide_to(self.near_crab(270, 150), 1.6)
        self.pause(INTRO)

    def beat_scuttle(self):
        self.beat("scuttle: hold left, the crab follows the cursor")
        (cx, cy), _ = self.crab_pixel()
        path = [(cx + dx, cy + dy) for dx, dy in SCUTTLE_PATH]
        self.glide_to(path[0], SCUTTLE_LEAD)
        self.pause(MOVE_PAUSE)
        self.mouse.button_down(BTN_LEFT)
        try:
            self.glide(path[1:], SCUTTLE_STEER)
        finally:
            self.mouse.button_up(BTN_LEFT)
        self.pause(SCUTTLE_SETTLE)
        self.wait_idle(4.0)

    def beat_dance(self):
        self.beat("dance: click the crab")
        (cx, cy), screen = self.crab_pixel()
        self.glide_to((cx, cy), 1.2)
        self.pause(MOVE_PAUSE)
        mark = self.click_left()
        self.expect_event(mark, "dance_start", 1.5, "click on the crab: dance_start event")
        # The cursor steps aside while the crab dances, then comes back to stop it.
        self.pause(0.8)
        self.glide_to((cx + 230, cy + 110), 1.2)
        self.pause(1.0)
        self.beat("dance: click again to stop")
        (cx, cy), screen = self.crab_pixel()
        self.glide_to((cx, cy), 1.2)
        self.pause(MOVE_PAUSE)
        mark = self.click_left()
        self.expect_event(mark, "dance_stop", 1.5, "second click on the crab: dance_stop event")
        self.pause(0.8)

    def beat_dash(self):
        self.beat("dash: right click")
        (cx, cy), _ = self.crab_pixel()
        self.glide_to((cx - DASH_OFFSET, cy - 20), 1.2)
        self.pause(MOVE_PAUSE)
        self.mouse.click(BTN_RIGHT)
        self.pause(DASH_GAP)
        self.glide([(cx, cy - 130), (cx + DASH_OFFSET, cy - 20)], 1.3)
        self.pause(0.2)
        self.mouse.click(BTN_RIGHT)
        self.pause(0.8)

    def visible_burrow(self, index):
        """The burrow's viewport pixel once it is comfortably in view, walking toward it if it is not."""
        for _ in range(4):
            self.wait_idle(4.0)
            screen = self.fresh_screen()
            pixel = screen.burrows.get(index) if screen else None
            if pixel and in_view(pixel, screen.view, BURROW_MARGIN):
                return pixel, screen
            if not pixel or pixel == (-1.0, -1.0):
                return None, screen
            # Off screen or near the edge: click the ground part of the way there.
            (cx, cy) = screen.crab
            w, h = screen.view
            dx, dy = pixel[0] - cx, pixel[1] - cy
            k = min(1.0, 0.75 * min((w / 2 - 120) / max(abs(dx), 1.0), (h / 2 - 90) / max(abs(dy), 1.0)))
            self.beat("walk toward burrow%d, it is not in view yet" % index)
            self.glide_to((cx + dx * k, cy + dy * k), 1.4)
            self.pause(MOVE_PAUSE)
            self.click_left()
            self.pause(1.0)
        return None, None

    def beat_burrow(self):
        self.beat("burrow: click it, the crab walks in and digs in")
        pixel, screen = self.visible_burrow(BURROW_INDEX)
        if pixel is None:
            self.check(False, "burrow%d is in view to click" % BURROW_INDEX)
            return False
        self.glide_to(pixel, 1.6)
        self.pause(MOVE_PAUSE)
        mark = self.click_left()
        enter = self.expect_event(mark, "burrow_enter", 12.0, "click on burrow%d: burrow_enter event" % BURROW_INDEX)
        if enter is None:
            return False
        self.beat("dug in")
        self.glide_to((pixel[0] - 160, pixel[1] + 150), 1.2)
        self.pause(BURROW_DWELL - 1.2)
        self.beat("burrow: click elsewhere to come out")
        screen = self.fresh_screen()
        cx, cy = screen.view[0] / 2.0, screen.view[1] / 2.0
        self.glide_to((cx + EXIT_CLICK[0], cy + EXIT_CLICK[1]), 1.3)
        self.pause(MOVE_PAUSE)
        mark = self.click_left()
        self.expect_event(mark, "burrow_exit", 3.0, "click elsewhere: burrow_exit event")
        return True

    def beat_tide(self):
        self.beat("the crab waits for the tide on the flats")
        # The state log lags the click by a moment: let the walk begin, then wait for it to end.
        self.pause(0.8)
        self.wait_idle(8.0)
        (cx, cy), _ = self.crab_pixel()
        since = self.mark()
        state = self.tail.latest()
        # Wave at the incoming sea until the surge stops the dance.
        if state is not None and state.depth is not None and state.depth < 30.0:
            self.glide_to((cx, cy), 1.3)
            self.pause(MOVE_PAUSE)
            mark = self.click_left()
            self.expect_event(mark, "dance_start", 1.5, "click on the crab while the tide comes in: dance_start event")
        self.glide_to((cx + REST_SPOT[0], cy + REST_SPOT[1]), 1.8)
        deadline_t = since.t + TIDE_CAP
        surged = swept = None
        wall_end = time.time() + TIDE_CAP + 15.0
        while time.time() < wall_end:
            self.tail.poll()
            if surged is None and self.find_event(since.events, "surge_begin", deadline_t):
                surged = True
                self.beat("surge: the water is deep enough to take the crab's grip")
            swept = self.find_event(since.events, "swept_out", deadline_t)
            if swept:
                break
            if not self.alive():
                raise Abort("the game exited during the tide")
            time.sleep(0.1)
        self.check(swept is not None, "the sea sweeps the crab out within %.0f s" % TIDE_CAP)
        if swept:
            self.beat("swept out: the sea carries the crab to the dunes")
            self.pause(HOLD)
        state = self.tail.latest()
        if state is not None:
            self.describe("end", state)

    # ---- the whole tour ----
    def sync_with_recorder(self):
        """Publish the window for record.sh and wait for it to start the video."""
        self.bounds = window_bounds(self.window)
        if not self.bounds:
            raise Abort("cannot measure the game window")
        if not self.sync:
            self.t0 = time.time()
            return
        path = os.path.join(self.out, "window.txt")
        with open(path + ".tmp", "w") as f:
            f.write("%s %d %d %d %d\n" % ((hex(self.window),) + tuple(self.bounds)))
        os.replace(path + ".tmp", path)
        go = os.path.join(self.out, "go")
        deadline = time.time() + GO_TIMEOUT
        while True:
            try:
                with open(go) as f:
                    fields = f.read().split()
                if fields:
                    self.t0 = float(fields[0])
                    return
            except (OSError, ValueError):
                pass
            if time.time() > deadline:
                raise Abort("record.sh never started the video")
            if not self.alive():
                raise Abort("the game exited while waiting for the video to start")
            time.sleep(0.05)

    def scenario_tour(self):
        self.begin(want_mouse=True, settle=SETTLE)
        # Park the cursor where the first frame will show it, off the crab.
        self.bounds = window_bounds(self.window)
        if self.bounds:
            self.place_abs(self.bounds[0] + 1010, self.bounds[1] + 610)
        self.sync_with_recorder()
        self.beat_intro()
        self.beat_scuttle()
        self.beat_dance()
        self.beat_dash()
        self.beat_burrow()
        self.beat_tide()
        self.finish()

    # ---- what happened, with times in the video ----
    def game_events(self):
        """(video seconds, text) for each CRABSIM_EVENT in the game log, timed by its own log stamp (UTC)."""
        found = []
        try:
            with open(self.log_path, "rb") as f:
                lines = f.read().decode("utf-8", "replace").splitlines()
        except OSError:
            return found
        for line in lines:
            m = LOG_TIME_RE.match(line)
            e = LOG_EVENT_RE.search(line)
            if not m or not e:
                continue
            y, mo, d, h, mi, s, ms = (int(g) for g in m.groups())
            wall = calendar.timegm((y, mo, d, h, mi, s)) + ms / 1000.0
            found.append((wall - self.t0, "game: " + e.group(1)))
        return found

    def write_events(self):
        rows = self.notes + [r for r in self.game_events() if r[0] >= -1.0]
        rows.sort(key=lambda r: r[0])
        with open(os.path.join(self.out, "events.txt"), "w") as f:
            for seconds, text in rows:
                f.write("video=%.1f %s\n" % (seconds, text))


def main():
    args = [a for a in sys.argv[1:] if a != "--sync"]
    sync = "--sync" in sys.argv[1:]
    if len(args) < 2:
        print(__doc__)
        return 2
    log_path, out_dir = args[0], args[1]
    pid = int(args[2]) if len(args) > 2 and args[2].isdigit() else None
    os.makedirs(out_dir, exist_ok=True)

    run = Tour(log_path, out_dir, pid, sync)
    aborted = None
    try:
        run.scenario_tour()
    except Abort as e:
        aborted = str(e)
        print("FAIL  aborted: %s" % aborted, flush=True)
    except KeyboardInterrupt:
        aborted = "interrupted"
        print("FAIL  interrupted", flush=True)
    finally:
        if run.mouse:
            run.mouse.close()  # releases any button still held
        run.write_events()

    failed = len(run.failures) + (1 if aborted else 0)
    print("\ntour: %d passed, %d failed" % (run.passed, failed))
    if aborted:
        return 3
    return 1 if run.failures else 0


if __name__ == "__main__":
    sys.exit(main())
