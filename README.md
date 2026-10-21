# SoHide

SoHide is a dependency-free AArch64 Android library for process-local CTF experiments. A loaded guest module calls the library from inside its own process; SoHide does not inject code, target packages, use `ptrace`, or require root.

The default profile performs three independently reported operations:

1. It replaces the target image's file-backed `PT_LOAD` mappings with private anonymous mappings at the same virtual addresses.
2. It removes the target image from Bionic's global `soinfo` list after discovering the live linker layout.
3. It verifies that the image path is absent from `/proc/self/maps` and its base is absent from `dl_iterate_phdr`.

The aggressive profile additionally clears the in-memory ELF magic. That option is intentionally explicit because it can break later diagnostics and unwind paths.

## Scope

- AArch64 only
- Android 10/API 29 through Android 17/API 37
- Fully native C/C++20
- No Java, JNI, root, Magisk, `ptrace`, or remote-process access
- Intended for owned CTF applications and controlled guest-process experiments

SoHide is not complete invisibility. Anonymous executable mappings remain observable. Memory scanners can still recognize code and data. JNI registrations, open file descriptors, namespace references, and other application state may remain observable. The linker-list operation uses Bionic internals and fails closed when it cannot prove the expected layout.

## Public API

```c
#include <SoHide/SoHide.h>

SoHideResult Result = {0};
SoHideStatus Status = SoHideHideAddress((const void*)&YourModuleEntryPoint, SoHideOptionDefault, &Result);
```

`SoHideHideAddress` hides the loaded image containing the supplied address. This is the preferred API for guest modules because it remains correct whether SoHide is linked statically or shared.

`SoHideHideSelf` hides the image containing the SoHide implementation. When SoHide is statically linked into the guest payload, that is the payload. When `libSoHide.so` is used separately, it is only `libSoHide.so`.

Every call returns a `SoHideStatus`. `SoHideResult.CompletedOptions` records exactly which stages completed before an error.

The installed public surface is only `include/SoHide/SoHide.h`. Linker discovery, ELF parsing, map parsing, remapping, and patching remain private implementation details under `src`.

## Required lifecycle

Call SoHide once during a controlled, single-threaded startup phase, after the guest module is fully relocated and before worker threads start. Load the target with `RTLD_NODELETE` and never call `dlclose` after unlinking it. Do not expect later `dlsym` discovery of an unlinked image.

The aggressive header option should be used only when the challenge does not need native stack unwinding through the hidden module.

## Build

```powershell
$env:ANDROID_NDK_HOME = "C:\Android\ndk\27.0.12077973"
cmake --preset android-arm64-release
cmake --build --preset android-arm64-release
```

The minimum platform is API 29. The resulting binary uses stable Linux memory syscalls and performs runtime discovery for the active Bionic linker.

For an existing CMake project:

```cmake
add_subdirectory(path/to/SoHide)
target_link_libraries(YourPayload PRIVATE SoHide::SoHide)
```

## Native proof

The proof consists of a native runner and a shared payload. It counts the payload's named `/proc/self/maps` entries and `dl_iterate_phdr` entries before and after hiding.

```powershell
adb push build/android-arm64-release/SoHideProofRunner /data/local/tmp/
adb push build/android-arm64-release/libSoHideProofPayload.so /data/local/tmp/
adb shell chmod 755 /data/local/tmp/SoHideProofRunner
adb shell /data/local/tmp/SoHideProofRunner /data/local/tmp/libSoHideProofPayload.so
adb shell /data/local/tmp/SoHideProofRunner /data/local/tmp/libSoHideProofPayload.so --aggressive
```

A successful run ends with output shaped like this:

```text
status=0 requested=0xb completed=0xb base=0x... mappings=3 bytes=28672
maps=3->0 linker=1->0 path=/data/local/tmp/libSoHideProofPayload.so
```

## Compatibility evidence

The implementation recognizes both historical `solist`/`sonext` and current `solist_head`/`solist_tail` Bionic symbols, including LLVM-renamed local symbols. It derives the `soinfo::next` offset from the live list rather than hard-coding an Android release offset, and validates the target entry by its load bias and program-header address.

The proof has been executed without root on a physical Pixel 8a running Android 17/API 37, using both default and aggressive profiles. Builds are checked with NDK r24, r27, and r30. Runtime behavior on each OEM build must still be tested because vendors can ship a stripped or modified linker; unsupported layouts return an error instead of being patched speculatively.

Relevant upstream implementation references are [Bionic linker iteration](https://android.googlesource.com/platform/bionic/+/android-17.0.0_r1/linker/linker.cpp), [Bionic linker list ownership](https://android.googlesource.com/platform/bionic/+/android-17.0.0_r1/linker/linker_main.cpp), and [Bionic `soinfo` layout](https://android.googlesource.com/platform/bionic/+/android-17.0.0_r1/linker/linker_soinfo.h).

## License

MIT
