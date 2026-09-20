# Lunar world expansion

The default runtime scene is now a lunar excavation field instead of a visible
five-metre retaining box. It keeps the original physical digging loop while
separating the scene into two computational scales.

## What is physical

- The driveable field is **100 m x 100 m**. A following **15 m x 15 m** window is
  the resident three-dimensional GPU MPM volume. Its nominal depth is 1.2 m,
  with mare-to-highland relief and impact craters. Material points remain
  movable through the whole resident depth.
- The live window contains a 3 x 3 set of five-metre chunks and snaps in 5 m
  increments, so consecutive windows overlap by 10 m. On a shift, overlapping
  chunks retain their current particles, departing chunks are copied to a CPU
  cache, and new chunks are seeded from the same continuous terrain function.
  Returning to a cached chunk restores its excavated/deposited state rather than
  resetting it.
- The cache can hold 512 five-metre chunks, more than the complete 100 m field
  plus its half-window edge buffer. Consequently, an in-bounds traversal cannot
  evict an earlier excavation during the same session. CPU memory is allocated
  only for visited chunks; a fully visited field can approach roughly 1 GB of
  raw particle-state storage.
- Every departed chunk also produces a 33 x 33 visual heightfield. Historical
  tracks, pits and deposited piles therefore remain visible outside the active
  physics window. This frozen proxy does not continue simulating; when the live
  window returns, it is hidden and the exact cached particles resume.
- The default 10 cm spacing aligns exactly with 50 cells per 5 m chunk and uses
  about 265,000 material points at the landing site. Terrain height changes the
  resident count slightly. Eight internal
  steps per 30 Hz gameplay step and a 15 Hz asynchronous surface request keep
  deformation responsive on the RTX 4070 Laptop target. `-SandQuality=Fine`
  remains available for experiments, but is not the performance default.
- The excavator, bucket contact, carried material and deposition continue to use
  the persistent particles inside the resident window. Streaming never deletes
  or teleports particles to imitate a bucket action; first-time chunks are
  initialized terrain, while visited chunks use their cached physical state.
- Window movement uses a two-phase visual handoff. The previous live surface and
  transition terrain remain in place until the replacement marching-cubes mesh
  has uploaded; only then does the terrain opening and historical trace proxy
  move. The old full-window objective floor has been removed. Approaching an edge
  now pre-generates one entering chunk per surface update, so ordinary driving
  spreads initialization over several frames. Only the three departing chunks
  are converted to frozen heightfields on a one-axis shift, instead of rebuilding
  all nine resident chunks.
- Historical deformation proxies no longer cast their own streamed chunk shadows.
  The continuous macro terrain carries the large-scale shadow, avoiding a square
  or delayed shadow flash while the proxy geometry changes; local rocks, machine
  and permanent terrain still cast hard lunar shadows.
- Ten irregular convex rocks in the playable patch are independent **Chaos
  rigid bodies** with mass, rotation and collision. The vehicle can push them;
  a particle-height bearing spring lets them settle partly below the current
  granular surface. This is one-way sand bearing, not a fully coupled
  rock-to-MPM reaction: the rock responds to the sand surface but does not yet
  transfer an equal force back into individual material points.

## What is environmental context

The surrounding **2,048 m x 2,048 m** detailed terrain is a lightweight procedural
mesh. A second non-colliding **8,192 m x 8,192 m** low-resolution ring continues
the relief to the visible horizon for only 30,968 additional triangles. Its
central measured backbone is a fixed 256 x 256 crop of the LROC NAC DTM product
`NAC_DTM_NOBILE03`. A 129 x 129 embedded height table is interpolated onto a
257 x 257 runtime mesh. The source
product describes terrain near Nobile crater rim and Mons Mouton at 4 m/pixel.
The source crop is radially feathered inside its 1,024 m square footprint so its
tile boundary cannot appear as straight ridges. Terrain beyond that measured crop
is explicitly synthetic low-frequency horizon relief rather than stretched source
pixels.

Official source and product page:

- <https://data.lroc.im-ldi.com/lroc/view_rdr_product/NAC_DTM_NOBILE03>
- <https://pds.lroc.im-ldi.com/data/LRO-L-LROC-5-RDR-V1.0/LROLRC_2001/DATA/SDP/NAC_DTM/NOBILE03/NAC_DTM_NOBILE03.TIF>

`Tools/PrepareLunarTerrain.py` documents the crop and reproduces the embedded
`LunarNobile03Height.inl` table from that TIFF. The large terrain preserves the
measured Nobile relief, then adds a designed mare-like lowland and analytical
impact craters to provide the requested variety. It is therefore a **documented
composite lunar scene**, not a claim that highland, mare and every displayed
crater occur together at one surveyed coordinate.

The macro terrain and its larger distant rocks are static visual context. The
100 m field becomes interactive locally as the physics window follows the
excavator; ten near-field rocks are independent rigid bodies. A 144 m transition
terrain moves its opening with the resident window; finite marching-cubes side
faces are culled relative to that moving window so the old square black rim and
pool-wall appearance are not rendered.

