"""A reactive playtest bot: plays one full round of the real game like a person, with the real virtual pointer.

It reads the game's own log (CrabSim.StateLog 1) and clicks with the left button only, exactly the input a player has:
no keys, no wheel, no holds. It is a playtest tool, not an AI: a handful of readable rules, no lookahead beyond the
tide, which is a known cosine.

The policy, in the order it is checked each tick (a quarter of a second):
  1. Molting: sit still. Leave only if the sea will flood the burrow before the molt is done.
  1b. A gull is down (landing, stalking or lunging): press BURROW and hide until it has gone, two seconds longer. A
     crab that is feeding sifts on while the gull is more than 700 uu away, on every other gull, and answers when it
     comes nearer. PLAYTEST_GULLS=ignore plays as if there were no gulls at all (the balance check for a crab that
     never looks up).
  2. The sea is coming at the crab (it will be 40 uu deep over the spot in 4 s): go on to a higher patch if one
     is close (6 s) and clear for 20 s, else to the nearest burrow that stays dry long enough, and from there to the high burrow the
     sea never floods.
  3. Food is 0.80 or more and a burrow that will stay dry through the molt is near: click it, then press MOLT.
  4. Food allows (0.55), the tide is low, no dry burrow is near, and none was dug yet: walk to clear dry sand and
     press DIG.
  5. Otherwise click the best reachable food patch (richest, near, and clear of the sea for the next 20 s) and let the
     crab feed. Leave when it is bare (or nearly: 0.15 left, and another patch is reachable), or when 3 above says so.
  6. In a burrow with nothing to do: wait. Come out when a patch is reachable again, that is, when the water has fallen.

A patch or burrow is clicked where it is on screen. One the camera cannot show (or that sits under a HUD button) is
reached with the HUD's FOOD or BURROW button instead: one click walks the crab to the game's own choice (the best
usable patch, richness over distance, and the safest reachable burrow), and the bot adopts that choice as its goal. A hop toward the target is the
fallback, for when the button is greyed (the crab is full, or already in a burrow) or refuses.

Clicks are left clicks on a food patch, a burrow, the HUD's FOOD or BURROW button (to reach one off screen), the
ground (a hop toward a target that is off screen, when the buttons cannot help, or toward open sand to dig) or the
HUD's DIG, MOLT buttons. Every click goes to <output dir>/clicks.csv with its time and kind (patchN, burrowN, a _hop
suffix for a hop, ground, dig, molt, gofood, goburrow); the SUMMARY line counts them by kind and per minute. The
pointer glides between targets on eased curves like tour.py's. Nothing here checks the game: PASS/FAIL lines only
say whether the round finished.

Usage: playtest.py <game log> <output dir> [game pid] [--sync]   (see tour.py; record.sh runs it with RECORD_TOUR=playtest)
Run the game at the default tide: CrabSim.StateLog 1, no CrabSim.TideSpeed, no FoodFloor or StartFood. Gulls are on.

Environment: PLAYTEST_CAP (real seconds before it gives up, default 1200), PLAYTEST_SHOTS (folder for the
screenshots, default the output dir), PLAYTEST_TIDE_SPEED (the tide's speed when it is not 1), PLAYTEST_GULLS
(answer, the default, or ignore) and PLAYTEST_ROUNDS (how many rounds to play, default 1: a round ended by a gull is
followed by NEW ROUND and another; PLAYTEST_CAP counts for the whole run).

Writes to <output dir>: clicks.csv (one row per click), playtest.log (what it decided and why) and the screenshots
1-first-feeding, 2-first-dig-done, 3-first-molt, 4-high-tide-in-burrow, 5-results (the panel, won or eaten), and for the
first gull gull-1-circling, gull-2-landed, gull-3-near, and gull-4-flying-in-view and gull-5-walking-in-view when the gull is on
screen (.png, 1280x720 by Scripts/shot.sh).

Exit code: 0 the last round finished (fully grown or eaten), 1 it did not (cap or crash of a rule), 3 the run could not proceed.
"""
import csv
import math
import os
import subprocess
import sys
import time
import traceback

from focus import pointer_position, screenshot, warp_pointer_to_screen, window_bounds
from kbm import BTN_LEFT
from session import Abort, CLICK_HOLD, SETTLE, event_detail, in_view
from tour import Tour, main

# ---- the beach and the tide as a player learns them in the first minute (GAME.md) ----
BURROWS = [(-2450, 350), (-1000, -520), (700, 760), (1900, -900)]
PATCHES = [(-1300, 450, 0.40), (-650, 520, 0.45), (-350, -700, 0.60), (600, -650, 0.75),
           (1150, -80, 0.80), (1700, -420, 0.90), (1800, 850, 1.00)]   # x, y, richness
TIDE_LOW, TIDE_HIGH, TIDE_PERIOD, SWELL = -190.0, 90.0, 180.0, 9.0
TIDE_SPEED = float(os.environ.get("PLAYTEST_TIDE_SPEED") or 1.0)
FLOOD_DEPTH = 50.0         # water over a burrow that floods it
CAPSULE_HALF = 55.0        # the crab's z is this far above the ground under it
FEED_RATE, DRAIN = 0.025, 0.004   # food per second from a full patch, and lost per second

