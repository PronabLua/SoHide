# HideSo

HideSo is a modular Android ARM64 static library for process-local CTF experiments. Its build artifact is exactly `hideso.a`.

The library contains no `main`, `JNI_OnLoad`, constructor, injector, package targeting, `ptrace`, Java, or root workflow. The caller owns startup and passes an address belonging to the loaded shared object that should be concealed.

## Runtime target

The integration surface follows the native runtime conventions used by `OG_LOADER/runtime`:

- Android ARM64 only
- C++20 with extensions disabled
- `-Wall -Wextra -Werror`
- hidden visibility
- no exceptions or RTTI
- 16 KiB-compatible final ELF link options
- no additional dynamic exports required

HideSo operates only inside the current guest process. It conceals the native shared object containing the supplied address; it does not hide the complete APK, Java classes, Binder state, guest configuration, or application process.

## Public API

The only installed header is `include/HideSo/HideSo.h`.

```cpp
#include <HideSo/HideSo.h>

HideSo::Request HideRequest = {(const void*)&Initialize, HideSo::Default};
HideSo::Result HideResult = {};
HideSo::Status HideStatus = HideSo::Calls(HideRequest, &HideResult);
```

`HideSo::Calls` is the operation entry point. `HideSo::StatusText` converts its status to a stable diagnostic string. All ELF parsing, mapping, linker discovery, and memory patching APIs remain private under `src` in the `HideSoPrivate` namespace.

The request contains:

- `ModuleAddress`: any function or object address inside the shared object to conceal
- `Options`: an option mask, normally `HideSo::Default`

The result reports the status, completed stages, resolved image base and path, mapping count, and remapped byte count. The result pointer may be null when the caller only needs the returned status.

## Modes

`HideSo::Default` performs these stages:

1. Replace the target image's named file-backed `PT_LOAD` mappings with private anonymous mappings at the same addresses.
2. Remove the target image from Bionic's global `soinfo` list.
3. Verify that the original mappings and linker enumeration entry are absent.

`HideSo::Aggressive` also clears the in-memory ELF magic. That mode can break native unwinding and diagnostics through the concealed module.

Individual flags can be combined when a challenge needs only part of the operation.

## Runtime integration

Add HideSo to the runtime's native CMake build:

```cmake
add_subdirectory(path/to/SoHide HideSoBuild)
target_link_libraries(GuestRuntimeNative PRIVATE HideSo::HideSo)
```

Call it from the runtime's explicit initialization path after `JNI_OnLoad` and native registration have completed:

```cpp
HideSo::Request HideRequest = {(const void*)&Initialize, HideSo::Default};
HideSo::Result HideResult = {};
if (HideSo::Calls(HideRequest, &HideResult) != HideSo::Success)
{
    NativeRuntimeBridge::ThrowInitializationError(Environment, HideSo::StatusText(HideResult.OperationStatus));
    return;
}
```

Call it once, before worker threads start. The containing shared object must remain loaded for the process lifetime. Do not call `dlclose` or expect later `dlsym` discovery after unlinking it.

## Standalone build

```powershell
$env:ANDROID_NDK_HOME = "C:\Android\ndk\27.0.12077973"
cmake --preset android-arm64-release
cmake --build --preset android-arm64-release
```

The archive is written to `build/android-arm64-release/hideso.a`.

## Compatibility

The minimum platform is Android 10/API 29. The implementation recognizes the Bionic linker list names used through Android 16 and the `solist_head`/`solist_tail` names introduced in Android 17. It derives the live `soinfo::next` offset instead of choosing a hard-coded release layout.

The process-local hide stages were validated without root on a physical Pixel 8a running Android 17/API 37. The static archive builds cleanly with NDK r24, r27, and r30. Modified or stripped vendor linkers can be unsupported; discovery fails without applying a speculative linker patch.

Anonymous executable mappings remain observable, and memory scanners can still identify code or data. HideSo is a CTF primitive, not a claim of complete process invisibility.

## License

MIT
