#include "mod/CameraOverhaulMod.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>

#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>

namespace camera_overhaul {
namespace {

/** Identifier of the single Mod Menu module registered by this mod. */
constexpr const char *kModuleId = "camera_overhaul.main";

/** Key of the field-of-view slider inside the Mod Menu module. */
constexpr const char *kFovKey = "fov";

bool matchesModule(std::string_view moduleId) { return moduleId == kModuleId; }

int parseInt(std::string_view value, int fallback) {
    const std::string text(value);
    char *end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE) {
        return fallback;
    }
    if (parsed < std::numeric_limits<int>::min() ||
        parsed > std::numeric_limits<int>::max()) {
        return fallback;
    }
    return static_cast<int>(parsed);
}

int clampFov(int value) { return std::clamp(value, kMinFov, kMaxFov); }

} // namespace

CameraOverhaulMod &CameraOverhaulMod::instance() {
    static CameraOverhaulMod instance;
    return instance;
}

CameraOverhaulMod::CameraOverhaulMod() : mSelf(*ll::mod::NativeMod::current()) {}

bool CameraOverhaulMod::load() {
    auto &self = getSelf();
    try {
        self.getLogger().debug("Loading...");

        std::error_code ec;
        std::filesystem::create_directories(self.getConfigDir(), ec);
        if (ec) {
            self.getLogger().error("Failed to create config directory {}: {}",
                                   self.getConfigDir().string(), ec.message());
            return false;
        }

        {
            std::lock_guard lock(mConfigMutex);
            mConfigFile.emplace();
            if (!mConfigFile->load()) {
                self.getLogger().error("Failed to load typed config");
                mConfigFile.reset();
                return false;
            }

            auto &value = mConfigFile->value();
            value.fov = clampFov(value.fov);
            if (!mConfigFile->save()) {
                self.getLogger().error("Failed to persist normalized config");
                mConfigFile.reset();
                return false;
            }
        }

        self.getLogger().info("Loaded");
        return true;
    } catch (const std::exception &ex) {
        self.getLogger().error("load() failed: {}", ex.what());
    } catch (...) {
        self.getLogger().error("load() failed with an unknown error");
    }
    return false;
}

bool CameraOverhaulMod::enable() {
    auto &self = getSelf();
    try {
        self.getLogger().debug("Enabling...");

        const ModConfig config = snapshotConfig();

        const bool registered =
            pl::modmenu::ModuleBuilder(kModuleId, "Camera Overhaul")
                .modId(self.getId())
                .description(
                    "Camera Overhaul settings. UI only for now, no camera hook "
                    "is installed yet.")
                .defaultEnabled(config.enabled)
                .config(kFovKey, "Field of View", pl::modmenu::ConfigType::SliderInt,
                        std::to_string(config.fov), std::to_string(kMinFov),
                        std::to_string(kMaxFov))
                .onConfigChanged(onConfigChanged)
                .registerModule();

        if (!registered) {
            self.getLogger().error("Failed to register Mod Menu module {}", kModuleId);
            return false;
        }

        mModuleRegistered = true;
        self.getLogger().info("Registered Mod Menu module {}", kModuleId);
        return true;
    } catch (const std::exception &ex) {
        self.getLogger().error("enable() failed: {}", ex.what());
    } catch (...) {
        self.getLogger().error("enable() failed with an unknown error");
    }
    return false;
}

bool CameraOverhaulMod::disable() {
    auto &self = getSelf();
    try {
        self.getLogger().debug("Disabling...");
        unregisterModule();
        self.getLogger().info("Disabled");
        return true;
    } catch (const std::exception &ex) {
        self.getLogger().error("disable() failed: {}", ex.what());
    } catch (...) {
        self.getLogger().error("disable() failed with an unknown error");
    }
    return false;
}

bool CameraOverhaulMod::unload() {
    auto &self = getSelf();
    try {
        self.getLogger().debug("Unloading...");
        unregisterModule();
        {
            std::lock_guard lock(mConfigMutex);
            mConfigFile.reset();
        }
        self.getLogger().info("Unloaded");
        return true;
    } catch (const std::exception &ex) {
        self.getLogger().error("unload() failed: {}", ex.what());
    } catch (...) {
        self.getLogger().error("unload() failed with an unknown error");
    }
    return false;
}

ModConfig CameraOverhaulMod::snapshotConfig() const {
    std::lock_guard lock(mConfigMutex);
    if (!mConfigFile) {
        return ModConfig{};
    }
    auto snapshot = mConfigFile->value();
    snapshot.fov = clampFov(snapshot.fov);
    return snapshot;
}

void CameraOverhaulMod::unregisterModule() {
    if (!mModuleRegistered) {
        return;
    }
    mModuleRegistered = false;
    pl::modmenu::unregisterModule(kModuleId);
}

bool CameraOverhaulMod::persistConfig(int fov) {
    std::lock_guard lock(mConfigMutex);
    if (!mConfigFile) {
        return false;
    }
    mConfigFile->value().fov = clampFov(fov);
    return mConfigFile->save();
}

void CameraOverhaulMod::onConfigChanged(std::string_view moduleId,
                                        std::string_view key,
                                        std::string_view value) {
    instance().handleConfigChanged(moduleId, key, value);
}

void CameraOverhaulMod::handleConfigChanged(std::string_view moduleId,
                                             std::string_view key,
                                             std::string_view value) {
    if (!matchesModule(moduleId) || key != kFovKey) {
        return;
    }

    auto &self = getSelf();
    const int fov = clampFov(parseInt(value, kMinFov));

    if (persistConfig(fov)) {
        self.getLogger().info("Persisted config after {}", key);
    } else {
        self.getLogger().warn("Failed to persist config after {}", key);
    }
}

} // namespace camera_overhaul
