# Dig It Up — visual / boom playtest

This update builds on the single Easy level: 5 × 5 m, 1.5 m of active sand.
It does not introduce the two deeper difficulty presets.

## Changes

- Main boom range is now −65° to +85° relative to the chassis, operated by E / Q.
  The previous source limit was +35°; the Easy update changed camera inclination
  to 42°, not this joint limit. This is a new range expansion, not a recovered
  historical +85° setting. Stick and bucket limits and speeds are unchanged.
- New `M_RegolithFine` material: brighter neutral silver-grey diffuse colour,
  lower-contrast broad variation, smaller grain shading and gentler bump strength.
  Old materials are retained. This is a stylized dry-sand appearance, not a
  measured lunar-soil optical model, and silver-grey does not mean metallic.
- Surface reconstruction spacing is 4 cm (formerly 5 cm). Its density grid has
  1,166,948 samples, up from approximately 0.62 million. This improves visible
  shape sampling; it does not make the physical sand particles smaller.
- MPM spacing remains 6.25 cm, with 153,600 material points, unchanged material
  mechanics and full-depth activity. Surface filtering and material shading still
  limit the smallest visible features. Surface refresh remains asynchronous and
  bounded to one pending job; rendering is capped at 60 FPS, not guaranteed at it.
- Native screen percentage is 100. The normal launcher requests 1920 × 1080;
  Unreal may fit a smaller window when the desktop work area is smaller.
- Victory uses a large gold runtime-rasterized bold font, rather than magnifying
  the old small bitmap font. Continue / Exit, Enter / Esc, pause and the latched
  three-second countdown are preserved.

## Acceptance checks

`-SandBoomRaiseTest` is a test-only input driver: it holds the brake and raises the
boom with ordinary joint-speed limiting after one second. It logs reaching 85°.
The normal launcher does not pass any test flags.

`-SandVictoryTest -SandVictoryContinueTest` still performs the prepared final dig,
captures Victory and resumes through the HUD action handler. This is a finishing
sequence test, not a full manual excavation of the initially flat sandbox.

Use `-ForceRes` with offscreen test runs to prevent Unreal fitting the requested
resolution to its headless display size. Check screenshot dimensions in the log;
command-line width and height alone do not prove an actual 1080p test.

## Verified build (2026-09-09)

- Automated regression: 9 passed, 0 warnings, 0 failures.
- Independent 1920 × 1080 build: the final-dig fixture displayed the sharp gold
  runtime font and resumed the existing terrain through Continue. First exposure
  to victory remained approximately 3 seconds. Peak sampled private memory was
  2,340 MiB for this run; the separate boom test sampled 2,350 MiB.
- Boom test reached +85° through normal Q-equivalent input, with four track
  supports at the logged endpoints. Screenshot retained as `Boom85.png`.
- Ordinary 25-second scripted dig / carry / dump: no premature victory, carried
  material returned to zero after dumping, clean process shutdown. Final screenshot
  reads 59.96 FPS; that is one observation, not a sustained-framerate guarantee.
  Sampled post-startup MPM GPU+readback was roughly 16–25 ms. Surface construction
  samples were about 105–112 ms end-to-end / 88 ms CPU mesh, limiting actual surface
  update frequency below its nominal 10 Hz during this test. Geometry refinement
  improves sampling but is not an improvement to the solver's time resolution.
- Logs, regression report and real game screenshots: `Artifacts/VisualValidation`.
  Earlier packages remain untouched. New playable: `Builds/DigItUpVisual_20260909`.
  The already-open main UE editor still has its earlier module loaded; use
  `PlaySandPreview.cmd` to play this independently built version.
- Game binary SHA-256:
  `84ADD73937AC1FB4496B061D170F382611D713AABA71E2B42DC9C5D9E601761A`.

These are short automated checks, not a long-session manual playthrough. Camera
collision, track bearing and below-floor bucket poses retain the existing prototype
approximations. No changes to the friction angle, cohesion or victory requirement
were made in this visual update.
