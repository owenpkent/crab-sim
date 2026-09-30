"""A scripted tour of the free camera orbit, for the screen recorder (RECORD_TOUR=orbit Scripts/record.sh).

Same real virtual mouse, window helpers and pacing as tour.py: the pointer glides on eased curves
instead of jumping, including while the right button is held for a drag, so a viewer can follow what is
driving the camera. Holding the right button and moving the pointer sideways turns yaw, and up or down
tilts pitch (CrabSim.OrbitDegreesPerPixel, CrabSim.OrbitPitchDegreesPerPixel, both 0.3 deg/px; pitch is
clamped to [-85, -10] and starts at -38). The pointer is locked inside the view while the button is
down, so a turn bigger than one drag's room is done as a release and a fresh press from the other side.

The story: the default view toward the sea; a slow drag right across most of the view (a bit under a
full turn round the crab); a drag down to look almost straight down on the crab; a drag up, low across
the sand; a slow wide circle with the pointer while holding, swinging yaw and pitch together round and
over the crab; a click on the crab starts the dance and a smaller circle orbits round it while it dances
(the crab keeps turning to face the camera); a second click stops the dance; a plain right click (no
drag) still dashes, on release; a final drag brings the camera back near the default view.

Usage: tour_orbit.py <game log> <output dir> [game pid] [--sync]   (see tour.py; record.sh runs it)

Writes <output dir>/events.txt like tour.py. The game must run with CrabSim.StateLog 1, CrabSim.Gulls 0
and the tide frozen (CrabSim.TideSpeed 0; record.sh does all three). Exit code as tour.py: 0 every beat
happened, 1 some did not, 2 bad usage, 3 the run could not proceed.
"""
import math
import sys

from kbm import BTN_RIGHT
from session import SETTLE
from tour import DASH_OFFSET, MOVE_PAUSE, Tour, main

MARGIN = 30                 # px kept clear of the view's true edge (the pointer is locked inside it while held)
YAW_DEG_PER_PIXEL = 0.3      # CrabSim.OrbitDegreesPerPixel's default
PITCH_DEG_PER_PIXEL = 0.3    # CrabSim.OrbitPitchDegreesPerPixel's default
PITCH_START = -38.0          # CrabPawn's starting camera pitch


def clamp(v, lo, hi):
    return max(lo, min(hi, v))


def circle_points(centre, radius_x, radius_y, turns_deg, steps, start_deg=-90.0):
    """`steps` points on an ellipse round `centre`, starting at `start_deg` and sweeping `turns_deg`."""
    out = []
    for i in range(steps + 1):
        deg = start_deg + turns_deg * i / steps
        rad = math.radians(deg)
        out.append((centre[0] + radius_x * math.cos(rad), centre[1] + radius_y * math.sin(rad)))
    return out


