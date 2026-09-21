# Dig It Up

## Lunar field (2026-09-20)

The default source-built scene is now a lunar excavation field: a **100 x 100 m
driveable granular field** streams through a 15 x 15 m GPU-MPM window inside a
8,192 m visual lunar horizon. Its detailed inner 2,048 m landscape uses a
central 1,024 m embedded LROC relief crop, with highlands, a mare-like basin,
impact craters and scattered rocks. The objective is now a **22 cm faceted golden
specimen**, randomly buried 30 cm below the local surface. Press **V** to show its
bearing for five seconds, travel across the field, then excavate it. Double-click
`PlayLunarWorld.cmd` to run it, or use `-SandLegacyBox` for the earlier box scene.
The airless sky includes a low, nearly fixed Earth at its lunar-surface angular
size, a half-degree Sun, a deliberately exposure-assisted Milky Way star field
and a slowly orbiting survey satellite.

The large landscape uses measured relief from the LROC `NAC_DTM_NOBILE03`
product plus clearly documented designed features. It is not presented as one
literal surveyed site. Only the local window is solved on the GPU at one time;
five-metre chunks follow the excavator, preserve their full particle state for the
session and leave a lightweight frozen deformation surface outside the live
window. A once-built 0.5 m visual mesh keeps the complete 100 m field and its
craters visible at long range, while actual track samples stamp shallow grooves
into frozen chunk heightfields across window changes without overlay geometry. See
[source, architecture and limitations](Docs/LunarWorld.md).

## Android excavator first playable

The Android version opens directly in the excavator with one left drive stick
and three right-side boom, stick and bucket levers. See
[Android controls, Windows touch preview and device validation](Docs/AndroidExcavator.md).

## Excavation research lab (2026-09-13)

`PlayExcavationLab.cmd` opens the experimental dry-sand material, finite traction
and load-limited feed mode. Use `Tools/PlayMachines.ps1 -Head Helix` to choose a
prototype; available heads are Paddle, Chevron, BucketWheel, Spoke and Helix.
The [Chinese research and implementation report](Docs/ExcavationLabV2.md) links
market products, papers, material assumptions and [measured results](Docs/ExcavationLabResults.md).
The lab includes a blade soil bin, grid/time-step sensitivity and no-throttle
holding tests. It is not calibrated against physical sand. The axial helix now
passes the baseline full-sandbox functional gate; grid/time-step convergence and
physical calibration remain open acceptance gates.

The axial segmented-helix configuration now has its own physical casing, trough,
belt transfer, load-limited depth control and ballasted tracked chassis. Launch it
with `PlayHelixRoadheader.cmd`; see the [Chinese Helix V1 validation report](Docs/HelixV1/README.md)
for repeat runs, stopped-belt controls, failure causes and numerical limitations.

## Experimental physical roadheader (2026-09-10)

Run `PlayMachines.cmd` to choose an excavator or a roadheader. The roadheader uses
a rotating paddle drum and a closed-loop scraper conveyor with MPM solid contact;
it does not capture or teleport sand. Press T for motors, G to reverse, Q/E to
raise/lower the cutting assembly and C for the internal view. `PlayRoadheaderBench.cmd`
opens the finer isolated transport experiment.

The follow-up [transport fix](Docs/TransportFix.md) adds a 240 Nm low-speed cutter
drive, guide plates and a local conveyed-sand surface. Material-point sphere
markers are now opt-in with P. Physics still uses the same conserved particle set.

The new default full-sandbox grid is 5 cm / 300,000 material points / 600 Hz
internal steps. `-Quality Fine` selects 3.125 cm / 1,228,800 points / 900 Hz;
this is substantially slower on the tested RTX 4060 Ti 8 GB. The 2.5 cm option
uses 1,200 Hz steps and has been tested on the small bench, not the full sandbox.
See [implementation, controls and measured limitations](Docs/RoadheaderPrototype.md).
The portable-build notes below retain the original excavator baseline.

A Windows single-player digging prototype built with Unreal Engine 5.8. The internal
UE project/module name remains `SandExcavator`.

## Build from this repository

1. Install Unreal Engine 5.8 and Visual Studio with the C++ game-development workload
   and a compatible Windows SDK. The tested setup used UE 5.8.2 and MSVC 14.51.
2. Clone into a short ASCII-only directory, for example `D:\Projects\dig-it-up`.
   All project-specific source, shaders and material assets are included; generated
   binaries, UE itself, packaged games and local test captures are not.
