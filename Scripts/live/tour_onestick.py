"""A scripted tour of one-stick mode, for the screen recorder (RECORD_TOUR=onestick Scripts/record.sh).

Same real virtual mouse, window helpers and pacing as tour.py, plus a virtual gamepad (gamepad.py, uinput the
same way the mouse is) for the stick itself. The story: the MENU column with its highlighted cursor box, a
tap moving the cursor, a select (FOOD: the crab walks to the best patch), a triple tap into STEER, steering
at full deflection and then at partial deflection (visibly slower: CrabStick::SteerSpeedMultiplier), a triple
tap back to the menu, and a mouse click on the ONE STICK button turning it off.

Usage: tour_onestick.py <game log> <output dir> [game pid] [--sync]   (see tour.py; record.sh runs it)

Writes <output dir>/events.txt like tour.py. The game must run with CrabSim.StateLog 1 and CrabSim.OneStick 1
(record.sh does both). Exit code as tour.py: 0 every beat happened, 1 some did not, 2 bad usage, 3 the run
could not proceed.
"""
import sys

from focus import window_bounds
from gamepad import Gamepad
from session import Abort, SETTLE, STICK_GAP_WAIT, STICK_PEAK
from tour import MOVE_PAUSE, Tour, main

STEER_HOLD = 2.5  # s the stick is held over for each steering beat: comfortably longer than a viewer needs to read it


class OneStickTour(Tour):
    def beat_menu_intro(self):
        self.beat("one-stick mode: the MENU column, MOVE highlighted")
        # Off to the crab's lower right: clear of the menu column, which sits on the left.
        self.glide_to(self.near_crab(320, 210), 1.2)
        self.pause(1.6)

    def beat_cursor_moves(self):
        self.beat("a tap down moves the cursor: MOVE to FOOD, FOOD to BURROW")
        mark = self.mark()
        self.stick_tap(0.0, STICK_PEAK)
        self.expect_event_word(mark, "cursor", "cursor", "FOOD", 1.5, "cursor=FOOD")
        self.pause(0.7)
        mark = self.mark()
        self.stick_tap(0.0, STICK_PEAK)
        self.expect_event_word(mark, "cursor", "cursor", "BURROW", 1.5, "cursor=BURROW")
        self.pause(0.9)
        self.beat("a tap up moves it back: BURROW to FOOD")
        mark = self.mark()
        self.stick_tap(0.0, -STICK_PEAK)
        self.expect_event_word(mark, "cursor", "cursor", "FOOD", 1.5, "cursor=FOOD")
        self.pause(0.9)

    def beat_select_food(self):
        self.beat("a right tap selects FOOD: held pending, then the gap closes it")
        mark = self.mark()
        self.stick_tap(STICK_PEAK, 0.0)
        self.pause(STICK_GAP_WAIT + 0.3)
        self.expect_event_word(mark, "select", "select", "FOOD", 1.5, "select=FOOD")
        self.expect_event(mark, "goto_food", 1.5, "the crab walks to the best patch: goto_food event")
        self.pause(3.0)

    def beat_enter_steer(self):
        self.beat("a triple tap: into STEER")
        mark = self.mark()
        self.stick_tap(0.0, STICK_PEAK)
        self.stick_tap(0.0, STICK_PEAK)
        self.stick_tap(0.0, STICK_PEAK)
        self.expect_event(mark, "toggle", 1.5, "toggle event")
        self.expect_event_word(mark, "mode", "mode", "steer", 1.5, "mode=steer")
        self.pause(0.7)

    def beat_steer_full(self):
        self.beat("steer: full deflection")
        self.pad.set_stick(1.0, 0.0)
        self.pause(STEER_HOLD)
        self.pad.centre_stick()
        self.pause(0.7)

    def beat_steer_partial(self):
        self.beat("steer: partial deflection (about 0.6), visibly slower")
        self.pad.set_stick(0.6, 0.0)
        self.pause(STEER_HOLD)
        self.pad.centre_stick()
        self.pause(0.9)

    def beat_exit_steer(self):
        self.beat("a triple tap: back to the menu, the crab stops")
        mark = self.mark()
        self.stick_tap(0.0, STICK_PEAK)
        self.stick_tap(0.0, STICK_PEAK)
        self.stick_tap(0.0, STICK_PEAK)
        self.expect_event(mark, "toggle", 1.5, "toggle event")
        self.expect_event_word(mark, "mode", "mode", "menu", 1.5, "mode=menu")
        self.pause(1.2)

    def beat_toggle_off(self):
        self.beat("the ONE STICK button: click it off")
        screen = self.fresh_screen()
        if screen is None or screen.onestick is None:
            self.check(False, "a CRABSIM_SCREEN line with the ONE STICK button arrived")
            return
        self.glide_to(screen.onestick, 1.2)
        self.pause(MOVE_PAUSE)
        mark = self.click_left()
        self.expect_event(mark, "onestick_off", 1.5, "onestick_off event")
        self.pause(2.2)

    def scenario_tour(self):
        self.begin(want_mouse=True, settle=SETTLE)
        self.pad = Gamepad()
        self.bounds = window_bounds(self.window)
        if self.bounds:
            self.place_abs(self.bounds[0] + 1010, self.bounds[1] + 610)
        self.sync_with_recorder()
        self.beat_menu_intro()
        self.beat_cursor_moves()
        self.beat_select_food()
        self.beat_enter_steer()
        self.beat_steer_full()
        self.beat_steer_partial()
        self.beat_exit_steer()
        self.beat_toggle_off()
        self.finish()


if __name__ == "__main__":
    sys.exit(main(OneStickTour))