The normal camera is now lower and closer to keep the excavator and bucket work
readable. Pressing **C** changes to an oblique 1.2 km regional view with black sky
and the 8.192 km horizon. In that distant view the sub-pixel live MPM mesh and
cached trace proxies are temporarily hidden under the continuous analytical
terrain, eliminating the conspicuous square physics patch. Returning to the close
camera restores them; physics and cached deformation continue unchanged while the
overview is active.

The **left/right arrow keys** orbit around the excavator and **up/down** change
camera elevation in both camera scales. The orbit is continuous rather than a set
of fixed viewpoints, so the terrain, route and excavated trace can be inspected
from any azimuth.

## Lunar specimen mission

Each normal launch chooses a hidden point at least 16 m from the landing area and
inside the central 80% of the 100 m field. A 22 cm irregular faceted golden
specimen is placed with its centre 30 cm below the undisturbed local surface; it
is smaller than half the current bucket width. The former full-size buried survey
floor is not spawned in the lunar profile.

Press **V** to pulse the mineral detector. For five seconds an amber arrow appears
around the excavator and continuously reports the world-space bearing of the
specimen relative to the current camera. It gives direction, not an exact map
coordinate or distance, and can be pulsed again after it fades. Exposing at least
three of five samples across the specimen starts the existing three-second victory
confirmation. A six-centimetre mesh-seam tolerance prevents ordinary marching-
cubes gaps from being mistaken for excavation. `-SandGemSeed=N` gives a repeatable
location for QA; normal launches use a fresh seed.

## Visual direction

The lunar profile uses an unlit airless-sky material, low-angle directional
sunlight, weak fill, dark mare material, brighter highlands and deterministic
rock placement. Near rocks use a charcoal procedural-noise material over an
80-face irregular mesh instead of scaled spheres or ellipsoids. The lighting
deliberately avoids Earth atmosphere and height fog.

## Measured runtime check

The current 1,280 x 720 streaming acceptance run started with 265,483 particles,
then shifted through resident counts of 274,742, 277,488 and 285,241 as terrain
height changed. It returned to the landing-site cache with 265,483 particles,
retained 4/4 track supports, rendered 51 FPS at capture, maintained 30/30 Hz sand
time, and reported warmed sampled solver/readback times of 15.7--22.2 ms (with
one 38.7 ms shift sample). The captured warmed surface build was 53.9 ms. Outside
the resident window the run retained nine visible historical chunk proxies with
18,432 triangles. These are observations from the development machine, not a
general hardware guarantee.

After the edge-prefetch update, a paced two-boundary traversal measured 14.17 ms
and 18.32 ms of game-thread window preparation at the two commits. The earlier
instant-teleport stress path, which deliberately bypasses the approach distance,
still measured 27--32 ms for first-time chunks. This optimization reduces the
normal driving hitch; it does not claim that arbitrary teleports are hitch-free.

The separate regional-view capture rendered 54 FPS at capture after startup,
held approximately 30/30 Hz sand time and showed the black airless sky without
the former square active-window patch or straight DTM crop seams. The 8.192 km
horizon has no collision and does not expand the MPM or driveable domain.

Final gameplay regressions also covered the original loop rather than only the
landscape. The deterministic scoop cycle moved up to 125 material points, carried
22 points in the bucket and later disabled retention to dump them. Four window
shifts returned to the original 265,483-particle state with 4/4 track supports;
the nine inactive trace chunks remained visible. The rigid-rock impulse check
moved a 23.73 kg convex rock by about 1.10 m before it settled with its bottom
about 2.17 cm below the sampled granular surface. The scripted excavator can
reach roughly 20 degrees pitch and 17 degrees roll after digging directly beneath
itself; this is a deliberately severe automated manoeuvre and remains a handling
limit to watch during manual playtesting.

The 100-fold increase in interactive plan area therefore does not allocate a
100-fold GPU volume. GPU particle/grid cost follows the 15 m resident window;
CPU history grows with visited chunks up to the whole-field cache cap.

The material parameters and gravity are still the project's gameplay baseline:
friction angle 40 degrees, equivalent cohesion 250 Pa, damping 0.65 /s and Earth
gravity. They have not been calibrated as lunar regolith and lunar gravity has
not been enabled; changing gravity would alter the established machine handling
and excavation objective and needs a separate tuning/validation pass.

## Run and fallback

Double-click `PlayLunarWorld.cmd` for the source-built lunar scene. The game still
uses W/S, A/D, Space, Q/E, R/F and T/G for the excavator. Use the **arrow keys**
to orbit, **C** to switch between close excavation and regional overview, and
**V** to display the golden specimen bearing for five seconds. Travel toward the
bearing, excavate the specimen, and keep it exposed through the three-second
confirmation.

Use `-SandLegacyBox` to bring back the five-metre retaining-box scene for A/B
comparison and regression testing.