class OrbitTour(Tour):
    def view_mid(self):
        screen = self.fresh_screen()
        vw, vh = screen.view if screen else (1280, 720)
        return vw, vh, (vw / 2.0, vh / 2.0)

    # ---- the beats ----
    def beat_intro(self):
        self.beat("the default view toward the sea")
        self.glide_to(self.near_crab(0, 60), 1.5)
        self.pause(2.0)

    def beat_drag_right(self):
        self.beat("a slow drag right, most of the way across the view: a bit under a full turn")
        vw, vh, mid = self.view_mid()
        start = (MARGIN + 80, mid[1])
        end = (vw - MARGIN - 80, mid[1])
        self.glide_to(start, 1.2)
        self.pause(MOVE_PAUSE)
        self.mouse.button_down(BTN_RIGHT)
        try:
            self.glide_to(end, 4.5)
        finally:
            self.mouse.button_up(BTN_RIGHT)
        self.pause(2.0)

    def beat_drag_down(self):
        self.beat("a drag down: almost straight down on the crab")
        vw, vh, mid = self.view_mid()
        start = (mid[0], MARGIN + 60)
        end = (mid[0], vh - MARGIN - 40)
        self.glide_to(start, 1.2)
        self.pause(MOVE_PAUSE)
        self.mouse.button_down(BTN_RIGHT)
        try:
            self.glide_to(end, 3.0)
        finally:
            self.mouse.button_up(BTN_RIGHT)
        self.pause(2.0)

    def beat_drag_up(self):
        self.beat("a drag up: low across the sand")
        vw, vh, mid = self.view_mid()
        start = (mid[0], vh - MARGIN - 40)
        end = (mid[0], MARGIN + 40)
        self.glide_to(start, 1.0)
        self.pause(MOVE_PAUSE)
        self.mouse.button_down(BTN_RIGHT)
        try:
            self.glide_to(end, 3.0)
        finally:
            self.mouse.button_up(BTN_RIGHT)
        self.pause(2.0)

    def beat_wide_circle(self):
        self.beat("a slow wide circle: yaw and pitch together, swinging round and over the crab")
        vw, vh, mid = self.view_mid()
        radius_x = min(mid[0], vw - mid[0]) - MARGIN - 60
        radius_y = min(mid[1], vh - mid[1]) - MARGIN - 30
        path = circle_points(mid, max(60.0, radius_x), max(40.0, radius_y), 340.0, 10)
        self.glide_to(path[0], 1.2)
        self.pause(MOVE_PAUSE)
        self.mouse.button_down(BTN_RIGHT)
        try:
            self.glide(path[1:], 5.0)
        finally:
            self.mouse.button_up(BTN_RIGHT)
        self.pause(1.0)

    def beat_dance_start(self):
        self.beat("dance: click the crab")
        (cx, cy), _ = self.crab_pixel()
        self.glide_to((cx, cy), 1.2)
        self.pause(MOVE_PAUSE)
        mark = self.click_left()
        self.expect_event(mark, "dance_start", 1.5, "click on the crab: dance_start event")
        self.pause(1.2)

    def beat_dance_orbit(self):
        self.beat("orbit round it while it dances: it turns to keep facing the camera")
        vw, vh, mid = self.view_mid()
        radius_x = min(200.0, min(mid[0], vw - mid[0]) - MARGIN - 60)
        radius_y = min(120.0, min(mid[1], vh - mid[1]) - MARGIN - 30)
        path = circle_points(mid, max(60.0, radius_x), max(40.0, radius_y), 220.0, 6)
        self.glide_to(path[0], 1.0)
        self.pause(MOVE_PAUSE)
        self.mouse.button_down(BTN_RIGHT)
        try:
            self.glide(path[1:], 3.5)
        finally:
            self.mouse.button_up(BTN_RIGHT)
        self.pause(0.8)

    def beat_dance_stop(self):
        self.beat("dance: click again to stop")
        (cx, cy), _ = self.crab_pixel()
        self.glide_to((cx, cy), 1.0)
        self.pause(MOVE_PAUSE)
        mark = self.click_left()
        self.expect_event(mark, "dance_stop", 1.5, "second click on the crab: dance_stop event")
        self.pause(1.0)

    def beat_dash(self):
        self.beat("a plain right click: still a dash, now on release")
        (cx, cy), _ = self.crab_pixel()
        self.glide_to((cx - DASH_OFFSET, cy - 20), 1.0)
        self.pause(0.6)
        self.mouse.click(BTN_RIGHT)
        self.pause(1.0)

    def beat_orbit_home(self):
        self.beat("a drag back: near the default view")
        vw, vh, mid = self.view_mid()
        state = self.tail.latest()
        cam = state.cam if state else 0.0
        pitch = state.pitch if state else PITCH_START
        max_dx = mid[0] - MARGIN - 40
        max_dy = mid[1] - MARGIN - 40
        dx = clamp(-cam / YAW_DEG_PER_PIXEL, -max_dx, max_dx)
        dy = clamp((pitch - PITCH_START) / PITCH_DEG_PER_PIXEL, -max_dy, max_dy)
        end = (mid[0] + dx, mid[1] + dy)
        self.glide_to(mid, 1.2)
        self.pause(MOVE_PAUSE)
        self.mouse.button_down(BTN_RIGHT)
        try:
            self.glide_to(end, 3.0)
        finally:
            self.mouse.button_up(BTN_RIGHT)
        self.pause(1.5)

    def scenario_tour(self):
        self.begin(want_mouse=True, settle=SETTLE)
        self.sync_with_recorder()
        self.beat_intro()
        self.beat_drag_right()
        self.beat_drag_down()
        self.beat_drag_up()
        self.beat_wide_circle()
        self.beat_dance_start()
        self.beat_dance_orbit()
        self.beat_dance_stop()
        self.beat_dash()
        self.beat_orbit_home()
        self.pause(1.5)
        self.finish()


if __name__ == "__main__":
    sys.exit(main(OrbitTour))
