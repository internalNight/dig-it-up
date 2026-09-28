# Desktop performance and sand detail — 2026-09-28

## Play

- The ready-to-run Windows build is `Builds/DesktopPerfDetail_20260928/Windows`.
  Both root launchers use it when present and otherwise use UE 5.8.
- For copying to another Windows PC, extract
  `Builds/DesktopPerfDetail_20260928/DigItUpDesktop_20260928.zip` and run
  `Windows/StartMain.cmd` or `Windows/StartSmooth.cmd`. The archive contains
  the executable, cooked content and required runtime files; it excludes test
  logs, screenshots and debug symbols. Its SHA-256 is
  `11EABDE065FEA9FBD13A3FCE8F40D311116662C3FC1E36D42DFECC6881FC89C6`.
- `PlayLunarWorld.cmd`: 10 cm MPM cells, 10 cm surface voxels, six internal
  substeps per 30 Hz outer step. This is the main desktop profile.
- `PlayLunarWorldSmooth.cmd`: 12.5 cm MPM cells, 10 cm surface voxels, eight
  internal substeps. This prioritizes frame pacing. Soil contact and settling
  differ from the main profile.
- `-SandInternalHz=240` restores the previous eight-substep lunar physics
  setting for comparison. The new surface extraction applies to both profiles.

The display grid is independent of MPM cell size. Neither profile claims
millimetre-scale physics or calibrated lunar regolith.

## Changes

- The live 15 m sand surface now uses 10 cm display voxels instead of 12 cm.
  In the same idle scene this increased extracted triangles from about 58,400
  to about 83,800 while keeping the physics window at 15 m.
- The 27-tap density smoothing kernel is calculated in three separable passes.
  Integer division occurs once at the end, preserving the old output at
  boundaries and in the interior. An automation check compares both methods.
- Mesh normals are accumulated from shared triangle geometry. This avoids six
  trilinear density queries per mesh vertex while retaining smooth shading.
  The top-normal regression prevents inverted lunar boundary faces.
- The lunar surface request is capped at one every two physical frames,
  with at most one surface build in flight. The GPU solver and surface builder
  continue to run asynchronously.

## Same-machine measurements

RTX 4060 Ti, 1280 x 720 hidden UE 5.8 development game, DX12/SM6, idle lunar
field. Figures below are warm two-second HUD windows, not a guarantee for other
hardware or active excavation. A separate screenshot was inspected at
1600 x 900 with the black boundary absent and all four supports present.

| Profile | MPM particles | Game | Sand | Surface | Solver + readback |
| --- | ---: | ---: | ---: | ---: | ---: |
| Previous 10 cm physics, 12 cm display, eight substeps | 265,483 | not logged | ~27–30 Hz from frame logs | ~9 Hz from frame logs | ~16–24 ms sampled |
| New main 10 cm physics, 10 cm display, six substeps | 265,483 | ~56–58 FPS | ~30 Hz | ~12 Hz | ~12–20 ms sampled |
| Smooth 12.5 cm physics, 10 cm display, eight substeps | 136,280 | ~60 FPS | ~30 Hz | ~15 Hz | ~12–14 ms sampled |

With eight substeps and 10 cm physics, the optimized 10 cm display build ran
at about 54 FPS, 27–28 Hz sand and 11 Hz surface. Reducing to six substeps
restored the 30 Hz physics target without changing particle spacing. Five
substeps were rejected: the surface became unstable, the machine rose, and
the mesh build grew to about 80 ms.

The smooth profile settled its parked chassis roughly 9–11 cm below the main
profile in these tests, although support remained 4/4. Use the main profile
when the excavation interaction or track height matters. The six-substep
profile passed a scripted scoop/carry/dump smoke run with 4/4 supports and
visible carried particles; this is not a numerical convergence validation.

The first rendered surface takes roughly 0.6 s while resources initialize.
The warm main-profile build was about 38–39 ms when uncontended. Build logs
also separately report GPU density, CPU mesh, game-thread upload and support
update times. `-SandPerfHud` shows live Game, Sand and Surface rates in a
development build.

The packaged main profile also passed a 1280 x 720 startup smoke test at
60 FPS, 30 Hz sand and about 10 Hz surface after warm-up on the same host.
At 1920 x 1080, the packaged main profile measured about 53–55 FPS, 30 Hz
sand and 8 Hz surface. The packaged smooth profile measured about 59–60 FPS,
30 Hz sand and 8–9 Hz surface. The latter uses the coarser MPM grid and has
the different settling behaviour described above. These are short idle runs;
active digging and other GPUs may differ.
