# First playable acceptance plan

Development proceeds through gates. A later gate does not begin until the earlier one is stable and measurable.

## Gate 0: Toolchain and module

- [x] UE 5.8.2 recognizes the project.
- [x] Development Editor / Win64 builds successfully.
- [x] DX12 and Shader Model 6 run on the RTX 4070 Laptop GPU.
- [x] The SandSimulation plugin loads and resolves its shader directory.
- [x] A compute pass writes a GPU buffer and passes CPU readback validation.

Gate 0 passed on 2026-09-07. The adapter is selected explicitly in unattended tests because this laptop also exposes an Intel integrated GPU.

## Gate 1: Three-dimensional material

- [x] Material and solver parameter contracts exist in SI units.
- [x] A compute pass creates stratified material points throughout a true 3D volume.
- [x] GPU readback verifies point bounds, rest velocity and density-derived total mass.
- [x] A small material-point volume transfers to and from a three-dimensional grid.
- [x] The P2G pass conserves mass through contended GPU atomic accumulation.
- [x] Grid gravity transfers back to material points without horizontal drift.
- [x] Floor collision and container walls work across repeated advected steps.
- [x] Fixed-step results do not depend materially on render frame rate.
- Debug slices expose mass, velocity and active nodes.

## Gate 2: Stable dry sand

- [x] A small-strain elastoplastic MPM update applies a Drucker-Prager shear projection.
- [x] The diagnostic pile settles without NaNs, escape or continuing bulk flow over the 1.2 s capture.
- [x] A 3D column-collapse test produces repeatable runout and exports review frames.
- [x] Particle mass is immutable; the contended P2G validation conserves total mass within 1e-5 kg.

The first diagnostic passed on 2026-09-07 using 3,584 particles, a 5 cm grid and 300 Hz internal steps. The initial 0.688 m column settled to 0.455 m and its released face advanced by approximately 0.23 m. This is a numerical acceptance test, not yet a calibration against a laboratory sand sample.

## Continuous-surface reconstruction milestone

- [x] A GPU compute pass splats three-dimensional material samples into a density field.
- [x] The diagnostic surface uses a 2.5 cm voxel grid independently of the coarser physics test grid.
- [x] A compact three-dimensional density filter removes visible material-point lattice aliasing.
- [x] UE GeometryCore Marching Cubes generates a continuous shared-vertex volume mesh.
- [x] Density-gradient normals and a high-roughness sand-color material produce a readable UE viewport preview.
- [x] An automation test checks non-empty GPU density, valid mesh indices, shared vertices and finite positions.
- [x] The surface reads the evolving persistent MPM particle buffers at the bounded surface-update rate.
- [ ] Density filtering and surface extraction run entirely on the GPU at the target update rate.
- [ ] The production mesh path supports shadows, temporal stabilization and close-range sand detail.

The prototype passed on 2026-09-08. The retained GIF and contact sheet are actual UE 5.8 DX12/SM6 frames from the evolving runtime surface, including scoop, carry and release.

Runtime integration:

- [x] Shared MPM particle/shader code is available to runtime actors rather than only automation tests.
- [x] Surface reconstruction accepts arbitrary MPM particle-position frames with bounded in-flight work.
- [x] A Development Game build contains the GPU collapse playback actor and compact Chaos excavator pawn.
- [x] Independent Editor/game validation and a continuous-surface excavation capture pass.
- [x] The MPM state remains persistent frame-to-frame and accepts moving track/bucket colliders.

## Gate 3: Simple moving boundary

- [x] A plate can push sand and form a wedge in front of it.
- [x] Sand flows around both sides of the plate.
- [x] Boundary impulses are measured on the GPU.
- [x] A material-point contact pass prevents persistent tunneling at the accepted tool speed.

## Gate 4: Bucket cycle

- [x] A thickened open-bucket moving boundary can cut into sand.
- [x] Sand enters and remains in the bucket during lifting.
- [x] Bucket rotation pours the load back into the sandbox.
- [x] Removing support leaves the surrounding MPM volume free to collapse.

This gate is the principal technical go/no-go milestone.

## Gate 5: Minimal excavator

- [x] The simplified chassis drives and turns on the sand with track-traction stabilization.
- [x] Four live MPM-surface samples support chassis height and stable pitch/roll; gravity moves an unbraked chassis down excavated slopes.
- [x] Robust top-surface sampling and filtered bucket reaction remove visible idle/drive chatter from individual particle contacts.
- [x] Four rigid retaining walls stop the vehicle at the sandbox edge while leaving the top open.
- [x] The boom, stick and bucket have independent driven joints.
- [x] Aggregate sand impulses affect the excavator with a bounded reaction coupling.
- [x] The player can repeat dig, lift, move and dump cycles.
- [x] Full dump reliably releases carried particles even while the chassis is pitched.

## Gate 6: First playable presentation

- [x] The 6.25 cm dense full-volume baseline runs at a 30 Hz simulation target on the RTX 4070 Laptop.
- [x] Chassis, arm and camera render at a capped 60 FPS; the expensive bulk-surface mesh updates asynchronously at 10 Hz.
- [x] A GPU-density/CPU-Marching-Cubes surface covers the bulk material.
- [x] Single-winding shared geometry, stable micro-displacement and a procedural dry-sand material improve surface readability without changing bulk mass.
- Materials and Niagara add close-range detail without owning bulk mass.
- [x] All 2 m of initial sand remains active and excavatable down to the container floor.

First-playable validation on 2026-09-08: 204,800 persistent points, 80,000 kg
initial sand mass, 7 moving tool colliders, successful scoop/hold/release cycle,
7/7 automation tests, approximately 20-28 ms typical warmed GPU+readback time (with
occasional surface/readback spikes around 37 ms), and a
2,062 MB peak private-memory measurement for the packaged automated run. The
remaining unchecked rendering items are presentation upgrades, not blockers for the
mechanical first playable.

## Measurements retained for every performance capture

- Active and total material-point count
- Active grid-node and block count
- Simulation substep count
- P2G, grid update, collision, G2P and surface timings
- GPU memory used by every persistent and transient buffer
- Total sand mass and mass error
- Maximum speed and penetration correction
- Number of sleeping and waking blocks
