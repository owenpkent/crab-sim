"""A scripted tour of molting, for the screen recorder (RECORD_TOUR=molt Scripts/record.sh).

Same real virtual mouse, window helpers and pacing as tour.py (the pointer glides between targets so a viewer can
follow it), but the story is the molt: the beach with the crab fed (CrabSim.FoodFloor keeps it at 0.9 or more, so no
foraging is needed), a click on the molt button in the open and the reason it gives, a burrow, the molt with its
progress ring, the crab settling into the hole and pulsing, the pip filling and the crab bigger when it climbs out,
twice more, and then the third molt ends the round: the results panel, and the NEW ROUND button that puts the world
back.

Usage: tour_molt.py <game log> <output dir> [game pid] [--sync]   (see tour.py; record.sh runs it)

Writes <output dir>/events.txt like tour.py. The game must run with CrabSim.StateLog 1 and CrabSim.FoodFloor 0.9
(record.sh does both). Exit code as tour.py: 0 every beat happened, 1 some did not, 2 bad usage, 3 the run could not
proceed.
"""
import sys

from focus import window_bounds
from session import Abort, SETTLE, event_detail
from tour import AWAY_PX, MOVE_PAUSE, Tour, main

BURROW_INDEX = 2           # burrow2, a short walk from the start and dry all through the clip
MOLT_DONE_WINDOW = 12.5    # s from the click to molt_done: the molt takes ten
AFTERGLOW = 2.0            # s kept after a molt, so the crab swelling and the pip filling can be seen
LOOK_AT_CRAB = 2.2         # s the crab walks about outside, bigger, before it goes back in
RESULTS_HOLD = 4.5         # s on the results panel


class MoltTour(Tour):
    def beat_intro_molt(self):
        self.beat("the beach at low tide, the crab fed, no molts yet")
        self.glide_to(self.near_crab(300, 200), 1.6)
        self.pause(1.4)

    def beat_refused(self):
        self.beat("the molt button in the open: click it for the reason")
        screen = self.fresh_screen()
        if screen is None or screen.molt is None:
            self.check(False, "a CRABSIM_SCREEN line with the molt button arrived")
            return
        self.glide_to(screen.molt, 1.5)
        self.pause(MOVE_PAUSE)
        mark = self.click_left()
        self.expect_event(mark, "molt_refused", 1.5, "click on the molt button out in the open: molt_refused event")
        self.pause(1.8)

    def beat_dig_in(self, label):
        self.beat("burrow: %s" % label)
        pixel, screen = self.visible_spot("burrows", BURROW_INDEX)
        if pixel is None:
            self.check(False, "burrow%d is in view to click" % BURROW_INDEX)
            return False
        self.glide_to(pixel, 1.4)
        self.pause(MOVE_PAUSE)
        mark = self.click_left()
        enter = self.expect_event(mark, "burrow_enter", 12.0, "click on burrow%d: burrow_enter event" % BURROW_INDEX)
        if enter is None:
            return False
        self.pause(0.6)
        return True

    def beat_molt(self, number):
        self.beat("molt %d: click the molt button, ten seconds in the burrow" % number)
        screen = self.fresh_screen()
        if screen is None or screen.molt is None:
            self.check(False, "a CRABSIM_SCREEN line with the molt button arrived")
            return False
        self.glide_to(screen.molt, 1.2)
        self.pause(MOVE_PAUSE)
        mark = self.click_left()
        begin = self.expect_event(mark, "molt_begin", 1.5, "click on the molt button: molt_begin event")
        if begin is None:
            return False
        # The cursor steps off the button so the progress ring can be seen.
        self.glide_to((screen.molt[0] - 210, screen.molt[1] + 40), 1.0)
        done = self.expect_event(mark, "molt_done", MOLT_DONE_WINDOW, "molt_done event")
        if done is None:
            return False
        self.beat("molted: the pip fills and the crab has grown (%s)" % done.rest)
        self.pause(AFTERGLOW)
        return True

    def beat_climb_out(self):
        self.beat("click elsewhere: the crab climbs out, bigger")
        (cx, cy), _ = self.crab_pixel()
        self.glide_to((cx + AWAY_PX[0], cy + AWAY_PX[1]), 1.2)
        self.pause(MOVE_PAUSE)
        mark = self.click_left()
        self.expect_event(mark, "burrow_exit", 2.0, "click elsewhere: burrow_exit event")
        self.pause(0.8)
        self.wait_idle(6.0)
        self.pause(LOOK_AT_CRAB)

    def beat_results(self):
        self.beat("three molts: the round is won")
        won = self.wait_for(lambda: self.find_event(0, "round_won", float("inf")), self.now_t() + 3.0)
        self.check(won is not None, "the third molt logs round_won")
        self.pause(RESULTS_HOLD)
        screen = self.fresh_screen()
        if screen is None or screen.newround is None:
            self.check(False, "a CRABSIM_SCREEN line with the new round button arrived")
            return
        self.beat("new round: click the button, the world is put back")
        self.glide_to(screen.newround, 1.4)
        self.pause(MOVE_PAUSE)
        mark = self.click_left()
        self.expect_event(mark, "round_new", 1.5, "click on NEW ROUND: round_new event")
        self.glide_to((screen.view[0] * 0.5 + 260, screen.view[1] * 0.5 + 170), 1.0)
        self.pause(2.6)

    def scenario_tour(self):
        self.begin(want_mouse=True, settle=SETTLE)
        # Park the cursor where the first frame will show it, off the crab.
        self.bounds = window_bounds(self.window)
        if self.bounds:
            self.place_abs(self.bounds[0] + 1010, self.bounds[1] + 610)
        self.sync_with_recorder()
        self.beat_intro_molt()
        self.beat_refused()
        # Molts one and two are each followed by a walk outside, so the crab can be seen growing.
        for number in (1, 2):
            if not self.beat_dig_in("dig in" if number == 1 else "dig in again"):
                break
            if not self.beat_molt(number):
                break
            self.beat_climb_out()
        else:
            if self.beat_dig_in("dig in for the last molt") and self.beat_molt(3):
                self.beat_results()
        self.finish()


if __name__ == "__main__":
    sys.exit(main(MoltTour))
