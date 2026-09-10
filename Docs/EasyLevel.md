# Dig It Up — Easy playtest

This is the first goal-driven level, built for the player's first successful dig.

- Sandbox: 5 × 5 m, initially filled to 1.5 m; 1 m of simulation headroom.
- All 153,600 material points remain active down to the floor (6.25 cm spacing).
- Retains the silver-grey material and 40° / 250 Pa / 0.65 per-second gameplay soil preset.
- Red floor top coincides with the physical floor at Z = 0. Mild emission keeps it readable in deep shadows.
- Camera inclination is 42° to provide a clearer view into the excavation.

Expose an approximately 10 × 10 cm continuous patch of red floor. The game remembers
the first qualifying exposure and shows Victory 3 seconds later, even if sand falls
back over it. Victory pauses the world. Click Continue Digging or press Enter to
resume exactly the same terrain, or click Exit Game / press Esc to quit. Success
is latched for this session and will not trigger repeatedly during free exploration.

The detector samples vertical visibility of the actual reconstructed 3D surface on
a 2.5 cm lattice. Four-by-four clear samples form the default qualifying patch.
This is an approximate geometric exposure measurement, not camera screenshot analysis.
It records initial surface coverage and excludes pre-existing reconstruction seams;
newly excavated patches beside walls and in corners can qualify. Tiny disconnected
holes do not add together. A 2 mm surface-height
tolerance is used at the floor. The HUD announces the countdown so it is readable
even when the player isn't looking directly at the qualifying patch.

Depth, patch side and countdown are centralized in `Config/DefaultGame.ini`, section
`/Script/SandSimulation.SandLevelSettings`. Source settings take effect after restarting
the level; changing the shipped defaults requires updating the packaged build. Only
the Easy preset is presented in this version. The proposed 2 m and 2.5 m levels are
reserved for later playtests.

## Validation

The `-SandVictoryTest` acceptance fixture starts in a prepared sloping corner excavation
with 12.5 cm of sand remaining at its lowest point. Excavated material is relocated
into the air region rather than removed; the point count and particle masses are
preserved. Scripted bucket motion clears the final layer. This fixture is never used
by the normal launcher.

Editor capture: first exposure at 1.515 s, Victory at 4.517 s (3.002 s delay), followed
by successful continuation through the HUD action handler. Physics continued and the
popup did not repeat. This verifies the finishing sequence, not a complete manual
playthrough from the initial flat sandbox.

Regression checks include full initial sand coverage, a 10 cm opening, a too-small
opening, reburial, and full-volume initialization at both the original 2 m depth and
the Easy level's 1.5 m depth. Existing numerical, GPU, surface and track-support tests
remain enabled. Detailed logs and captures are retained under `Artifacts/EasyValidation`.

Final automated regression: 9 passed, 0 failed, 0 warnings, including qualifying
corner openings and rejection of pre-existing seams. The independent game build
also exercised ordinary digging without a premature victory and the Victory exit
handler (clean process exit). The normal run sampled about 2075 MiB peak private
memory. The mild-emission floor material is explicitly included in the cook list.

The subsequent visual-polish update is documented in `Docs/VisualPolish.md`.
Launch `PlaySandPreview.cmd`, which now prefers `Builds/DigItUpVisual_20260909`.
Earlier packages are retained for comparison. The currently open main Editor may
still have the old module loaded; the independently built game is the playtest entry.

Existing approximations still apply: track bearing is simplified, bucket plates can
visually intersect the rigid floor at extreme joint poses, and dynamic sand geometry
does not provide full camera collision. The first player test should focus on whether
the route to the bottom is understandable and whether the ending feels satisfying.
