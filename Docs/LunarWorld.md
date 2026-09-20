# Lunar world expansion

The default runtime scene is now a lunar excavation field instead of a visible
five-metre retaining box. It keeps the original physical digging loop while
separating the scene into two computational scales.

## What is physical

- The central **10 m x 10 m** patch is a full three-dimensional GPU MPM volume.
  Its nominal depth is 1.2 m, with local highland undulation and three small
  impact craters. Material points remain movable through the whole depth.
- The default 6.25 cm cell spacing bounds the enlarged patch to roughly half a
  million material points. `-SandQuality=Fine` is available for experiments,
  but is not the performance default.
- The excavator, bucket contact, carried material, deposition and goal detector
  continue to use the same persistent particle set. No visible sand is deleted
  and regenerated to fake excavation.
- Ten small rigid rocks are distributed in the playable patch. They collide
  with the vehicle, but the current GPU MPM solver does not yet solve two-way
  sand/rock coupling, so they are not claimed as movable granular inclusions.

## What is environmental context

The surrounding **1,024 m x 1,024 m** terrain is a lightweight procedural mesh.
Its backbone is a fixed 256 x 256 crop of the LROC NAC DTM product
`NAC_DTM_NOBILE03`. A 129 x 129 embedded height table is interpolated onto a
257 x 257 runtime mesh. The source
product describes terrain near Nobile crater rim and Mons Mouton at 4 m/pixel.

Official source and product page:

- <https://data.lroc.im-ldi.com/lroc/view_rdr_product/NAC_DTM_NOBILE03>
- <https://pds.lroc.im-ldi.com/data/LRO-L-LROC-5-RDR-V1.0/LROLRC_2001/DATA/SDP/NAC_DTM/NOBILE03/NAC_DTM_NOBILE03.TIF>

`Tools/PrepareLunarTerrain.py` documents the crop and reproduces the embedded
`LunarNobile03Height.inl` table from that TIFF. The large terrain preserves the
measured Nobile relief, then adds a designed mare-like lowland and analytical
impact craters to provide the requested variety. It is therefore a **documented
composite lunar scene**, not a claim that highland, mare and every displayed
crater occur together at one surveyed coordinate.

The macro terrain and its larger rocks are static collision context. Only the
central 10 m square is represented by material points. A 24 m transition mesh
blends the two surfaces and removes the old box-wall appearance.

## Visual direction

The lunar profile uses a black sky, low-angle directional sunlight, weak fill,
dark mare material, brighter highlands and deterministic rock placement. The
lighting deliberately avoids Earth atmosphere and height fog.

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
