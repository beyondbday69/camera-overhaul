#pragma once

#include <mutex>
#include <optional>

#include "mod/Config.h"

#include <pl/Mod.hpp>

namespace camera_overhaul {

/**
 * @brief C++ lifecycle mod for LeviLaunchroid.
 *
 * The lifecycle order is load() -> enable() -> disable() -> unload(). The mod
 * currently ships UI only: it registers a single Mod Menu module and stores
 * the field-of-view slider value in the typed config file. No camera hook is
 * installed yet.
 */
class CameraOverhaulMod {
  public:
    static CameraOverhaulMod &instance();

    CameraOverhaulMod();

    [[nodiscard]] ll::mod::NativeMod &getSelf() const { return mSelf; }

    bool load();
    bool enable();
    bool disable();
    bool unload();

  private:
    ll::mod::NativeMod &mSelf;
    mutable std::mutex mConfigMutex;
    std::optional<pl::config::ConfigFile<ModConfig>> mConfigFile;
    bool mModuleRegistered{false};

    [[nodiscard]] ModConfig snapshotConfig() const;
    void unregisterModule();
    bool persistConfig(int fov);

    static void onConfigChanged(std::string_view moduleId, std::string_view key,
                                std::string_view value);
    void handleConfigChanged(std::string_view moduleId, std::string_view key,
                             std::string_view value);
};

} // namespace camera_overhaul
