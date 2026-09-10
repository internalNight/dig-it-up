# Physical roadheader implementation

Scope: selectable excavator / roadheader, visible rotating drum and closed-loop
scraper conveyor, contact-driven sand transport (no capture, teleport or deletion),
configurable finer MPM grid, local UE 5.8.2 build and runtime validation.

Hardware: 32 GB RAM, RTX 4060 Ti 8 GB. UE D:/UE_5.8, VS 2026 Community.
Build junction: D:/UEProjects/DigItUp.

Gates:
- [x] Compile Editor target with installed toolchain.
- [x] Validate collider trajectories and conservation/transport on a small bench.
- [x] Run both selectable vehicles and inspect images/logs.
- [x] Document measured limits and provide launchers.

See RoadheaderPrototype.md for measured outcomes and remaining engineering work.

Numerical fidelity is separate from visual resolution and material calibration.
The current small-strain frictional MPM and approximate chassis suspension remain
prototype models. Refinement alone does not establish experimental accuracy.
