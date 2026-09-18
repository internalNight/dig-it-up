# Android excavator first playable

The Android build starts directly in the excavator. A left touch stick drives
forward/backward and steers left/right. Three independent right-side levers move
the boom, stick and bucket. A lever moved upward gives the same positive joint
command as Q, R or T on the desktop. All four controls return to zero when
released. Each finger remains assigned to the control where it first touched,
so the player can drive and operate multiple joints together.

The desktop keyboard controls are unchanged. Run `PlayMobileExcavatorPreview.cmd`
to check the mobile layout at 1600 x 900 on Windows. The mouse emulates one
finger. The stick base stays anchored in the lower-left corner; drag its knob
from the fixed centre to drive and steer. Use an Android device to test
simultaneous touches. Run
`Tools/PlayMobileExcavatorPreview.ps1 -Capture` to make an automatic screenshot
at `Artifacts/SandSurfaceUEPreview.png`. Add `-Performance` to show the
diagnostic overlay in a desktop preview.

On Android the simulation defaults to the `Mobile` quality tier: 10 cm MPM
cells, 37,500 sand particles in the standard 5 x 5 x 1.5 m bed, and an
8 cm visual surface grid. The four track support patches now scale with cell
size and require floor-connected particles. When the visible mesh has a gap
at a tread, the coarse tier can use that physical bearing patch rather than
dropping a support point. The command-line option `-SandQuality=Legacy` or
another explicit tier still overrides it.
The Windows preview selects the same tier explicitly.

The project must be cooked and tested on a Vulkan-capable Android device with
the UE 5.8 Android toolchain installed. The MPM solver uses GPU compute shaders
and per-frame GPU readback. A successful Win64 compile or screenshot does not
establish Android shader compatibility, frame rate, stability, or touch behavior
on a real device. Record the device/GPU, Android version, UE renderer, FPS,
GPU frame time, particle count, and any shader or readback errors when changing
physics resolution. The user tested the previous version on an Honor Magic6
(Snapdragon 8 Gen 3) and reported smooth operation, but the excavator sank and
the mobile sky rendered a warning.
Version 1.1 enlarges the left stick by about 30%, removes its rectangular
backdrop, and does not spawn the unsupported SkyAtmosphere actor on Android.
Version 1.2 anchors the stick base and directional centre and limits touch
capture to the area around the stick. The desktop preview enables mouse-to-touch
input for one-finger interaction.
Version 1.3 retains the 30 Hz physical outer step, uses eight instead of ten
internal MPM steps per outer step, and lets the mobile surface rebuild whenever
its prior job finishes, up to 30 Hz. The simulation now accumulates wall time
while a GPU step is in flight. A Development Android build displays Game FPS,
Sand Hz, Surface Hz, solver/readback time and surface-build time in the top-right.
Wait five seconds after launch before reading these values. For the evidence
and limitations, see `Docs/MobilePerformance.md`.

On the current development machine, UE 5.8.2 recognizes Android API 36,
Build Tools 36.0.0, CMake 3.22.1 and NDK 27.2.12479018 (r27c). The Android
arm64 C++ target compiled and `RunUAT BuildCookRun` completed an Android ASTC
Development cook, package and archive on 18 September 2026. The current test
build is `Artifacts/AndroidPerformance13/SandExcavator-arm64.apk` (version
1.3, code 4, about 160 MB). `bPackageDataInsideApk=True` embeds
`assets/main.obb.png`, so this APK can be installed as one file. The archive
also contains `Install_SandExcavator-arm64.bat` for installation through ADB.
The older `Artifacts/AndroidFixedJoystick/` package is version 1.2,
`Artifacts/AndroidMobileFixFinal/` is version 1.1, and
`Artifacts/AndroidSingleApk/` is version 1.0. The earlier
`Artifacts/AndroidPackage/` build keeps its content in a separate OBB and
must be installed with its script.

The generated Gradle build failed on this Windows host with a Java loopback
connection error when using the default temporary directory. Setting both
`TEMP` and `TMP` to the short local path `C:\UEGradleTemp` made the build pass.
UE Live Coding prevented `RunUAT -build` while the editor was open, so the
successful packaging run reused the separately compiled Android binary. No
Android phone was connected during packaging. APK signature and package
metadata were checked. In an isolated Win64 copy, a 20-second idle run kept
all 4 supports and a 154.2 cm chassis height from 2 to 20 seconds; an
eight-second digging run retained all 4 supports. These desktop runs cannot
establish the revised APK's frame rate, touch behavior or bearing performance
on the Honor Magic6. Check those on the device before calling this fix done.
The 1.2 Windows preview rendered the anchored stick at 1600 x 900. This machine
has no installed Android emulator image, and automated mouse injection could
not focus the game window, so a live one-finger drag was not verified here.
