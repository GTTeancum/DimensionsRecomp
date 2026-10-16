# Native Toy Pad

Native mode is enabled by default (native_roster = true in the Input
configuration). It uses the emulated portal and disables companion services.
Set native_roster = false and restart to return to the legacy companion or
physical portal configuration.

Place your own existing, writable 180-byte .bin tags in native-roster/tags
beside the executable, or set native_roster_tags to another directory.
Relative paths resolve beside the executable. Restart after changing the
catalog. Tag state is persisted; keep your own backups. Optional matching PNGs
sit beside their tags and must match the pinned portrait bindings and hashes.
Missing or rejected artwork falls back to names. This source publication
includes no game data, tag payloads, or portrait images.

## Controls and behavior

- Open with F6 or the LB+RB controller chord.
- Navigate the seven positions with the left stick or D-pad. A selects, lifts,
  or drops; B cancels or goes back. The dialog also supports mouse interaction.
- X removes the focused owned token. Y, followed by confirmation, clears owned
  tokens. Clear does not delete tag files or remove unmanaged portal entries.
- In the picker, Y opens filters, X toggles a favorite, and LB/RB changes pages.
  Franchise, year, type, ability and favorites filters are manual: there is no
  automatic puzzle detection.
- The display reflects portal light state. A lifted token remains held until
  dropped or restored. Save failures retain the token and report an error.
  Favorites are stored locally.

The UI is preliminary. Initial user feedback supports this direction, with
visual refinement still planned; it is not evidence of comprehensive gameplay
compatibility.

## Build dependency

Use the main repository's [build instructions](../README-dev.md), including
your own game data and generated sources, CMake/Ninja, Clang, and the Windows
SDK/MSVC headers and libraries. The native code requires the matching SDK
changes; an unmodified SDK is insufficient.

The SDK submodule remains at the tested upstream base
ee34f28f9e886c3bff2ea8f9ea83f48aff497e58. Apply the included
[source patch](native-toypad-sdk.patch) before building the SDK. No SDK fork is
required. The patch contains only the twelve native Toy Pad SDK source/test
changes; it excludes the five later unrelated upstream commits and local
build accommodations.

From a fresh clone of this repository, with the SDK submodule initialized at
the pinned base and no local SDK edits, run from the repository root:

```
git -C rexglue-sdk rev-parse HEAD
git -C rexglue-sdk apply --check ../docs/native-toypad-sdk.patch
git -C rexglue-sdk apply ../docs/native-toypad-sdk.patch
```

The first command must report ee34f28f9e886c3bff2ea8f9ea83f48aff497e58.
Stop if the base differs or the patch check fails; do not reset an edited
checkout. Apply this patch once. The patch intentionally leaves the SDK
working tree modified while preserving the upstream submodule pointer.

Build and install the patched SDK in Release before building the game; point
CMAKE_PREFIX_PATH at that installation when configuring consumers. The source
patch was verified to apply to an isolated index at the exact base and to
reconstruct the reviewed SDK source tree. No new SDK build was performed for
publication.

The tested environment used a local option to suppress CMake user-package
registry registration and materialized libmspack symlinks for its Windows
checkout. Those accommodations are not part of these commits. A fresh build
must provide a checkout with usable dependency symlinks and account for the
SDK install's existing package-registry behavior. The tested build used a
private SDK prefix; this is not a fresh-clone reproducibility claim.

## Test evidence and reproduction requirements

Twenty native-roster executables passed on Windows with C++23, clang-cl,
Release, and assertions explicitly enabled (-UNDEBUG after -DNDEBUG).
The catalog test was rerun after adding diagnostics: its optional symlink
fixture skipped on Windows error 1314; the remaining assertions passed.
The test permits that optional fixture to skip for errors 1314 or 5.

The tests are committed as source. The aggregate CMake harness used for this
run contains machine-specific paths and is intentionally not published.
There is currently no portable one-command runner. Reconstruct targets with
the following source/link requirements, using the matching SDK include path:

| Tests (native_roster_NAME_test.cpp) | Additional sources / definitions / libraries |
|---|---|
| SDK: bumper_keys, input_latch, nonblocking | SDK include directory |
| Game: browser, catalog, favorites_generation, favorites_writer, lights, model, portrait_cache, portrait_file | Game src and SDK include directories |
| Game: clear, controller, large_controller, lift, refresh | native_roster.cpp, NATIVE_ROSTER_CONTROLLER_ONLY |
| Game: real_backend, full_backend | native_roster.cpp, native_roster_backend.cpp, NATIVE_ROSTER_CONTROLLER_ONLY, rex::runtime |
| Game: portrait_trust | native_roster_sha256.cpp |
| Game: full_portrait | native_roster_sha256.cpp, rex::runtime |

Use find_package(rexglue CONFIG REQUIRED) for linked SDK targets. On Windows,
make the matching SDK runtime DLL available when running linked executables.
Every target must keep assertions active.

Run browser, catalog, favorites_writer, clear, controller, large_controller,
and lift with one argument naming a fresh disposable fixture directory.
Run real_backend with a private starter-input directory containing
batman.bin, gandalf.bin, wyldstyle.bin, batmobile.bin, then a fresh
destination directory. Run full_backend with a private directory of 315
existing 180-byte tags, then a fresh destination directory; it copies tags
before exercising writes. Run full_portrait with the private directory of
315 matching resource_<tag-resource-id>.png files. Other tests take no
arguments. Do not use live saves or runtime tag directories as fixture outputs.
The asset-dependent tests cannot be reproduced from this source tree alone.

Coverage includes input capture/release, catalog and controller behavior,
held pickup, clear, persistence/failure retention, protocol behavior, filters
and favorites, and exact-hash portrait decoding. The full backend test
exercised 315 existing identities sequentially and seven simultaneous slots;
the portrait test decoded 315 pinned images and rejected altered data.
Private input hashes remained unchanged.

A separate 120-second startup followed by an ordinary window close exited 0.
The log discovered 315 catalog entries, with emulation enabled and no companion
listener. Save files, tags, configuration, and protected installed files
remained unchanged. This was a startup/close smoke test, not an interactive
gameplay or puzzle-completion test. No new build or execution was performed
while preparing these publication commits.

See [third-party notices](native-toypad-third-party-notices.md) for metadata
and implementation attribution.
