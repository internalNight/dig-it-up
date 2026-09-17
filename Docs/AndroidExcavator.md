# Android excavator first playable

The Android build starts directly in the excavator. A left touch stick drives
forward/backward and steers left/right. Three independent right-side levers move
the boom, stick and bucket. A lever moved upward gives the same positive joint
command as Q, R or T on the desktop. All four controls return to zero when
released. Each finger remains assigned to the control where it first touched,
so the player can drive and operate multiple joints together.

The desktop keyboard controls are unchanged. Run `PlayMobileExcavatorPreview.cmd`
to check the mobile layout at 1600 x 900 on Windows. The mouse emulates one
finger; use an Android device to test simultaneous touches. Run
`Tools/PlayMobileExcavatorPreview.ps1 -Capture` to make an automatic screenshot
at `Artifacts/SandSurfaceUEPreview.png`.

On Android the simulation defaults to the `Mobile` quality tier: 12.5 cm MPM
cells, about 19,200 sand particles in the standard 5 x 5 x 1.5 m bed, and an
8 cm visual surface grid. This is a deliberately coarse first playable. A
command-line `-SandQuality=Legacy` or another explicit tier still overrides it.
The Windows preview selects the same tier explicitly.

The project must be cooked and tested on a Vulkan-capable Android device with
the UE 5.8 Android toolchain installed. The MPM solver uses GPU compute shaders
and per-frame GPU readback. A successful Win64 compile or screenshot does not
establish Android shader compatibility, frame rate, stability, or touch behavior
on a real device. Record the device/GPU, Android version, UE renderer, FPS,
GPU frame time, particle count, and any shader or readback errors during the
first on-device run before increasing physics resolution.

On the current development machine, UE 5.8.2 recognizes Android API 36,
Build Tools 36.0.0, CMake 3.22.1 and NDK 27.2.12479018 (r27c). The Android
arm64 C++ target compiled and `RunUAT BuildCookRun` completed an Android ASTC
Development cook, package and archive on 17 September 2026. The preferred
test build is `Artifacts/AndroidSingleApk/SandExcavator-arm64.apk` (about
160 MB). `bPackageDataInsideApk=True` embeds `assets/main.obb.png`, so this
APK can be installed as one file. The archive also contains
`Install_SandExcavator-arm64.bat` for installation through ADB. The earlier
`Artifacts/AndroidPackage/` build keeps its content in a separate OBB and
must be installed with its script.

The generated Gradle build failed on this Windows host with a Java loopback
connection error when using the default temporary directory. Setting both
`TEMP` and `TMP` to the short local path `C:\UEGradleTemp` made the build pass.
UE Live Coding prevented `RunUAT -build` while the editor was open, so the
successful packaging run reused the separately compiled Android binary. No
Android phone was connected during packaging. APK signature and package
metadata were checked, but on-device rendering, frame rate, embedded content loading,
GPU readback and simultaneous touch controls remain to be verified.
