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

On the current development machine, `Build.bat SandExcavator Android Development`
reports `Sdk: not found. Required version r27c`, so an APK has not yet been
produced. Install the UE 5.8 Android toolchain through **Platforms > SDK
Management > Android > Install SDK** and accept the Android SDK licenses. UE
5.8 requires NDK r27c. After UE recognizes the SDK, package an Android ASTC
Development build, launch it on a phone, and validate all four simultaneous
touches and the GPU sand solver before treating it as a mobile release.
