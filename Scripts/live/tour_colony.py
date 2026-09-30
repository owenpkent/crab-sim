"""A scripted tour of the colony under the dune-foot burrow, for the screen recorder (RECORD_TOUR=colony
Scripts/record.sh).

Same real virtual mouse, window helpers and pacing as tour.py. The story: walk to the colony's entrance
burrow (0, the highest, which never floods) and dig in; DOWN into the cutaway; a beat to look round; walk to
the pantry and EAT from the colony's store; walk to the active face and DIG until the crab holds a pellet;
carry it up by clicking the top of the entrance shaft (rather than the UP button, to show that route too) as
the mound grows; back DOWN; dance near the colony's own crabs so they wave back; UP to the beach.

Usage: tour_colony.py <game log> <output dir> [game pid] [--sync]   (see tour.py; record.sh runs it)

Writes <output dir>/events.txt like tour.py. The game must run with CrabSim.StateLog 1, CrabSim.Gulls 0, the
tide frozen (CrabSim.TideSpeed 0) and CrabSim.StartFood 0.3 (record.sh sets all four). Exit code as tour.py:
0 every beat happened, 1 some did not, 2 bad usage, 3 the run could not proceed.
"""
import sys
import time

from session import Abort, SETTLE, in_view
from tour import MOVE_PAUSE, Tour, main

COLONY_BURROW_INDEX = 0     # the colony's own entrance is always burrow 0
DOWN_WINDOW = 4.0           # s from the click on DOWN to colony_enter
LOOK_ROUND_PAUSE = 2.0      # s: a beat to take in the cutaway before moving on
EAT_SECONDS = 4.0           # s spent on EAT, long enough for the food bar to visibly move
DIG_MAX_WAIT = 14.0         # s to wait for a pellet (walk to the face, then dig a PelletUu's worth)
UP_WINDOW = 12.0            # s from the click on the shaft top to colony_exit
DANCE_WINDOW = 1.5          # s from a click on the crab to dance_start


