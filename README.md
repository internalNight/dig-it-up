# Dig It Up

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

Current playtest: **Dig It Up — Easy**, a 5 × 5 m sandbox with 1.5 m of active sand.
Uncover approximately 10 × 10 cm of the red floor; Victory appears 3 seconds later.
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
- One 5 m x 5 m x 3 m sandbox
- Four rigid retaining walls; the upper boundary remains open
- Initial sand volume occupies the lower 2 m; every layer remains excavatable
- Open upper region with 1 m initial headroom
- 6.25 cm dense full-volume physics cells (80 x 80 x 32 material layout, 204,800 points)
- 5 cm independent surface-reconstruction voxels
- 30 Hz outer physics, 300 Hz internal MPM steps, 60 FPS game rendering and 10 Hz asynchronous bulk-surface updates
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
the boom, **R/F** the stick and **T/G** curls/dumps the bucket. **H** hides or shows
the help panel; **Esc** exits the packaged game.

The workspace name contains non-ASCII characters, which the current MSVC/UE PCH path can mis-handle. Project generation and command-line builds therefore use the existing directory junction `D:\UEProjects\SandExcavator`; it points at this same workspace and does not duplicate source files.

The lightweight Entry map spawns the 5 m x 5 m x 3 m sandbox, outdoor lighting,
continuous sand surface and excavator automatically. In editor tooling, a **Sand
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

Measured on the RTX 4070 Laptop: warmed MPM work is typically 20-28 ms per 30 Hz
outer frame, with occasional surface/readback spikes around 37 ms; the optimized asynchronous surface mesh is about 46-53 ms at 10 Hz. The packaged
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
