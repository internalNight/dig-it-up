# Windows USB playtest distribution — 2026-09-09

Artifact: `Releases/DigItUp_Windows_USB_20260909.zip` (373.5 MiB).
Unpacked folder: `Releases/DigItUp_Windows_USB_20260909/DigItUp` (642.7 MiB plus manifest).
Copy the complete folder, or copy and fully extract the ZIP. Start with `Start.cmd`;
`Start_720p.cmd` is the lower-render-resolution alternative. No gameplay changes
were made for this packaging task. This remains the verified Development playtest
binary, not a separately compiled Shipping build or signed commercial installer.

## Packaging checks

- Kept cooked `.pak`, `.utoc`, `.ucas` data and required runtime Engine/game trees.
- Did not include PDBs, Saved folders, Artifacts, historical logs, staging manifests,
  project source, or a development-machine shortcut. Original game builds were not
  deleted or overwritten. Original third-party notices remain included.
- Uses a relative launcher with no UE/editor fallback and no forced adapter index.
  UE's normal DX12 adapter selection is high-performance / discrete-preferring.
- C++ runtime DLLs are deployed beside the real game executable from the installed
  Visual Studio 2026 redistributable (14.51.36247). Its Microsoft-signed offline
  x64 installer is also included; the signature was verified before packaging.
  GameInput's offline installer is included as an optional fallback, not auto-run.
- Runtime libraries were copied from Redistributable, not System32 or Debug CRT.
  Local runtime files require maintenance with future game distributions.
- The launcher redirects project settings/logs to `%LOCALAPPDATA%/DigItUp/Saved`.
  It does not implement terrain save/load. OS/driver caches can still be written.
- 61 distributed files have SHA-256 entries; the manifest does not hash itself.
  The verifier passed under built-in Windows PowerShell 5.1. Hashes detect transfer
  damage; neither the manifest nor the game is publisher-signed.

## Relocation acceptance

The delivered ZIP was extracted into a fresh system temporary directory whose path
contains spaces and Chinese characters, outside both the project and validation
checkout. Child working directory was `C:/Windows`; its PATH contained only Windows
and System32. No adapter index was forced and a separate fresh UserDir was used.

- Normal dig/carry/dump: successful initialization and clean exit, no false victory.
- Prepared final dig: visible gold Victory after 3.002 seconds from first qualifying
  exposure, followed by a clean exit through the game's HUD exit handler.
- All cooked materials and title font rendered in the relocated copy.
- Observed `msvcp140.dll`, `msvcp140_1.dll`, `vcruntime140.dll` and `vcruntime140_1.dll`
  loaded from the extracted game's own binary directory, not installed system CRT.
- No loaded DLL came from the UE5, Visual Studio or UEProjects installation trees.
- All 61 packaged file hashes still matched after the tests. Test-only screenshot
  flags created captures in the temporary extraction, not in the distributed ZIP.
- Peak sampled private memory: 2,141 MiB normal run; 2,097 MiB victory/exit run.
  These 720p short runs are dependency/smoke checks, not new performance guarantees.

Evidence: `Artifacts/PortableValidation/Result.json`, `LoadedModules.csv`, logs and
screenshots. The temporary extraction is retained for diagnosis; it is not needed
to play or redistribute the ZIP.

## Compatibility limits

Target: Windows 10/11 x64, compatible DX12/SM6 GPU and current drivers. Only the
current RTX 4070 Laptop / 16 GB machine was tested. We did not test a clean OS,
another physical computer, AMD/Intel GPU, ARM emulation, or USB storage performance.
The same-host relocation test cannot establish all-machine compatibility. Runtime
dependencies are included; incompatible hardware or OS components are not fixable
by copying additional game files. The package has no terrain progress persistence.

References:

- https://learn.microsoft.com/en-us/cpp/windows/deployment-in-visual-cpp
- https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist
- https://dev.epicgames.com/documentation/unreal-engine/hardware-and-software-specifications-for-unreal-engine

## Reproduction

`Tools/PackagePortable.ps1` creates a new folder, manifest and ZIP. It refuses an
existing destination/archive; pass a new versioned Destination for later builds.
`Tools/TestPortable.ps1` extracts the specified ZIP and runs isolated acceptance
tests with module-origin and integrity checks. Neither script removes old releases.