class ColonyTour(Tour):
    def wait_for_state(self, pred, timeout, what):
        """Polls the log until a state satisfies pred, or timeout (game seconds from now) passes."""
        deadline_t = self.now_t() + timeout
        wall_end = time.time() + timeout + 10.0
        while time.time() < wall_end:
            self.tail.poll()
            state = self.tail.latest()
            if state is not None and pred(state):
                return state
            if state is not None and state.t > deadline_t:
                break
            if not self.alive():
                raise Abort("the game exited during " + what)
            time.sleep(0.05)
        self.check(False, "%s within %.0f s" % (what, timeout))
        return None

    # ---- the beats ----
    def beat_intro(self):
        self.beat("the beach at low tide, food already sifted a little (CrabSim.StartFood)")
        self.glide_to(self.near_crab(240, 140), 1.4)
        self.pause(1.0)

    def beat_walk_to_colony_burrow(self):
        # Burrow 0 sits behind the starting camera (toward the dunes, -X), off screen and behind it: the BURROW
        # button reaches it with no aim needed, the same way session.py's colony scenario does.
        self.beat("BURROW: walk to the colony's own burrow (0, the highest, under the dune foot)")
        screen = self.fresh_screen()
        if screen is None or screen.goburrow is None:
            self.check(False, "a CRABSIM_SCREEN line with the BURROW button arrived")
            return False
        self.glide_to(screen.goburrow, 1.4)
        self.pause(MOVE_PAUSE)
        mark = self.click_left()
        enter = self.expect_event(mark, "burrow_enter", 14.0, "click BURROW: burrow_enter event")
        if enter is None:
            return False
        self.beat("dug in")
        self.pause(0.8)
        return True

    def beat_down(self):
        self.beat("DIG (now DOWN): into the colony")
        screen = self.fresh_screen()
        if screen is None or screen.dig is None:
            self.check(False, "a CRABSIM_SCREEN line with the DOWN button arrived")
            return False
        self.glide_to(screen.dig, 1.2)
        self.pause(MOVE_PAUSE)
        mark = self.click_left()
        entered = self.expect_event(mark, "colony_enter", DOWN_WINDOW, "click DOWN: colony_enter event")
        return entered is not None

    def beat_look_round(self):
        self.beat("a beat to look round the cutaway")
        self.pause(LOOK_ROUND_PAUSE)

    def beat_eat(self):
        self.beat("FOOD (now EAT): walk to the pantry and eat from the store")
        screen = self.fresh_screen()
        if screen is None or screen.gofood is None:
            self.check(False, "a CRABSIM_SCREEN line with the EAT button arrived")
            return False
        self.glide_to(screen.gofood, 1.2)
        self.pause(MOVE_PAUSE)
        self.click_left()
        self.pause(EAT_SECONDS)

    def beat_dig(self):
        self.beat("DIG (now help dig): walk to the face and dig until a pellet forms")
        screen = self.fresh_screen()
        if screen is None or screen.dig is None:
            self.check(False, "a CRABSIM_SCREEN line with the dig button arrived")
            return False
        self.glide_to(screen.dig, 1.2)
        self.pause(MOVE_PAUSE)
        self.click_left()
        got = self.wait_for_state(lambda s: s.carry == 1, DIG_MAX_WAIT, "carry=1 once a pellet is dug")
        return got is not None

    def beat_carry_up(self):
        # The camera follows the crab the same way it does on the surface, so right after digging at a face
        # well below the mouth the shaft top (burrow0's own screen position) is usually off the top of the
        # view: click it directly when it happens to be in view, otherwise fall back to the UP button, which
        # needs no aim (the same choice a player has).
        screen = self.fresh_screen()
        pixel = screen.burrows.get(COLONY_BURROW_INDEX) if screen else None
        if pixel and in_view(pixel, screen.view):
            self.beat("carry it up: click the top of the entrance shaft (not the UP button)")
            self.glide_to(pixel, 1.2)
        elif screen is not None and screen.goburrow is not None:
            self.beat("carry it up: BURROW (now UP), the shaft top being off screen from here")
            self.glide_to(screen.goburrow, 1.2)
        else:
            self.check(False, "either the shaft top or the UP button is in view")
            return False
        self.pause(MOVE_PAUSE)
        mark = self.click_left()
        pellet = self.expect_event(mark, "colony_pellet", UP_WINDOW, "arriving with the pellet: colony_pellet event")
        self.expect_event(mark, "colony_exit", UP_WINDOW, "climbing out: colony_exit event")
        self.beat("the mound grows")
        self.pause(1.0)
        return pellet is not None

    def beat_back_down(self):
        self.beat("DOWN again")
        screen = self.fresh_screen()
        if screen is None or screen.dig is None:
            self.check(False, "a CRABSIM_SCREEN line with the DOWN button arrived")
            return False
        self.glide_to(screen.dig, 1.2)
        self.pause(MOVE_PAUSE)
        mark = self.click_left()
        entered = self.expect_event(mark, "colony_enter", DOWN_WINDOW, "click DOWN: colony_enter event")
        if entered is None:
            return False
        # The camera eases into the underground side view (camera lag): let it settle before aiming at the
        # crab from its on-screen position, or the click lands where the crab used to be, mid-transition.
        self.pause(1.5)
        return True

    def beat_dance(self):
        self.beat("dance near the colony's own crabs: they wave back")
        (cx, cy), _ = self.crab_pixel()
        self.glide_to((cx, cy), 1.0)
        self.pause(MOVE_PAUSE)
        mark = self.click_left()
        self.expect_event(mark, "dance_start", DANCE_WINDOW, "click on the crab: dance_start event")
        self.pause(2.5)
        (cx, cy), _ = self.crab_pixel()
        self.glide_to((cx, cy), 1.0)
        self.pause(MOVE_PAUSE)
        self.click_left()
        self.pause(0.6)

    def beat_up(self):
        self.beat("BURROW (now UP): back to the beach")
        screen = self.fresh_screen()
        if screen is None or screen.goburrow is None:
            self.check(False, "a CRABSIM_SCREEN line with the UP button arrived")
            return False
        self.glide_to(screen.goburrow, 1.2)
        self.pause(MOVE_PAUSE)
        mark = self.click_left()
        self.expect_event(mark, "colony_exit", UP_WINDOW, "click UP: colony_exit event")
        self.pause(1.0)

    def scenario_tour(self):
        self.begin(want_mouse=True, settle=SETTLE)
        self.sync_with_recorder()
        self.beat_intro()
        if self.beat_walk_to_colony_burrow() and self.beat_down():
            self.beat_look_round()
            self.beat_eat()
            self.beat_dig()
            self.beat_carry_up()
            if self.beat_back_down():
                self.beat_dance()
                self.beat_up()
        self.finish()


if __name__ == "__main__":
    sys.exit(main(ColonyTour))
