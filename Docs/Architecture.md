# Sand simulation architecture

## Non-negotiable invariants

1. The entire initial 5 m x 5 m x 2 m sand volume is movable and excavatable.
2. There is no fixed deep sand layer. Only the container floor and side walls are immovable.
3. Sleeping is a compute optimization, not a conversion to static terrain.
4. Sleeping material retains mass and the state required to resume simulation.
5. Removing support must wake material above and around the excavated region so collapse can propagate.
6. Sand mass must not be silently created, destroyed, or restored when chunks sleep or wake.
7. SI units are used inside the solver; Unreal centimeter conversion occurs at integration boundaries.
8. Rendering never owns authoritative sand state.

## Initial domain

- World extent: 5 m x 5 m x 3 m
- Initial sand extent: 5 m x 5 m x 2 m
- Initial air extent: 5 m x 5 m x 1 m
- Baseline cell size: 0.03125 m
- Baseline grid: 160 x 160 x 96
- Allocated grid nodes: 163 x 163 x 99, including one quadratic-kernel ghost layer on each side
- Occupied sand cells: 160 x 160 x 64 = 1,638,400
- Initial material-point target: four stratified points per occupied cell
- Block size: 8 x 8 x 8 cells
- Fixed simulation step: 1/30 s with adaptive internal substeps

The 2.5 cm grid (200 x 200 x 120) is a later quality target, not the first implementation target.

## Runtime ownership

### SandSimulation plugin

- Persistent material-point buffers
- Grid buffers and active-node compaction
- P2G and G2P transfers
- Drucker-Prager return mapping
- Gravity and boundary conditions
- Sleeping, waking and support-removal propagation
- Collider SDF sampling
- Aggregate rigid-body impulse output
- Density reconstruction and render-surface buffers

### Chaos

- Chassis rigid body
- Arm rigid body and driven hinge
- Bucket rigid body and driven hinge
- Container floor and walls

### Niagara

- Dust
- Sparse airborne grains
- Contact and sliding accents

Niagara does not own bulk sand mass.

## Excavator baseline

- Approximate overall length: 0.4 m
- Three rigid bodies: chassis, arm and bucket
- Two driven revolute joints
- Simplified left and right ground-contact patches
- Third-person orbit camera
- Keyboard and mouse controls
- Placeholder geometry until the physics interaction loop is stable

## Solver sequence

1. Build the active material-point and active-block lists.
2. Clear active grid nodes.
3. Transfer material-point mass, momentum and stress to the grid.
4. Apply gravity and internal forces.
5. Apply floor, wall, bucket, arm and chassis boundary conditions.
6. Accumulate collider impulses and torque.
7. Transfer updated grid motion back to material points.
8. Advect material points and update deformation/plastic state.
9. Update block activity, exposure and support state.
10. Reconstruct a renderable density surface.

## Quality policy

Physics and appearance have separate resolutions. The bulk solver targets centimeter-scale behavior. Surface reconstruction, procedural normals, roughness variation and sparse Niagara particles provide millimeter-scale visual detail without pretending to add physical information.

## Baseline dry-sand calibration

- Bulk density: 1600 kg/m^3
- Internal friction angle: 30 degrees
- Cohesion: 0 Pa
- Dilation angle: 5 degrees
- Effective Young's modulus: 250 kPa
- Poisson ratio: 0.20
- Initial relative compaction: 0.45
- Hardening rate: 4.0
- Tool/container friction coefficient: 0.50
- Restitution: 0.02
- Unresolved velocity damping: 0.15 /s

These are first-pass effective continuum parameters for clean dry sand, not a claim that one parameter set represents every grain-size distribution. Calibration is accepted only after the column-collapse and tool-push benchmarks are repeatable.
