# Contributing

One maintainer, public repo, MIT licence. This is how changes get in.

## Workflow

One branch per change. `main` is protected: pull requests are required for
everyone, including the owner, no force pushes, no deletions, and a branch is
deleted once its PR merges. Open a PR for every change, even a small one.

Stacked PRs are fine: base a PR on the branch it builds on rather than on
`main`, and say so in the PR body. When the base branch merges into `main`,
GitHub retargets the PR onto `main` automatically.

Merges use a merge commit, not squash. The per-commit messages are the record
of what happened and why; squashing throws that away.

## Commit messages

A summary line that says what changed, in plain words, naming the actual
thing: a class (`CrabGull`), a script (`record.sh`), a scenario (the `gull`
live scenario). Not "fix bug" or "update files". Below it, a short paragraph
on why, and anything a reviewer would otherwise have to dig for: what it costs
(food, time), what it touches (docs, config), what the before/after behavior
is. Look at `git log` for the tone; it is dense and specific, not boilerplate.
No "Co-Authored-By" trailers, no generated-by footers, no attribution of any
kind beyond the author git already records.

## Before opening a PR

- `Scripts/build.sh` builds the editor target. Must succeed.
- `Scripts/test.sh` runs the headless automation suite. While iterating on one
  area, narrow it: `Scripts/test.sh CrabSim.Gull` or whatever filter matches.
  Both must be clean before a PR goes up.
- `Scripts/live-test.sh` for anything that touches input, the camera, the HUD
  layout, the state log, or the tours. The headless suite cannot see the
  window, the cursor, or the camera; this is what catches regressions there.
  It needs an X11 session and write access to `/dev/uinput`, drives the real
  pointer, and only one session may run at a time. Leave the machine alone
  while it runs. Run the one scenario that matters with `LIVE_SCENARIOS=`
  rather than the whole list if you're iterating.
- `RECORD_TOUR=<name> Scripts/record.sh` for anything that changes a look:
  a new material, a new animation, a changed HUD element, a changed tour
  script. Commit the resulting mp4 and its `.events.txt` (both go through
  LFS). Pick the tour that covers what changed, or add a new one if none
  does.

Not every PR needs all four. A docs-only change needs none of them. A change
to `CrabGullMath.h` needs the build, the suite, and probably the `gull` live
scenario; it does not need a recording unless the gull's look changed too.
Say in the PR which of these you ran.

## Code

- Every `.h` and `.cpp` starts with an SPDX header, `// SPDX-License-Identifier:
  Apache-2.0`, matching every file already in the module. Don't invent a
  different line.
- Rules live as pure functions in `*Math.h` headers (`CrabMovementMath.h`,
  `CrabGullMath.h`, `CrabMoltMath.h`, and so on): no engine singletons, no
  `UWorld`, nothing that needs a running game to call. That's what makes them
  unit-testable headless and what the balance simulations run against.
  New rules get a header in that style and tests alongside the existing ones
  in `Source/CrabSim/Tests/`. Actors (`CrabPawn`, `CrabGull`, `CrabBeach`,
  `CrabHUD`) wire the math to the world; they don't carry rule logic
  themselves.
- The `CRABSIM_STATE`, `CRABSIM_EVENT`, `CRABSIM_SCREEN`, and `CRABSIM_COLONY`
  log lines are a parsed interface: the live tests and the tour scripts read
  them by field name. Add new fields at the end of the line. Never rename or
  reorder an existing field or remove one; something downstream is almost
  certainly reading it by position or name.
- Design intent goes in `GAME.md`; controls and layout go in `README.md`.
  When a change touches either, update both in the same PR, not as a
  follow-up.

## Art

The pipeline and the contract (names, orientation, units, texture packing,
the skeleton, the animation contract) are in `Art/README.md`. Read it before
touching anything under `Art/`.

- `.uasset` and `.umap` files are generated, never hand-edited in the editor.
  `Art/unreal/build_content.py` (run via `Scripts/import-art.sh`) builds the
  materials and imports meshes, textures, and animations; `build_level.py`
  (via `Scripts/build-level.sh`) builds the maps. If something needs to look
  different, change the script or the source art and re-run the build, don't
  open the editor and nudge it by hand: the next rebuild would silently
  overwrite that nudge.
- CC0 textures come from Poly Haven through `Art/fetch_polyhaven.sh`. Record
  what was fetched, its licence, and the date in `Art/assets/polyhaven.md`.
  The same goes for any other third-party asset: provenance recorded before
  it's used, CC0 or equivalent only.
- Binaries (`.uasset`, `.fbx`, `.blend`, images, audio, video) are in git LFS;
  see `.gitattributes` for the exact extensions. Don't add a new binary type
  without adding its LFS rule first.

## Safety on a dev box

Never kill `UnrealEditor` by process name. Other projects may have it open on
the same machine. Every script here stops only what it started, by the pid it
recorded when it launched the process. Never run two live sessions
(`live-test.sh` or `record.sh`) at once; both drive the real pointer and will
fight each other.

## Accessibility

Accessibility is a design constraint of this game, not a nice-to-have. Input
must work with a pointer alone: no scroll wheel, no held key combinations, no
cue-timed windows that demand a reaction in a tight window. A one-stick mode
exists alongside the pointer path and has to keep working too.

Any new control needs a pointer-only path before it needs anything else, and
needs an entry in `README.md`'s Controls section describing it. If it can't
be done with a single click or a held click, it's probably the wrong control
for this game; ask before building it.

## Reporting bugs

Attach the run folder, not just a description: `Saved/LiveTest/<run>/` or
`Saved/Record/<run>/`, in particular `game.log` and, for a tour, `tour.txt`.
Say which scenario or tour it was and what you expected instead of what you
saw. A log with the `CRABSIM_STATE`/`CRABSIM_EVENT` lines around the moment
of the bug is worth more than a screenshot.
