# Lunar world expansion

The default runtime scene is now a lunar excavation field instead of a visible
five-metre retaining box. It keeps the original physical digging loop while
separating the scene into two computational scales.

## What is physical

- The central **10 m x 10 m** patch is a full three-dimensional GPU MPM volume.
  Its nominal depth is 1.2 m, with local highland undulation and three small
  impact craters. Material points remain movable through the whole depth.
- The default 7.5 cm cell spacing uses 283,807 material points. Eight internal
  steps per 30 Hz gameplay step and a 15 Hz asynchronous surface request keep
  deformation responsive on the RTX 4070 Laptop target. `-SandQuality=Fine`
  remains available for experiments, but is not the performance default.
- The excavator, bucket contact, carried material, deposition and goal detector
  continue to use the same persistent particle set. No visible sand is deleted
  and regenerated to fake excavation.
- Ten irregular convex rocks in the playable patch are independent **Chaos
  rigid bodies** with mass, rotation and collision. The vehicle can push them;
  a particle-height bearing spring lets them settle partly below the current
  granular surface. This is one-way sand bearing, not a fully coupled
  rock-to-MPM reaction: the rock responds to the sand surface but does not yet
  transfer an equal force back into individual material points.

## What is environmental context

The surrounding **2,048 m x 2,048 m** terrain is a lightweight procedural mesh.
Its central measured backbone is a fixed 256 x 256 crop of the LROC NAC DTM product
`NAC_DTM_NOBILE03`. A 129 x 129 embedded height table is interpolated onto a
257 x 257 runtime mesh. The source
product describes terrain near Nobile crater rim and Mons Mouton at 4 m/pixel.
The outer 512 m band on each side is explicitly synthetic low-frequency horizon
relief; the source crop is tapered rather than stretched beyond its footprint.

Official source and product page:

- <https://data.lroc.im-ldi.com/lroc/view_rdr_product/NAC_DTM_NOBILE03>
- <https://pds.lroc.im-ldi.com/data/LRO-L-LROC-5-RDR-V1.0/LROLRC_2001/DATA/SDP/NAC_DTM/NOBILE03/NAC_DTM_NOBILE03.TIF>

`Tools/PrepareLunarTerrain.py` documents the crop and reproduces the embedded
`LunarNobile03Height.inl` table from that TIFF. The large terrain preserves the
measured Nobile relief, then adds a designed mare-like lowland and analytical
impact craters to provide the requested variety. It is therefore a **documented
composite lunar scene**, not a claim that highland, mare and every displayed
crater occur together at one surveyed coordinate.

The macro terrain and its larger distant rocks are static visual context. Only
the central 10 m square and ten near-field rigid rocks are interactive. A 128 m
transition terrain overlaps the physical patch; the finite marching-cubes side
faces are culled so the old square black rim and pool-wall appearance are not
rendered.

## Visual direction

The lunar profile uses an unlit airless-sky material, low-angle directional
sunlight, weak fill, dark mare material, brighter highlands and deterministic
rock placement. Near rocks use a charcoal procedural-noise material over an
80-face irregular mesh instead of scaled spheres or ellipsoids. The lighting
deliberately avoids Earth atmosphere and height fog.

## Measured runtime check

The same 1,600 x 900 automated excavation pass was captured before and after
this revision on the development machine. The earlier 6.25 cm build used
493,621 particles, rendered about 23 FPS and reported 85--87 ms solver/readback
with about 60 ms surface builds. The revised default uses 283,807 particles,
rendered 53--54 FPS, maintained 30/30 Hz sand time and normally reported
18--35 ms solver/readback with 38--53 ms asynchronous surface builds. These are
development-machine measurements, not a general hardware guarantee.

The material parameters and gravity are still the project's gameplay baseline:
friction angle 40 degrees, equivalent cohesion 250 Pa, damping 0.65 /s and Earth
gravity. They have not been calibrated as lunar regolith and lunar gravity has
not been enabled; changing gravity would alter the established machine handling
and excavation objective and needs a separate tuning/validation pass.

## Run and fallback

Double-click `PlayLunarWorld.cmd` for the source-built lunar scene. The game still
uses W/S, A/D, Space, Q/E, R/F and T/G for the excavator. The original objective
is retained as a buried survey marker: expose about 10 cm x 10 cm and hold the
opening for three seconds. Press **C** to switch between the close excavation
camera and a regional overview that reveals the highlands, mare-like basin and
larger impact craters.

Use `-SandLegacyBox` to bring back the five-metre retaining-box scene for A/B
comparison and regression testing.
