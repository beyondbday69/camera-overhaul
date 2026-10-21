# Camera Overhaul

Native **LeviLaunchroid** (Minecraft Bedrock / Android) mod skeleton, built on the
[Preloader SDK](https://github.com/LiteLDev/preloader-android).

This is the **skeleton only**: the mod ships an empty lifecycle, a typed config
file, and a single Mod Menu module with a field-of-view slider. **No camera
behaviour and no game hook is installed yet** – the slider is UI only.

| Field | Value |
| --- | --- |
| Mod id | `camera_overhaul` |
| Display name | Camera Overhaul |
| Author | Swapnil |
| Version | 0.1.0 |
| Entry library | `libcamera_overhaul.so` |
| ABI | `arm64-v8a` only |
| C++ standard | C++20 |
| Minecraft versions | `26.51` |

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
        ├── CameraOverhaulMod.cpp  Lifecycle + Mod Menu module
        ├── CameraOverhaulMod.h
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
| `pl::modmenu::ConfigType::SliderInt` | `include/pl/ModMenu.hpp` |
| `pl::modmenu::unregisterModule(std::string_view)` | `include/pl/ModMenu.hpp` |
| `pl::config::ConfigFile<T>` | `include/pl/Config.hpp` |
| `pl::config::Schema<T>`, `pl::config::FieldSchema` | `include/pl/Config.hpp` |
| `pl::config::defaultJson(T)`, `pl::config::schema(T)` | `include/pl/Config.hpp` |

## Lifecycle

`PL_REGISTER_MOD` is used exactly once, in `src/main.cpp`. The mod object keeps
the loader-provided handle in `mSelf` (`*ll::mod::NativeMod::current()`).

| Phase | Behaviour |
| --- | --- |
| `load()` | Creates the config directory, loads/merges `config/config.json` through `pl::config::ConfigFile<ModConfig>`, clamps the FOV value, logs `Loaded`. Returns `false` on failure. |
| `enable()` | Registers exactly one Mod Menu module, `camera_overhaul.main`, with a single **Field of View** slider (30–140). UI only – no hook. |
| `disable()` | Unregisters the Mod Menu module and logs `Disabled`. |
| `unload()` | Unregisters the module and releases the config file. |

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
   **Field of View** slider. Moving the slider rewrites
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

- **Built and verified locally**: the host config generator and the Android
  `arm64-v8a` library (`libcamera_overhaul.so`), including the `.levipack`
  packaging step.
- **Verified on GitHub Actions**: the `Build` workflow is green on `main` and on
  the `v0.1.0` tag. Each run builds and uploads the `camera_overhaul` artifact.
- **Release**: tag `v0.1.0` published
  [`camera_overhaul.levipack`](https://github.com/beyondbday69/camera-overhaul/releases/tag/v0.1.0)
  as a release asset.
- **Not verified**: the mod has never been imported into a running LeviLaunchroid
  on a device. The Mod Menu module is registered but deliberately does not
  change any camera behaviour yet.