3. Right-click `SandExcavator.uproject`, generate Visual Studio project files, then
   build the **Development Editor / Win64** target. Open the project and press Play
   in the default Entry map. The game mode generates the sandbox at runtime.
4. To package, use UE's Windows packaging workflow or `RunUAT BuildCookRun`. The local
   packaging/test helpers under `Tools` use example developer paths; override their
   parameters for your installation. `PlaySandPreview.cmd` is a convenience launcher
   for the original workspace, not a prebuilt game distributed by a source clone.

The original portable Easy build has 1.5 m of active sand, 6.25 cm physical spacing, 4 cm
surface reconstruction, silver-grey material and an 85-degree boom raise limit.
It requires Windows x64 and a DX12/SM6-capable GPU. There is no terrain save/load.
Only an RTX 4070 Laptop / 16 GB system has been tested; framerates are targets, not
guarantees. See [visual update](Docs/VisualPolish.md) and
[portable distribution notes](Docs/PortableRelease.md). No open-source license is
granted by this upload; Unreal Engine and third-party components keep their own terms.

## Project and milestone notes

Current playtest: **Dig It Up — Lunar Field**, a 100 × 100 m driveable regolith
field with a moving 15 × 15 m active physics window inside an 8.192 km visual
lunar context. Use the timed mineral bearing to find and expose most of the
buried golden specimen; Victory appears 3 seconds later.
Continue Digging preserves the excavation, or Exit Game ends the session.
See `Docs/EasyLevel.md` for the current level and acceptance evidence. The baseline
and earlier milestone measurements below describe the original 2 m prototype.

`SandExcavator` is a UE 5.8 C++ first playable for a small excavator interacting with a fully three-dimensional volume of dry sand.

The runtime plugin uses RDG compute passes and an APIC/MLS-MPM-style transfer with Drucker-Prager frictional plasticity. Chaos owns the excavator chassis; the GPU solver owns persistent sand state and returns aggregate impulses for two-way coupling.

## Physical bucket-wheel transport prototype (2026-09-13)

Use `PlayRoadheaderTransport.cmd` for the new source-built transport profile. It
loads `Config/RoadheaderTransport.json`: a six-pocket wheel, continuous belt,
finite drives, synchronized uncapped linear reaction, and load-aware depth/feed
assistance. The older packaged preview launcher does not include these changes.

Press **T** to start the motors, then **W** to feed into the level sand bed.
**Q/E** takes manual control of lift; **R** restores depth assistance; **C** changes
view and **P** shows the actual MPM samples. The HUD reports trough mass, actual
tail crossing and rear deposition separately. The profile requires the locally
built UE 5.8 Editor target; it is not a new portable package.

See [the Chinese optimization and validation report](Docs/TransportOptimization.md)
and [machine-readable acceptance](Docs/TransportV3/acceptance.json) for measured
results and limitations. The support model and material remain uncalibrated;
functional transport does not establish real-machine performance.

## Baseline target

- Windows PC, DX12 and Shader Model 6
- One 100 m x 100 m driveable field with a following 15 m x 15 m GPU-MPM window
- A 2.048 km detailed context plus a non-colliding 8.192 km horizon ring
- Camera-following celestial sky with 1.90 degree Earth, 0.53 degree Sun,
  exposure-assisted stars and a two-hour survey-orbiter path
- Three-by-three five-metre chunks remain live; visited chunks preserve their full
  particle state and a visible frozen deformation proxy while GPU load stays local
- The complete field remains visible on a permanent 0.5 m preview grid; window
  shifts update a mask instead of rebuilding terrain, and travelled tracks persist
- No visible retaining walls in the lunar profile; the upper boundary remains open
- Nominal 1.2 m granular depth with local highland and crater relief
- 10 cm dense full-volume physics cells by default (about 265,000 points initially)
- 12 cm independent surface-reconstruction voxels
- 30 Hz outer physics, 240 Hz internal MPM steps, 60 FPS game target and 15 Hz asynchronous bulk-surface requests
- One solver step per submitted lunar GPU job to avoid burst catch-up hitches
- RTX 4070 Laptop 8 GB development target
- Single player, one dry-sand material, no open world or networking

See `Docs/Architecture.md` and `Docs/FirstPlayable.md` before changing solver scope.

## Play the current build

Double-click `PlaySandPreview.cmd`. It prefers the packaged build at
`Builds/DigItUpVisual_20260909/SandExcavator.exe` (with the older first playable
retained as a fallback), so UE Editor does not need to be open.
Close UE Editor first on a 16 GB machine to avoid several gigabytes of avoidable memory use.