# ---- the rules' numbers ----
WALK_SPEED = 300.0         # uu/s: between the crab's sideways (450) and forward (250) speeds
THREAT_LEAD = 4.0          # s ahead the sea is looked at
THREAT_DEPTH = 40.0        # uu of water (at the swell crest) over the crab that counts as a threat (the surge is 45)
FLOOD_MARGIN = 15.0        # uu short of the flood depth that already counts as flooded: burrow heights are a guess
RETREAT_NEED = 18.0        # s a burrow must stay dry after arrival to retreat to it
FLOOD_LEAD = 8.0           # s before the flood (plus the walk) that a crab in a low burrow moves up
MIN_WINDOW = 20.0          # s a patch must stay clear of the sea after arrival
NEAR_PATCH_WALK = 6.0      # s: with the sea coming, only a patch this close is worth a detour from the way to a burrow
MIN_RICH = 0.12            # a patch with less than this is not worth the walk
LEAVE_RICH = 0.15          # a patch being sifted is left, for a better one, when this little is left in it
MOLT_FOOD = 0.80
MOLT_SECONDS = 10.0
MOLT_MARGIN = 6.0          # s dry wanted on top of the molt's ten
DIG_FOOD = 0.55            # the dig costs 0.30 and the store must be back up for the molt afterwards
MAX_DIGS = 1
DIG_CLEAR = 265.0          # uu from every burrow and patch (the game wants 250)
DIG_TIDE_QUIET = 40.0      # s of dry sand wanted at the spot: the tide is still low
NEAR_BURROW = 900.0        # uu: a burrow this close makes digging pointless
DIG_WALKS = [(-40, 230), (-300, 170), (300, 170), (-40, -200), (-380, -60)]  # px from the crab, toward the dunes first
RECLICK_WAIT = 1.6         # s before the same order is given again
GOTO_WAIT = 2.0            # s the game gets to answer a FOOD or BURROW press (goto_food, goto_burrow or goto_refused)
GOTO_RETRY = 10.0          # s of game time before a refused button is pressed again
FULL = 0.98                # food at which the game greys the FOOD button
BUTTON_RANGE = 3500.0      # uu: BURROW takes the highest floor within this, else the nearest dry burrow
IDLE_RESET = 45.0          # s of nothing to do at low tide, after which the patches are assumed fresh
STUCK_SECONDS = 6.0        # a walk that has not moved the crab 15 uu in this long is stuck
CAP = float(os.environ.get("PLAYTEST_CAP") or 1200.0)
GULLS = (os.environ.get("PLAYTEST_GULLS") or "answer").strip().lower()   # "answer" or "ignore"
ROUNDS = max(1, int(os.environ.get("PLAYTEST_ROUNDS") or 1))
GULL_THREAT = ("Landing", "Stalking", "Lunging")   # the phases in which a gull is down and coming
GULL_FEED_ON = 700.0       # uu: on every other gull a crab that is feeding sifts on until the gull is this near
GULL_HOLD = 2.0            # s a crab in its burrow waits after the gull was last down
GULL_NEED = 12.0           # s a burrow must stay dry after arrival to be a hiding place from a gull
GULL_FRESH = 0.7           # s: a gull line older than this means the gull is gone
TICK = 0.25
FAR = 999.0

# ---- the screen ----
AIM_MARGIN = 100           # px a clicked target keeps from the viewport edge
HOP_PX = (420, 290)        # farthest a hop toward an off-screen target goes from the crab, px (clear of the HUD buttons)
HOP_CHAIN = 250.0          # uu: the next hop is clicked when the crab is this close to the end of the last one
HOP_MIN = 130              # nearest, so a hop is never a click on the crab (that dances)
SCREEN_PER_UU = (0.6, -0.35)   # rough px per uu, right and up, for a target the camera cannot project
BUTTON_HALF = {"dig": (70.0, 70.0), "molt": (70.0, 70.0), "gofood": (85.0, 50.0), "goburrow": (85.0, 50.0)}   # half a HUD button, px at 720p
HUD_ROOM = 10.0 + 20.0     # a button's hit margin and some room, px at 720p
ONE = {"burrows": "burrow", "patches": "patch"}
GOTO_BUTTON = {"patches": "gofood", "burrows": "goburrow"}
GOTO_EVENT = {"patches": "goto_food", "burrows": "goto_burrow"}
LABEL = {"dig": "DIG", "molt": "MOLT", "gofood": "FOOD", "goburrow": "BURROW"}
SHOT_NAMES = {"feeding": "1-first-feeding", "dig": "2-first-dig-done", "molt": "3-first-molt",
              "high": "4-high-tide-in-burrow", "results": "5-results", "gull_circling": "gull-1-circling",
              "gull_landed": "gull-2-landed", "gull_near": "gull-3-near", "gull_air": "gull-4-flying-in-view",
              "gull_ground": "gull-5-walking-in-view"}


# ---- the tide, as a function of the game clock ----
def tide_level(t):
    clock = t * TIDE_SPEED
    return 0.5 * (TIDE_LOW + TIDE_HIGH) - 0.5 * (TIDE_HIGH - TIDE_LOW) * math.cos(2.0 * math.pi * clock / TIDE_PERIOD)


def ground(x, y=0.0):
    """The beach's height under (x, y): the slope and the dunes. Close enough to place a burrow or a patch."""
    d = min(1.0, max(0.0, (-2200.0 - x) / 1400.0))
    return -0.06 * x - 40.0 + d * d * (3.0 - 2.0 * d) * 260.0


def depth(t, z):
    """Water over ground at height z at game time t, counting the swell at its crest."""
    return max(0.0, tide_level(t) + SWELL - z)


def first_time(now, test, horizon=400.0, step=1.0):
    """The first game time from now at which test(t) holds, or None."""
    t = now
    while t <= now + horizon:
        if test(t):
            return t
        t += step
    return None


def dry_for(z, t):
    """Seconds after t before the sea floods a burrow at height z (with the margin), FAR if it never does."""
    flood = first_time(t, lambda u: depth(u, z) > FLOOD_DEPTH - FLOOD_MARGIN)
    return FAR if flood is None else flood - t


