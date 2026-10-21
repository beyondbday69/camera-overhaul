# Camera Overhaul

Native **LeviLaunchroid** (Minecraft Bedrock / Android) mod that adds dynamic
camera lean/tilt when turning and strafing, native GPU accumulation motion blur,
and FOV override, built on the
[Preloader SDK](https://github.com/LiteLDev/preloader-android).

The mod hooks the game's own `CameraAPI` vtable through RTTI and intercepts
the Android EGL swapchain (`eglSwapBuffers`) for pure hardware-accelerated
motion blur. All hooks are resolved at runtime and removed cleanly on disable/unload.

| Field | Value |
| --- | --- |
| Mod id | `camera_overhaul` |
| Display name | Camera Overhaul |
| Author | Swapnil |
| Version | 0.4.1 |
| Entry library | `libcamera_overhaul.so` |
| ABI | `arm64-v8a` only |
| C++ standard | C++20 |
| Minecraft versions | `1.26.51.1`, `1.26.*` |

## File tree

```text
camera_overhaul/
├── .github/
│   └── workflows/
│       └── build.yml          GitHub Actions: build, package, upload, release
├── .clang-format
├── .gitattributes
├── .gitignore
├── CMakeLists.txt             Fetches the Preloader SDK, builds libcamera_overhaul.so
├── README.md
├── config/
│   ├── config.json            Packaged typed config defaults
│   └── config.schema.json     Packaged JSON schema for config.json
├── manifest.json              LeviLaunchroid mod manifest
├── scripts/
│   └── package.sh             Builds and produces the .levipack
└── src/
    ├── config_generator.cpp   Host tool that writes config/config.json + schema
    ├── main.cpp               PL_REGISTER_MOD entry point
    └── mod/
        ├── CameraOverhaulMod.cpp  Lifecycle, Mod Menu module, CameraAPI hooks
        ├── CameraOverhaulMod.h
        ├── CameraShake.cpp    Procedural handheld shake engine
        ├── CameraShake.h
        ├── Config.cpp          Typed config helpers
        └── Config.h            ModConfig struct + JSON schema metadata
```

## SDK APIs used

Every symbol below exists in the pinned SDK release
[`LiteLDev/preloader-android@0.2.2`](https://github.com/LiteLDev/preloader-android/tree/0.2.2).

| API | Header |
| --- | --- |
| `PL_REGISTER_MOD(TYPE, INSTANCE_EXPR)` | `include/pl/Mod.hpp` |
| `ll::mod::NativeMod` (alias of `pl::mod::NativeMod`) | `include/pl/Mod.hpp` |
| `ll::mod::NativeMod::current()` | `include/pl/Mod.hpp` |
| `NativeMod::getId()`, `getName()`, `getModDir()` | `include/pl/Mod.hpp` |
| `NativeMod::getDataDir()`, `getConfigDir()` | `include/pl/Mod.hpp` |
| `NativeMod::getLogger()` | `include/pl/Mod.hpp` |
| `pl::log::Logger::info/warn/error/debug(fmt, args...)` | `include/pl/Logger.hpp` |
| `pl::modmenu::ModuleBuilder` | `include/pl/ModMenu.hpp` |
| `pl::modmenu::ConfigType::SliderInt`, `SliderFloat`, `Toggle` | `include/pl/ModMenu.hpp` |
| `pl::modmenu::unregisterModule(std::string_view)` | `include/pl/ModMenu.hpp` |
| `pl::memory::resolveVtableFunction(typeInfo, slot, module)` | `include/pl/memory/Vtable.hpp` |
| `pl::memory::HookHandle`, `pl::memory::hook` | `include/pl/memory/Hook.hpp` |
| `pl::config::ConfigFile<T>` | `include/pl/Config.hpp` |
| `pl::config::Schema<T>`, `pl::config::FieldSchema` | `include/pl/Config.hpp` |
| `pl::config::defaultJson(T)`, `pl::config::schema(T)` | `include/pl/Config.hpp` |

## Lifecycle

`PL_REGISTER_MOD` is used exactly once, in `src/main.cpp`. The mod object keeps
the loader-provided handle in `mSelf` (`*ll::mod::NativeMod::current()`).

| Phase | Behaviour |
| --- | --- |
| `load()` | Creates the config directory, loads/merges `config/config.json` through `pl::config::ConfigFile<ModConfig>`, clamps and persists the normalized values, logs `Loaded`. Returns `false` on failure. |
| `enable()` | Registers the `camera_overhaul.main` Mod Menu module (FOV slider plus shake toggle/sliders) and installs the two `CameraAPI` hooks. Missing targets are logged; the mod never crashes the game because of them. |
| `disable()` | Removes both hooks, resets the shake engine, unregisters the Mod Menu module and logs `Disabled`. |
| `unload()` | Removes hooks, unregisters the module and releases the config file. |

Every lifecycle entry point catches exceptions and returns `false` on failure, so
no exception ever crosses the lifecycle boundary.

## Configuration

`config/config.json` and `config/config.schema.json` are produced from the
`ModConfig` struct in `src/mod/Config.h` by `src/config_generator.cpp`, so the
packaged defaults can never drift away from the code.

```json
{
    "enabled": true,
    "fov": 70,
    "shake_enabled": true,
    "shake_idle": 0.4,
    "shake_intensity": 0.6,
    "shake_roughness": 0.55,
    "shake_speed": 1.0,
    "version": 1
}
```

## Run the GitHub Action

1. Push the repository to GitHub (or fork it).
2. Open the **Actions** tab and select the **Build** workflow.
3. It runs automatically on every push, on every pull request, and on
   `workflow_dispatch` (Run workflow).
4. It also runs for tags matching `v*`; for those runs the `.levipack` is
   attached to a GitHub Release automatically.

The workflow uses the Android SDK that ships with the `ubuntu-latest` runner,
installs the Android NDK (`r28c`, via `nttld/setup-ndk`), CMake and Ninja, then
runs `scripts/package.sh`.

## Download the artifact

1. Open the **Actions** tab and click the successful **Build** run.
2. Scroll to **Artifacts** at the bottom of the run summary.
3. Download `camera_overhaul`. The zip contains:
   - `camera_overhaul.levipack` – the importable package
   - `camera_overhaul/` – the same package unpacked
     (`manifest.json`, `libcamera_overhaul.so`, `config/config.json`,
     `config/config.schema.json`)

For a `v*` tag, the same file is attached to the GitHub Release page.

## Import into LeviLaunchroid

1. Copy `camera_overhaul.levipack` to the device (or download it on the device).
2. In LeviLaunchroid, open the mod section and use the import action
   (*Import mod*), then pick `camera_overhaul.levipack`.
3. Confirm the import dialog. LeviLaunchroid extracts the package into the
   mod folder of the selected Minecraft version.
4. Start the game. The Mod Menu shows the **Camera Overhaul** module with the
   **Field of View** slider and the **Camera Shake** toggle plus its intensity,
   speed, roughness and idle-sway sliders. Changes are written back to
   `config/config.json` inside the installed mod folder.

Alternatively, copy the unpacked `camera_overhaul/` folder into the native mod
folder of the selected Minecraft version.

## Build locally

Requirements: Android SDK, Android NDK `r28c` (28.2.13676358), CMake 3.22+,
Ninja, Bash.

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk/28.2.13676358
./scripts/package.sh
```

Output:

```text
dist/arm64-v8a/camera_overhaul/
dist/arm64-v8a/camera_overhaul.levipack
```

The script mirrors `examples/full-cpp-mod/build.ps1` from the LeviLaunchroid
repository: it builds the host config generator, builds the Android library,
assembles the `camera_overhaul/` folder and zips its contents into the
`.levipack`.

`ANDROID_STL=c++_shared` is used because LeviLaunchroid itself builds its
native code with the shared STL, and the mod is `dlopen`ed into the launcher
process.

## Status

- **Build**: GitHub Actions compiles the mod and produces the `.levipack`
  artifact on every push. Local builds are no longer used.
- **Verified on GitHub Actions**: the `Build` workflow is green on `main`
  (run
  [39635095879](https://github.com/beyondbday69/camera-overhaul/actions/runs/39635095879))
  and on the `v0.2.0` tag (run
  [39635758086](https://github.com/beyondbday69/camera-overhaul/actions/runs/39635758086)).
- **Release**: tag
  [`v0.2.0`](https://github.com/beyondbday69/camera-overhaul/releases/tag/v0.2.0)
  publishes
  [`camera_overhaul.levipack`](https://github.com/beyondbday69/camera-overhaul/releases/download/v0.2.0/camera_overhaul.levipack).
- **Implemented**: `CameraAPI` slot 7 (`getFov`) is overridden with the
  configured FOV in radians; slot 6 (`getShakeOffsets`) gets a procedural
  rotational offset appended every frame. Both slots are resolved through
  `_ZTS9CameraAPI` RTTI, not hard-coded addresses.
- **Not verified yet**: the mod has not been imported into a running
  LeviLaunchroid on a device. In-game testing is the next step; the game log
  prints `Camera FOV hook is live` and `Camera shake hook is live` once each
  hook fires.