The in-game HUD shows live speed, the number of supported track sampling points,
and every control. **W/S** drive, **A/D** steer, **Space** brakes, **Q/E** controls
the boom, **R/F** the stick and **T/G** curls/dumps the bucket. In the lunar field,
the **arrow keys** orbit the camera, **C** changes close/regional scale and **V**
pulses the five-second mineral bearing. **H** hides or shows the help panel;
**Esc** exits the packaged game.

The workspace name contains non-ASCII characters, which the current MSVC/UE PCH path can mis-handle. Project generation and command-line builds therefore use the existing directory junction `D:\UEProjects\SandExcavator`; it points at this same workspace and does not duplicate source files.

The lightweight Entry map spawns the lunar field, continuous regolith surface
and excavator automatically. In editor tooling, a **Sand
Simulation Volume** actor still exposes the full domain, initial fill and SI-unit
material/solver parameters.

## Continuous-surface preview

The current renderer splats evolving material points to a 4 cm three-dimensional
density grid. A compact filter removes particle-lattice aliasing and UE GeometryCore
Marching Cubes builds a shared-vertex mesh with density-gradient normals. Stable
world-space micro-displacement and a high-roughness procedural sand material retain
small-scale detail without changing the simulated volume. One surface build is allowed
in flight, so CPU work cannot form an unbounded backlog.

For source development, open `SandExcavator.uproject` and press **Play**, or launch the lightweight game process directly:

```powershell
& 'D:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' 'D:\UEProjects\SandExcavator\SandExcavator.uproject' -game -d3d12 -sm6 -graphicsadapter=0
```

The chassis samples the evolving MPM surface at four points beneath its two tracks.
Each support intersects the visible volumetric mesh. A filtered average of the eight
highest non-carried points within a 10.5 cm patch provides a moving bearing envelope,
limiting visual sinkage to 3 cm and preventing a parked track from chasing its own
collision void downward. Lowering terrain is followed more quickly than rising terrain.
Neither the support height nor any layer of sand is locked to the initial surface.
Gravity and a damped vertical suspension follow excavated height, a bounded support-
plane constraint supplies stable pitch/roll on player-made slopes, and the tangential
gravity component lets an unbraked vehicle travel downhill. Space suppresses that
downhill force so it also acts as a genuine holding brake. This is a track-contact
approximation rather than individual track-link simulation.

The track collision shell is kept close to the rendered tread surface, and near-level
static track friction absorbs sub-centimetre asynchronous reaction drift. Bucket
reaction is separately dead-zoned, low-pass filtered and acceleration-limited, so
digging still loads the chassis without making it chatter at rest.

The open bucket is represented by five moving plates plus an interior carrier for
sub-grid material. It cuts the MPM volume, admits sand, keeps the captured mass during
lifting and releases it when the opening turns downward. No sand points are created or
deleted. Track traction prevents aggregate reaction impulses from unrealistically
sliding the simplified 12 kg chassis.

The full-dump state also uses the bucket linkage angle instead of relying only on its
world-space opening direction. This keeps unloading reliable when the chassis is
pitched on an excavated slope. Rendering is capped at 60 FPS independently of the
fixed 30 Hz MPM update, avoiding unnecessary GPU load above the prototype target.

Measured on the RTX 4070 Laptop for the legacy packaged 5 m build: warmed MPM
work is typically 20-28 ms per 30 Hz outer frame, with occasional surface/readback
spikes around 37 ms; its optimized asynchronous surface mesh is about 46-53 ms
at 10 Hz. The packaged
Development build peaked at 2,062 MB private memory during an automated run.

If the editor shows a large detailed landscape in an `Untitled` level, do not run the
preview in that map: PIE duplicates the landscape and can consume several extra
gigabytes. Use the packaged launcher, or choose **File > New Level > Empty Level**.

## Silver-grey / excavation-retaining preset (2026-09-09)

The playable actor now uses friction angle 40 degrees, equivalent cohesion 250 Pa
and velocity damping 0.65 /s. Baseline laboratory-style parameter defaults remain
30 degrees / 0 Pa / 0.15 /s. This is a gameplay-tuned dry granular-soil preset,
not calibrated lunar regolith; gravity and water content have not been changed.
The new `M_RegolithRuntime` asset supplies neutral silver-grey dust shading.
See `Docs/RegolithTuning.md` for the A/B pit test and interaction changes.
The packaged update is ready to run. An already-open Editor still has the previous
DLL loaded; rebuild the main project's Editor target after closing it before using
PIE for the new code. The launcher does not depend on that Editor rebuild.