class PlayTest(Tour):
    def __init__(self, log_path, out_dir, pid, sync):
        super().__init__(log_path, out_dir, pid, sync)
        self.burrow_at = {i: (x, y, ground(x, y)) for i, (x, y) in enumerate(BURROWS)}
        self.rich = [full for _, _, full in PATCHES]   # richness left in each patch, as far as the crab knows
        self.goal = None            # (kind, index) the last click asked for
        self.goal_hop = False       # and whether that click was a hop toward it
        self.goal_button = False    # or a press of the FOOD or BURROW button (the goal is then the game's choice)
        self.refused = {}           # button name -> game time it was last refused
        self.pending = None         # (kind, index, pos, check time, hop) the last click, checked once
        self.last_click = 0.0
        self.click_times = []
        self.click_kinds = []
        self.misses = []
        self.ev_i = 0
        self.molting = False
        self.dug = 0
        self.dig_walks = 0
        self.dig_plan = False
        self.no_molt_until = 0.0
        self.idle_since = None
        self.here_since = None            # when the crab reached the patch or burrow it was sent to
        self.sift = None                  # (patch, its richness, the crab's food, game time) when the sifting began
        self.watch = (0.0, 0.0, 0.0)      # wall time, x, y when the crab last moved
        self.snap_due = {}
        self.snap_done = set()
        self.round_t0 = 0.0               # game time at which the tide clock last started from low water (each round's start)
        self.round_no = 1
        self.outcomes = []                # "won" or "eaten" for each round played
        self.gull_count = 0               # the newest gull's number this round, as the game counts them
        self.gulls_met = 0                # gulls seen over the whole run, and how many the bot answered with BURROW
        self.gull_answers = 0
        self.gull_seen_t = -999.0         # shifted game time a gull was last down
        self.gull_goal = False            # the crab is on its way to a burrow because of a gull
        self.shots = os.environ.get("PLAYTEST_SHOTS") or out_dir
        os.makedirs(self.shots, exist_ok=True)
        self.csv = open(os.path.join(out_dir, "clicks.csv"), "w", newline="")
        self.rows = csv.writer(self.csv)
        self.rows.writerow(["wall", "video_s", "game_t", "kind", "why", "px", "py"])
        self.logf = open(os.path.join(out_dir, "playtest.log"), "w")
        self.began = time.time()

    # ---- reporting ----
    def say(self, text):
        s = self.tail.latest()
        line = "[t=%6.1f v=%6.1f] %s" % (s.t if s else 0.0, self.video(), text)
        print(line, flush=True)
        self.logf.write(line + "\n")
        self.logf.flush()

    def snap(self, key):
        """Screenshot the game window (Scripts/shot.sh, else the window grab from focus.py)."""
        if key in self.snap_done:
            return
        self.snap_done.add(key)
        path = os.path.join(self.shots, "playtest-%s.png" % SHOT_NAMES[key])
        shot = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "shot.sh")
        try:
            ok = subprocess.run([shot, path], capture_output=True, timeout=30).returncode == 0
        except (OSError, subprocess.SubprocessError):
            ok = False
        if not ok:
            ok = screenshot(self.shots, "playtest-%s" % SHOT_NAMES[key], self.window)
        self.say("screenshot %s: %s" % (SHOT_NAMES[key], path if ok else "FAILED"))

    def snap_later(self, key, seconds):
        if key not in self.snap_done and key not in self.snap_due:
            self.snap_due[key] = time.time() + seconds

    # ---- the pointer ----
    def click(self, kind, why=""):
        """One left click where the pointer is, logged with its time."""
        s = self.tail.latest()
        self.pending = None            # any click supersedes the last order, so its check no longer means anything
        self.mouse.click(BTN_LEFT, CLICK_HOLD)
        at = pointer_position()
        px, py = (at[0] - self.bounds[0], at[1] - self.bounds[1]) if at else (-1, -1)
        self.rows.writerow(["%.3f" % time.time(), "%.2f" % self.video(), "%.2f" % (s.t if s else 0.0), kind, why, px, py])
        self.csv.flush()
        self.click_times.append(time.time())
        self.click_kinds.append(kind)
        self.last_click = time.time()

    def point_at(self, pixel):
        """Glide the pointer to a viewport pixel, quicker for a short move."""
        here = self.pointer()
        there = self.screen_point(pixel)
        self.glide_to(pixel, min(1.3, 0.35 + math.hypot(there[0] - here[0], there[1] - here[1]) / 1400.0))
        time.sleep(0.12)

    def press(self, name):
        """Click a HUD button: "dig", "molt", "gofood" or "goburrow". None when the game does not log that button."""
        screen = self.fresh_screen()
        pixel = getattr(screen, name) if screen else None
        if pixel is None:
            return None
        self.point_at(pixel)
        mark = self.mark()
        self.click(name, "the %s button" % LABEL[name])
        return mark

    def hud_boxes(self, screen):
        """[(left, top, right, bottom)] of each HUD button the game logged, grown by its hit margin and some room, px."""
        k = screen.view[1] / 720.0
        boxes = []
        for name, (hx, hy) in BUTTON_HALF.items():
            b = getattr(screen, name)
            if b is not None:
                hx, hy = (hx + HUD_ROOM) * k, (hy + HUD_ROOM) * k
                boxes.append((b[0] - hx, b[1] - hy, b[0] + hx, b[1] + hy))
        return boxes

    def on_hud(self, pixel, screen):
        return any(l < pixel[0] < r and t < pixel[1] < b for l, t, r, b in self.hud_boxes(screen))

    def aim(self, screen, kind, pos):
        """(pixel, hop): where to click for a burrow or patch. Its own pixel when it is in view and clear of the HUD
        buttons, else a ground pixel part of the way toward it, so it comes into view."""
        pixel = getattr(screen, kind).get(pos[2])
        if pixel and pixel != (-1.0, -1.0) and in_view(pixel, screen.view, AIM_MARGIN) and not self.on_hud(pixel, screen):
            return pixel, False
        cx, cy = screen.crab
        if pixel and pixel != (-1.0, -1.0):
            vx, vy = pixel[0] - cx, pixel[1] - cy
        else:
            s = self.tail.latest()
            vx, vy = (pos[1] - s.y) * SCREEN_PER_UU[0], (pos[0] - s.x) * SCREEN_PER_UU[1]
        k = min(1.0, HOP_PX[0] / max(abs(vx), 1.0), HOP_PX[1] / max(abs(vy), 1.0))
        vx, vy = vx * k, vy * k
        length = math.hypot(vx, vy)
        if length < HOP_MIN:
            vx, vy = vx * HOP_MIN / max(length, 1.0), vy * HOP_MIN / max(length, 1.0)
        x, y = min(max(cx + vx, 80.0), screen.view[0]), min(max(cy + vy, 60.0), screen.view[1] - 60.0)
        # The camera trails the crab, so its pixel is not always the middle: keep the hop off the HUD buttons. In a
        # button's row it stops short of the button on its left (the go-to buttons sit left of DIG and MOLT).
        for _ in (0, 1):
            for left, top, right, bottom in self.hud_boxes(screen):
                if top < y < bottom and x > left:
                    x = left - 1.0
            if math.hypot(x - cx, y - cy) >= HOP_MIN:
                break
            # That left it on the crab (a click there dances): go up or down from it instead.
            y = min(max(cy + math.copysign(HOP_MIN, vy if vy else -1.0), 60.0), screen.view[1] - 60.0)
        return (x, y), True

    def needs_hop(self, kind, pos):
        """True when the latest screen line shows the target off screen or under a HUD button (no click can reach it)."""
        screens = self.tail.screens
        return not screens or self.aim(screens[-1], kind, pos)[1]

    def spot(self, kind, index):
        """(x, y, index) of a burrow or a patch in the world."""
        if kind == "burrows":
            x, y, _ = self.burrow_at[index]
        else:
            x, y, _ = PATCHES[index]
        return (x, y, index)

    def press_goto(self, kind, s, why):
        """Reach a patch or burrow that cannot be clicked with the FOOD or BURROW button. True when the game took the
        press and its own choice is now the goal. False when the button is greyed, refused or missing: hop instead."""
        name = GOTO_BUTTON[kind]
        last = self.refused.get(name)
        inside = s.burrow is not None and s.burrow >= 0
        feeding = s.feeding is not None and s.feeding >= 0
        full = s.food is not None and s.food >= FULL
        # The game greys BURROW in a burrow and FOOD when full. Standing on a patch, FOOD would only pick that one.
        if (last is not None and s.t - last < GOTO_RETRY) or (inside if kind == "burrows" else full or feeding):
            return False
        mark = self.press(name)
        if mark is None:
            return False               # an older game that does not log the button
        limit = mark.t + GOTO_WAIT
        found = self.wait_for(lambda: self.find_event(mark.events, GOTO_EVENT[kind], limit)
                              or self.find_event(mark.events, "goto_refused", limit), limit)
        chosen = event_detail(found, ONE[kind]) if found is not None and found.name == GOTO_EVENT[kind] else None
        chosen = None if chosen is None else int(chosen)
        if chosen is None or not (chosen in self.burrow_at if kind == "burrows" else 0 <= chosen < len(PATCHES)):
            self.refused[name] = self.now_t() - self.round_t0
            if found is None:
                reason = "no answer from the game"
            elif found.name == "goto_refused":
                reason = "refused: %s" % found.rest
            else:
                reason = "it named an unknown %s: %s" % (ONE[kind], found.rest)
            self.say("press %s: %s: hop toward it instead" % (LABEL[name], reason))
            return False
        self.goal = (kind, chosen)     # the game's choice replaces the bot's
        self.goal_hop = False
        self.goal_button = True
        self.pending = None            # a press has no MISS check
        self.say("press %s: the game chose %s%d: %s" % (LABEL[name], ONE[kind], chosen, why))
        return True

    def go(self, kind, index, why):
        """Click a burrow or a patch (or press FOOD or BURROW, or click the ground toward it, when it cannot be clicked)
        unless that order already stands."""
        pos = self.spot(kind, index)
        s = self.tail.latest()
        if self.heading_to(s, pos):
            return
        if self.goal == (kind, index):
            if self.goal_hop:
                # A hop is under way: click the next one when the crab is nearly at its end, so it never stops between.
                if self.moving(s) and not self.hop_nearly_done(s) or time.time() - self.last_click < 0.5:
                    return
            elif time.time() - self.last_click < RECLICK_WAIT:
                return
        elif self.goal_button and self.goal and self.goal[0] == kind and self.needs_hop(kind, pos):
            # The game chose a target of its own for a button press and the crab is on its way there: leave it be.
            if self.heading_to(s, self.spot(*self.goal)) or time.time() - self.last_click < RECLICK_WAIT:
                return
        screen = self.fresh_screen()
        if screen is None:
            return
        pixel, hop = self.aim(screen, kind, pos)
        if hop and self.press_goto(kind, s, why):
            return
        self.goal_button = False
        self.say("click %s%d%s: %s" % (ONE[kind], index, " (hop toward it)" if hop else "", why))
        self.point_at(pixel)
        if not hop:
            # The camera follows the crab, so the target may have moved while the pointer glided: aim once more.
            again = self.fresh_screen()
            now = getattr(again, kind).get(index) if again else None
            if now and now != (-1.0, -1.0) and math.hypot(now[0] - pixel[0], now[1] - pixel[1]) > 8:
                warp_pointer_to_screen(*self.screen_point(now))
                time.sleep(0.1)
        self.click(ONE[kind] + str(index) + ("_hop" if hop else ""), why)
        self.goal = (kind, index)
        self.goal_hop = hop
        self.pending = (kind, index, pos, time.time() + 1.2, hop)

    def hop_nearly_done(self, s):
        return self.goal_hop and s.target is not None and math.hypot(s.x - s.target[0], s.y - s.target[1]) < HOP_CHAIN

    @staticmethod
    def heading_to(s, pos):
        return s is not None and s.target is not None and math.hypot(s.target[0] - pos[0], s.target[1] - pos[1]) < 6.0

    # ---- what the log says ----
    def absorb(self, s):
        """Fold new log events into what the bot knows, and time the screenshots."""
        while self.ev_i < len(self.tail.events):
            e = self.tail.events[self.ev_i]
            self.ev_i += 1
            note = e.name in ("surge_begin", "swept_out", "flooded_out", "soft_begin", "soft_end", "molt_refused",
                              "molt_cancel", "molt_done", "dig_begin", "dig_cancel", "dig_done", "molt_begin", "food_begin",
                              "food_end", "burrow_enter", "burrow_exit", "round_won", "round_eaten", "gull_circling",
                              "gull_landed", "gull_stalking", "gull_scared", "gull_left", "gull_catch")
            if e.name == "food_begin":
                patch, richness = event_detail(e, "patch"), event_detail(e, "richness")
                if patch is not None and richness is not None:
                    self.rich[int(patch)] = richness
                    self.sift = (int(patch), richness, s.food, s.t)
                self.snap_later("feeding", 3.0)
            elif e.name == "food_end":
                patch, amount = event_detail(e, "patch"), event_detail(e, "amount")
                if patch is not None and amount is not None:
                    self.rich[int(patch)] = max(0.0, self.rich[int(patch)] - amount)
            elif e.name == "dig_done":
                index = event_detail(e, "burrow")
                if index is not None:
                    self.burrow_at[int(index)] = (s.x, s.y, s.z - CAPSULE_HALF)
                self.dug += 1
                self.snap_later("dig", 0.8)
            elif e.name == "molt_begin":
                self.molting = True
                self.snap_later("molt", 4.0)
            elif e.name in ("molt_done", "molt_cancel", "flooded_out"):
                self.molting = False
            elif e.name in ("round_won", "round_eaten"):
                self.snap_later("results", 2.0)
            elif e.name == "gull_circling":
                self.gulls_met += 1
                self.snap_later("gull_circling", 2.0)
            elif e.name == "gull_landed":
                self.snap_later("gull_landed", 1.5)
            if note:
                self.say("event %s (game t=%.1f) %s" % (e.name, e.t, e.rest))
        for i, (px, py, full) in enumerate(PATCHES):
            if s.water is not None and s.water - ground(px, py) > 12.0:
                self.rich[i] = full        # the sea has soaked it: fresh once it leaves
        if s.tide is not None and s.tide >= 0.995 and s.burrow is not None and s.burrow >= 0:
            self.snap("high")
        gull = self.gull_now(s)
        if gull is not None and gull.phase in ("Stalking", "Lunging") and gull.dist <= 900.0:
            self.snap("gull_near")
        # The gull itself, when the camera shows it (the game logs its pixel), in the air or on the sand.
        screens = self.tail.screens
        if gull is not None and screens and screens[-1].gull and (s.t + self.round_t0) - screens[-1].t < 1.0 \
                and in_view(screens[-1].gull, screens[-1].view, 80):
            self.snap("gull_air" if gull.alt > 20.0 else "gull_ground")
        for key, due in list(self.snap_due.items()):
            if time.time() >= due:
                del self.snap_due[key]
                self.snap(key)
        # A click that was meant for a burrow or a patch: did the game take it?
        if self.pending and time.time() >= self.pending[3]:
            kind, index, pos, _, hop = self.pending
            self.pending = None
            took = (self.heading_to(s, pos) or s.burrow == index and kind == "burrows"
                    or s.feeding == index and kind == "patches")
            if not hop and not took:
                self.misses.append((s.t, kind, index))
                self.say("MISS: the click on %s%d did not become an order" % (ONE[kind], index))

    def moving(self, s):
        return s.speed > 30.0 or (s.target is not None and (s.feeding is None or s.feeding < 0) and not s.dig)

    def stuck(self, s):
        """True when a walk has not moved the crab for a while."""
        wall, x, y = self.watch
        if math.hypot(s.x - x, s.y - y) > 15.0 or not self.moving(s):
            self.watch = (time.time(), s.x, s.y)
            return False
        return time.time() - wall > STUCK_SECONDS

    # ---- gulls ----
    def gull_now(self, s):
        """The gull the game is logging now (its newest CRABSIM_GULL line is fresh), or None. A gull's game time is the
        game's own clock, s.t here has the round's start taken off."""
        g = self.tail.latest_gull()
        if g is None or (s.t + self.round_t0) - g.t > GULL_FRESH:
            return None
        self.gull_count = g.count
        return g

    def gull_holds(self, s):
        """True while a crab in its burrow should stay: a gull is down, or was a moment ago."""
        if GULLS != "answer":
            return False
        g = self.gull_now(s)
        if g is not None and g.phase in GULL_THREAT:
            self.gull_seen_t = s.t
        return s.t - self.gull_seen_t < GULL_HOLD

    def answer_gull(self, s):
        """Out in the open with a gull down: press BURROW (a hop to the nearest safe burrow when the button will not),
        after sifting on with a far gull when the crab is feeding, on every other gull. True when it took the tick."""
        g = self.gull_now(s)
        if g is None or g.phase not in GULL_THREAT:
            self.gull_goal = False
            return False
        self.gull_seen_t = s.t
        feeding = s.feeding is not None and s.feeding >= 0
        if feeding and g.count % 2 == 1 and g.dist > GULL_FEED_ON:
            return False                      # a far gull: sift on, and look again next tick
        if self.gull_goal and self.goal and self.goal[0] == "burrows" and s.target is not None:
            return True                       # on the way already
        self.gull_answers += 1
        self.say("GULL %d is %s, %.0f uu away: BURROW" % (g.count, g.phase.lower(), g.dist))
        self.gull_goal = True
        if not self.press_goto("burrows", s, "a gull is down"):
            safe = self.safe_burrows(s, GULL_NEED)
            self.go("burrows", safe[0][1] if safe else self.refuge(s), "a gull is down")
        return True

    # ---- the rules ----
    def safe_burrows(self, s, need):
        """[(walk seconds, index)] of the burrows the crab can reach and still have `need` seconds of dry hole, nearest first."""
        found = []
        for i, (x, y, z) in self.burrow_at.items():
            walk = math.hypot(x - s.x, y - s.y) / WALK_SPEED
            if dry_for(z, s.t + walk) >= need:
                found.append((walk, i))
        return sorted(found)

    def refuge(self, s):
        """The burrow to sit the high tide out in: the nearest the sea never floods, else the highest."""
        never = [(math.hypot(x - s.x, y - s.y), i) for i, (x, y, z) in self.burrow_at.items() if dry_for(z, s.t) >= FAR]
        if never:
            return min(never)[1]
        return max(self.burrow_at, key=lambda i: self.burrow_at[i][2])

    def sea_threat(self, s):
        z = s.z - CAPSULE_HALF
        return (s.depth or 0.0) > THREAT_DEPTH or depth(s.t + THREAT_LEAD, z) > THREAT_DEPTH

    def patch_options(self, s):
        """[(score, index)] of the patches worth walking to now, best first."""
        options = []
        for i, (px, py, _) in enumerate(PATCHES):
            rich = self.rich[i]
            if rich < MIN_RICH:
                continue
            walk = math.hypot(px - s.x, py - s.y) / WALK_SPEED + 0.5
            arrive, z = s.t + walk, ground(px, py)
            if depth(arrive, z) > 8.0:
                continue
            soak = first_time(arrive, lambda u: depth(u, z) > THREAT_DEPTH)
            window = FAR if soak is None else soak - arrive
            if window < MIN_WINDOW:
                continue
            gain = min(rich, FEED_RATE * (0.3 + 0.7 * rich) * window)
            options.append((gain - DRAIN * walk + (0.05 if self.goal == ("patches", i) else 0.0), i))
        return sorted(options, reverse=True)

    def button_burrow(self, s):
        """The burrow a press of BURROW takes, by the game's rule: of the dry ones the crab gets to before the sea (3 s to
        spare) and within 3500 uu, the highest floor (the nearer among floors within 10 uu), else the nearest dry one."""
        dry = [(math.hypot(x - s.x, y - s.y), z, i) for i, (x, y, z) in self.burrow_at.items()
               if dry_for(z, s.t) >= math.hypot(x - s.x, y - s.y) / WALK_SPEED + 3.0]
        near = [d for d in dry if d[0] <= BUTTON_RANGE]
        if near:
            top = max(z for _, z, _ in near)
            near = [d for d in near if d[1] >= top - 10.0]
        pool = near or dry
        return min(pool)[2] if pool else None

    def molt_choice(self, s):
        """The burrow to go and molt in, when the food covers the walk and it stays dry through the molt. One out of view
        is reached with BURROW, which takes the game's own choice, so that is the walk the food has to cover."""
        for walk, i in self.safe_burrows(s, MOLT_SECONDS + MOLT_MARGIN)[:1]:
            if self.needs_hop("burrows", self.spot("burrows", i)):
                j = self.button_burrow(s)
                if j is not None:
                    x, y, z = self.burrow_at[j]
                    there = math.hypot(x - s.x, y - s.y) / WALK_SPEED
                    if dry_for(z, s.t + there) >= MOLT_SECONDS + MOLT_MARGIN:
                        walk, i = there, j
            if s.food >= MOLT_FOOD + DRAIN * (walk + 2.0):
                return i
        return None

    def clearance(self, x, y):
        near = [math.hypot(x - bx, y - by) for bx, by, _ in self.burrow_at.values()]
        near += [math.hypot(x - px, y - py) for px, py, _ in PATCHES]
        return min(near)

    def dig_wanted(self, s):
        """The rule for a dig. Once decided it holds (the food would otherwise flap around the threshold, and the crab
        would walk off and back) until the hole is dug, the tide is no longer low, or the store cannot pay for it."""
        if self.dug >= MAX_DIGS or self.dig_walks > len(DIG_WALKS):
            return False
        z = s.z - CAPSULE_HALF
        if first_time(s.t, lambda u: depth(u, z + 10.0) > 0.0, horizon=DIG_TIDE_QUIET) is not None:
            self.dig_plan = False       # the tide is not low enough for a hole that stays dry
            return False
        if self.dig_plan:
            return s.food >= 0.32
        if s.food < DIG_FOOD or any(math.hypot(x - s.x, y - s.y) <= NEAR_BURROW
                                    for x, y, hz in self.burrow_at.values() if dry_for(hz, s.t) > 40.0):
            return False
        self.dig_plan = True
        return True

    def dig_step(self, s):
        """Press DIG on clear sand, else walk toward clear sand (dunes first) and try again."""
        if self.clearance(s.x, s.y) >= DIG_CLEAR:
            mark = self.press("dig")
            if mark is None:
                return
            began = self.wait_for(lambda: self.find_event(mark.events, "dig_begin", mark.t + 2.0), mark.t + 2.0)
            self.say("dig %s" % ("began" if began else "was refused (clearance %.0f)" % self.clearance(s.x, s.y)))
            if began:
                self.wait_for(lambda: self.find_event(mark.events, "dig_done", mark.t + 7.0), mark.t + 7.0)
            else:
                self.dig_walks += 1
            return
        screen = self.fresh_screen()
        if screen is None:
            return
        dx, dy = DIG_WALKS[self.dig_walks % len(DIG_WALKS)]
        self.dig_walks += 1
        cx, cy = screen.crab
        self.say("walk to open sand to dig (clearance %.0f uu)" % self.clearance(s.x, s.y))
        self.point_at((cx + dx, cy + dy))
        self.click("ground", "open sand for a dig")
        self.goal = ("ground", 0)

    def press_molt(self, s):
        mark = self.press("molt")
        if mark is None:
            return
        began = self.wait_for(lambda: self.find_event(mark.events, "molt_begin", mark.t + 2.0), mark.t + 2.0)
        if not began:
            self.no_molt_until = time.time() + 5.0
            self.say("the molt did not begin (food %.3f)" % s.food)

    # ---- one tick ----
    def step(self, s):
        inside = s.burrow is not None and s.burrow >= 0
        stuck = self.stuck(s)              # every tick, so its timer starts when the walk does
        if self.molting or (s.molt or 0.0) > 0.0:
            here = self.burrow_at.get(s.burrow)
            left = MOLT_SECONDS * (1.0 - (s.molt or 0.0))
            if here and dry_for(here[2], s.t) < left + 2.0:
                self.say("the sea will flood this hole before the molt ends: leaving")
                self.go("burrows", self.refuge(s), "flood before the molt is done")
            return
        if inside:
            self.gull_goal = False
            return self.step_inside(s)
        if GULLS == "answer" and self.answer_gull(s):
            return
        moving = self.moving(s)
        if self.sea_threat(s):
            options = [o for o in self.patch_options(s)      # a higher patch close by, that the sea has not reached yet
                       if math.hypot(PATCHES[o[1]][0] - s.x, PATCHES[o[1]][1] - s.y) / WALK_SPEED <= NEAR_PATCH_WALK]
            if options and self.molt_choice(s) is None:
                self.go("patches", options[0][1], "the sea is coming: on to a higher patch")
                return
            safe = self.safe_burrows(s, RETREAT_NEED)
            self.go("burrows", safe[0][1] if safe else self.refuge(s), "the sea is coming")
            return
        if moving and not self.hop_nearly_done(s):
            if stuck:
                self.say("STUCK: no progress for %.0f s at %.0f,%.0f" % (STUCK_SECONDS, s.x, s.y))
                self.goal = None
                self.watch = (time.time(), s.x, s.y)
            return
        if self.arriving(s):
            return
        choice = self.molt_choice(s)
        if choice is not None:
            self.go("burrows", choice, "food %.2f is enough to molt" % s.food)
            return
        if self.dig_wanted(s):
            return self.dig_step(s)
        if s.feeding is not None and s.feeding >= 0:
            self.idle_since = None
            return self.leave_thin_patch(s)
        options = self.patch_options(s)
        if options:
            self.idle_since = None
            self.go("patches", options[0][1], "richest reachable (score %.2f, %.2f left)" % (options[0][0], self.rich[options[0][1]]))
            return
        self.wait_out(s)

    def leave_thin_patch(self, s):
        """Sifting a patch that is nearly bare: click a better one that is in view. One that needs the FOOD button or a hop
        is not worth leaving for: the button's own pick may be this one, so the crab sifts on until it is bare."""
        if self.sift is None or self.sift[0] != s.feeding:
            return
        _, rich, food, t = self.sift
        left = rich - ((s.food - food) + DRAIN * (s.t - t))
        others = [o for o in self.patch_options(s)
                  if o[1] != s.feeding and not self.needs_hop("patches", self.spot("patches", o[1]))]
        if left < LEAVE_RICH and others:
            self.go("patches", others[0][1], "patch%d is nearly bare (%.2f left): on to a better one" % (s.feeding, left))

    def arriving(self, s):
        """The crab stands at the burrow or patch it was sent to, and the log has not caught up (feeding= and burrow=
        show a tick later): give it a moment. After 1.5 s a patch it is not feeding on is bare, and the crab is
        standing on it, so it must not be clicked again (a click on the crab is a dance)."""
        if not self.goal or self.goal[0] == "ground" or self.goal_hop or (s.feeding is not None and s.feeding >= 0):
            self.here_since = None
            return False
        kind, i = self.goal
        if kind == "patches" and self.rich[i] < MIN_RICH:
            self.here_since = None      # it has been sifted bare: nothing to wait for
            return False
        x, y, _ = self.spot(kind, i)
        if math.hypot(s.x - x, s.y - y) >= (200.0 if kind == "patches" else 80.0):
            self.here_since = None
            return False
        if self.here_since is None:
            self.here_since = time.time()
        if time.time() - self.here_since < 1.5:
            return True
        self.here_since = None
        if kind == "patches":
            self.rich[i] = 0.0
            self.say("patch%d is bare (the crab is on it and not feeding)" % i)
        self.goal = None
        return False

    def step_inside(self, s):
        """In a burrow and not molting."""
        z = self.burrow_at[s.burrow][2] if s.burrow in self.burrow_at else ground(s.x, s.y)
        dry = dry_for(z, s.t)
        if s.food >= MOLT_FOOD + 0.004 and dry >= MOLT_SECONDS + MOLT_MARGIN and time.time() >= self.no_molt_until:
            self.say("food %.2f: molt (this hole stays dry %.0f s)" % (s.food, min(dry, 999)))
            return self.press_molt(s)
        refuge = self.refuge(s)
        rx, ry, _ = self.burrow_at[refuge]
        if dry < math.hypot(rx - s.x, ry - s.y) / WALK_SPEED + FLOOD_LEAD and s.burrow != refuge:
            self.go("burrows", refuge, "the sea will flood this hole in %.0f s" % dry)
            return
        if self.gull_holds(s):
            return
        options = self.patch_options(s)
        if options:
            self.idle_since = None
            self.go("patches", options[0][1], "the water has fallen: out to the richest patch")
            return
        self.wait_out(s)

    def wait_out(self, s):
        """Nothing to eat and nothing to flee: sit in a burrow. After a long lull at low tide, assume the flats are fresh again."""
        if s.burrow is None or s.burrow < 0:
            safe = self.safe_burrows(s, RETREAT_NEED)
            self.go("burrows", safe[0][1] if safe else self.refuge(s), "nothing to eat: wait in a burrow")
            return
        if self.idle_since is None:
            self.idle_since = time.time()
            self.say("waiting: no patch is reachable (food %.2f)" % s.food)
        elif time.time() - self.idle_since > IDLE_RESET and not self.sea_threat(s):
            self.rich = [full for _, _, full in PATCHES]
            self.idle_since = time.time()
            self.say("waited %.0f s: assuming the patches are fresh again" % IDLE_RESET)

    # ---- the round ----
    def play(self):
        """One round, from the bot's clock as it stands. "won" or "eaten" when the results panel comes up, None at the cap."""
        errors = 0
        while time.time() - self.began < CAP:
            self.tail.poll()
            s = self.tail.latest()
            if s is None:
                time.sleep(TICK)
                continue
            if not self.alive():
                raise Abort("the game exited during the round")
            s = s._replace(t=s.t - self.round_t0)      # the tide restarts with each round, so the bot's clock does
            self.absorb(s)
            if s.over:
                return "eaten" if s.eaten else "won"
            try:
                self.step(s)
                errors = 0
            except Abort:
                raise
            except Exception:
                errors += 1
                self.say("RULE ERROR %d: %s" % (errors, traceback.format_exc().strip().splitlines()[-1]))
                traceback.print_exc()
                if errors > 20:
                    raise Abort("the rules keep failing")
            time.sleep(TICK)
        return None

    def linger(self, seconds):
        """Watch the log for a while without playing (the results panel is up), so its screenshot is taken."""
        end = time.time() + seconds
        while time.time() < end:
            self.tail.poll()
            s = self.tail.latest()
            if s is not None:
                self.absorb(s._replace(t=s.t - self.round_t0))
            time.sleep(0.2)

    def new_round(self):
        """Click NEW ROUND on the results panel and start the bot's memory over, with the new round's clock. False if it did not start."""
        screen = self.fresh_screen()
        pixel = screen.newround if screen else None
        if pixel is None:
            return False
        self.point_at(pixel)
        mark = self.mark()
        self.click("newround", "start the next round")
        limit = mark.t + 3.0
        began = self.wait_for(lambda: self.find_event(mark.events, "round_new", limit), limit)
        if began is None:
            return False
        self.round_t0 = began.t
        self.round_no += 1
        self.burrow_at = {i: (x, y, ground(x, y)) for i, (x, y) in enumerate(BURROWS)}
        self.rich = [full for _, _, full in PATCHES]
        self.goal, self.goal_hop, self.goal_button = None, False, False
        self.refused, self.pending = {}, None
        self.molting, self.dug, self.dig_walks, self.dig_plan = False, 0, 0, False
        self.no_molt_until, self.idle_since, self.here_since, self.sift = 0.0, None, None, None
        self.gull_goal, self.gull_seen_t = False, -999.0
        self.wait_for(lambda: self.tail.latest() is not None and self.tail.latest().t > began.t + 0.3 and not self.tail.latest().over, began.t + 4.0)
        self.say("ROUND %d begins (game time %.1f)" % (self.round_no, began.t))
        return True

    def summary(self):
        gaps = [b - a for a, b in zip([self.began] + self.click_times, self.click_times)]
        kinds, real = self.click_kinds, time.time() - self.began
        hops = sum(1 for k in kinds if k.endswith("_hop"))
        goto = sum(1 for k in kinds if k in ("gofood", "goburrow"))
        self.say("SUMMARY outcomes=%s rounds=%d eaten=%d gulls=%d gull_answers=%d clicks=%d hops=%d ground=%d goto_buttons=%d "
                 "dig_presses=%d molt_presses=%d per_min=%.1f longest_gap=%.0f s misses=%d dug=%d real=%.0f s"
                 % ("+".join(self.outcomes) or "none", len(self.outcomes), self.outcomes.count("eaten"), self.gulls_met, self.gull_answers,
                    len(kinds), hops, kinds.count("ground"), goto, kinds.count("dig"), kinds.count("molt"), len(kinds) * 60.0 / max(real, 1.0),
                    max(gaps, default=0.0), len(self.misses), self.dug, real))

    def scenario_tour(self):
        self.begin(want_mouse=True, settle=SETTLE)
        self.bounds = window_bounds(self.window)
        if self.bounds:
            self.place_abs(self.bounds[0] + 1010, self.bounds[1] + 610)
        self.sync_with_recorder()
        self.began = time.time()
        self.beat("the round starts: default tide, no cheats, gulls %s" % ("answered with BURROW" if GULLS == "answer" else "ignored"))
        result = None
        for number in range(ROUNDS):
            result = self.play()
            if result is None:
                break
            self.outcomes.append(result)
            self.say("ROUND %d over: %s (game time %.1f)" % (self.round_no, result, self.tail.latest().t - self.round_t0 if self.tail.latest() else 0.0))
            if number + 1 < ROUNDS:
                self.linger(3.0)             # the panel and its screenshot, then the next round
                if not self.new_round():
                    self.say("NEW ROUND did not start the next round")
                    break
        self.check(result is not None, "the round finished (fully grown, or eaten by a gull) within %.0f s" % CAP)
        if result is not None:
            self.beat("the results panel: %s" % ("fully grown" if result == "won" else "eaten by a gull"))
            self.linger(6.0)                 # the panel stays up: its screenshot, and a moment of it on the clip
        self.summary()
        self.finish()


if __name__ == "__main__":
    sys.exit(main(PlayTest))
