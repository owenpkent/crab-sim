<!--
Filling in the sections below speeds up review. If a section does not apply,
write "n/a".
-->

## Summary

<!-- 1-3 bullets: what does this change and why? -->

## Related issue

<!-- e.g. Closes #123, or "no issue" -->

## Type of change

- [ ] Gameplay
- [ ] Art / assets
- [ ] Tooling / scripts
- [ ] Bug fix
- [ ] Refactor (no behavior change)
- [ ] Docs only
- [ ] Other (describe)

## Test plan

<!--
What you ran and what you saw. For anything visual or anything you play,
say what you looked at (screenshot path, log line) instead of "works".
-->

- [ ] `Scripts/build.sh` succeeds
- [ ] `Scripts/test.sh` passes
- [ ] Added or updated automation tests (pure rules go in `CrabMovementMath` style headers so they test without a world)
- [ ] `Scripts/live-test.sh` passes (real game, real pointer)
- [ ] Played it, not just ran the tests

## Accessibility check

<!--
Anything that touches input, timing, camera or on-screen feedback should
answer this. Skip if irrelevant.
-->

- [ ] Everything is reachable with the pointer alone
- [ ] No new need for precision, fast input, cue-timed windows, key combos, or the scroll wheel
- [ ] No camera chores added (rotation, zoom)
- [ ] Feedback does not rely on colour alone

## Art and assets

<!-- Skip for code-only changes. -->

- [ ] Every third-party asset is CC0 or equivalent and recorded in `Art/assets/`
- [ ] Follows the contract in `Art/README.md` (names, orientation, units, texture packing)
- [ ] Scripts rebuild the assets headless (`Art/blender/`, `Art/unreal/`)
- [ ] Binary files are covered by the LFS rules in `.gitattributes`

## Notes for reviewers

<!-- Anything subtle, surprising, or worth a closer look. -->
