#include "mod/CameraOverhaulMod.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>

#include <fmt/format.h>
#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>
#include <pl/memory/Vtable.hpp>

namespace camera_overhaul {
namespace {

/** Identifier of the single Mod Menu module registered by this mod. */
constexpr const char *kModuleId = "camera_overhaul.main";

/** Keys of the Mod Menu entries. */
constexpr const char *kFovKey = "fov";
constexpr const char *kShakeEnabledKey = "shake_enabled";
constexpr const char *kShakeIntensityKey = "shake_intensity";
constexpr const char *kShakeSpeedKey = "shake_speed";
constexpr const char *kShakeRoughnessKey = "shake_roughness";
constexpr const char *kShakeIdleKey = "shake_idle";

/** RTTI type name and vtable slots used by the game's CameraAPI. */
constexpr std::string_view kCameraTypeInfo = "9CameraAPI";
constexpr std::string_view kMinecraftLibrary = "libminecraftpe.so";
constexpr std::size_t kFovSlot = 7;
constexpr std::size_t kShakeSlot = 6;

constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;

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

float parseFloat(std::string_view value, float fallback) {
    const std::string text(value);
    char *end = nullptr;
    errno = 0;
    const float parsed = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE ||
        !std::isfinite(parsed)) {
        return fallback;
    }
    return parsed;
}

bool parseBool(std::string_view value, bool fallback) {
    if (value == "true" || value == "1" || value == "on" || value == "yes") {
        return true;
    }
    if (value == "false" || value == "0" || value == "off" || value == "no") {
        return false;
    }
    return fallback;
}

int clampFov(int value) { return std::clamp(value, kMinFov, kMaxFov); }

float clampShakeIntensity(float value) {
    return std::clamp(value, kMinShakeIntensity, kMaxShakeIntensity);
}

float clampShakeSpeed(float value) {
    return std::clamp(value, kMinShakeSpeed, kMaxShakeSpeed);
}

float clampShakeRoughness(float value) {
    return std::clamp(value, kMinShakeRoughness, kMaxShakeRoughness);
}

float clampShakeIdle(float value) {
    return std::clamp(value, kMinShakeIdle, kMaxShakeIdle);
}

std::string formatFloat(float value) { return fmt::format("{:.2f}", value); }

void normalizeConfig(ModConfig &config) {
    config.fov = clampFov(config.fov);
    config.shake_intensity = clampShakeIntensity(config.shake_intensity);
    config.shake_speed = clampShakeSpeed(config.shake_speed);
    config.shake_roughness = clampShakeRoughness(config.shake_roughness);
    config.shake_idle = clampShakeIdle(config.shake_idle);
}

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
            normalizeConfig(value);
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
        mFeatureEnabled.store(config.enabled, std::memory_order_relaxed);
        mFov.store(config.fov, std::memory_order_relaxed);
        applyShakeSettings(config);
        mShake.reset();
        mShakeEpoch = std::chrono::steady_clock::now();

        const bool registered =
            pl::modmenu::ModuleBuilder(kModuleId, "Camera Overhaul")
                .modId(self.getId())
                .description(
                    "Field-of-view override and procedural handheld camera shake.")
                .defaultEnabled(config.enabled)
                .config(kFovKey, "Field of View", pl::modmenu::ConfigType::SliderInt,
                        std::to_string(config.fov), std::to_string(kMinFov),
                        std::to_string(kMaxFov))
                .config(kShakeEnabledKey, "Camera Shake",
                        pl::modmenu::ConfigType::Toggle,
                        config.shake_enabled ? "true" : "false")
                .config(kShakeIntensityKey, "Shake Intensity",
                        pl::modmenu::ConfigType::SliderFloat,
                        formatFloat(config.shake_intensity),
                        formatFloat(kMinShakeIntensity),
                        formatFloat(kMaxShakeIntensity), kShakeEnabledKey)
                .config(kShakeSpeedKey, "Shake Speed",
                        pl::modmenu::ConfigType::SliderFloat,
                        formatFloat(config.shake_speed),
                        formatFloat(kMinShakeSpeed),
                        formatFloat(kMaxShakeSpeed), kShakeEnabledKey)
                .config(kShakeRoughnessKey, "Shake Roughness",
                        pl::modmenu::ConfigType::SliderFloat,
                        formatFloat(config.shake_roughness),
                        formatFloat(kMinShakeRoughness),
                        formatFloat(kMaxShakeRoughness), kShakeEnabledKey)
                .config(kShakeIdleKey, "Idle Sway",
                        pl::modmenu::ConfigType::SliderFloat,
                        formatFloat(config.shake_idle),
                        formatFloat(kMinShakeIdle),
                        formatFloat(kMaxShakeIdle), kShakeEnabledKey)
                .onToggle(onToggle)
                .onConfigChanged(onConfigChanged)
                .registerModule();

        if (!registered) {
            self.getLogger().error("Failed to register Mod Menu module {}", kModuleId);
            return false;
        }

        mModuleRegistered = true;
        self.getLogger().info("Registered Mod Menu module {}", kModuleId);

        installCameraHooks();
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
        removeCameraHooks();
        mShake.reset();
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
        removeCameraHooks();
        mShake.reset();
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
    normalizeConfig(snapshot);
    return snapshot;
}

void CameraOverhaulMod::applyShakeSettings(const ModConfig &config) {
    ShakeSettings settings;
    settings.enabled = config.shake_enabled;
    settings.intensity = config.shake_intensity;
    settings.speed = config.shake_speed;
    settings.roughness = config.shake_roughness;
    settings.idle = config.shake_idle;
    mShake.configure(settings);
}

double CameraOverhaulMod::shakeTimeSeconds() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                         mShakeEpoch)
        .count();
}

void CameraOverhaulMod::installCameraHooks() {
    auto &self = getSelf();
    removeCameraHooks();

    mFovHookSeen.store(false, std::memory_order_relaxed);
    mShakeHookSeen.store(false, std::memory_order_relaxed);

    const auto fovTarget = pl::memory::resolveVtableFunction(
        kCameraTypeInfo, kFovSlot, kMinecraftLibrary);
    if (fovTarget == 0) {
        self.getLogger().error(
            "camera hook not found: CameraAPI::getFov (slot {})", kFovSlot);
    } else {
        void *original = nullptr;
        pl::memory::HookHandle handle(
            reinterpret_cast<void *>(fovTarget),
            reinterpret_cast<void *>(&CameraOverhaulMod::getFovDetour),
            &original);
        if (!handle.installed()) {
            self.getLogger().error("Failed to hook CameraAPI::getFov at 0x{:x}",
                                   fovTarget);
        } else {
            mOriginalGetFov = reinterpret_cast<GetFovFn>(original);
            mFovHook = std::move(handle);
            self.getLogger().info("Hooked CameraAPI::getFov at 0x{:x}",
                                  fovTarget);
        }
    }

    const auto shakeTarget = pl::memory::resolveVtableFunction(
        kCameraTypeInfo, kShakeSlot, kMinecraftLibrary);
    if (shakeTarget == 0) {
        self.getLogger().error(
            "camera hook not found: CameraAPI::getShakeOffsets (slot {})",
            kShakeSlot);
    } else {
        void *original = nullptr;
        pl::memory::HookHandle handle(
            reinterpret_cast<void *>(shakeTarget),
            reinterpret_cast<void *>(&CameraOverhaulMod::getShakeOffsetsDetour),
            &original);
        if (!handle.installed()) {
            self.getLogger().error(
                "Failed to hook CameraAPI::getShakeOffsets at 0x{:x}",
                shakeTarget);
        } else {
            mOriginalGetShakeOffsets =
                reinterpret_cast<GetShakeOffsetsFn>(original);
            mShakeHook = std::move(handle);
            self.getLogger().info(
                "Hooked CameraAPI::getShakeOffsets at 0x{:x}", shakeTarget);
        }
    }
}

void CameraOverhaulMod::removeCameraHooks() {
    mFovHook.reset();
    mShakeHook.reset();
    mOriginalGetFov = nullptr;
    mOriginalGetShakeOffsets = nullptr;
}

std::optional<float> CameraOverhaulMod::getFovDetour(void *self) {
    auto &mod = instance();
    if (mod.mFeatureEnabled.load(std::memory_order_relaxed)) {
        if (!mod.mFovHookSeen.exchange(true, std::memory_order_relaxed)) {
            mod.getSelf().getLogger().info("Camera FOV hook is live");
        }
        const int fov = mod.mFov.load(std::memory_order_relaxed);
        return static_cast<float>(fov) * kDegToRad;
    }

    if (mod.mOriginalGetFov) {
        return mod.mOriginalGetFov(self);
    }
    return std::nullopt;
}

std::vector<ShakeEntry>
CameraOverhaulMod::getShakeOffsetsDetour(void *self, void *context, float a,
                                         float b, float c) {
    auto &mod = instance();
    std::vector<ShakeEntry> offsets;
    try {
        if (mod.mOriginalGetShakeOffsets) {
            offsets = mod.mOriginalGetShakeOffsets(self, context, a, b, c);
        }

        if (!mod.mFeatureEnabled.load(std::memory_order_relaxed)) {
            return offsets;
        }

        if (!mod.mShakeHookSeen.exchange(true, std::memory_order_relaxed)) {
            mod.getSelf().getLogger().info("Camera shake hook is live");
        }

        const ShakeOffset offset = mod.mShake.sample(mod.shakeTimeSeconds());
        if (offset.pitch != 0.0f || offset.yaw != 0.0f) {
            ShakeEntry entry;
            // The game stores camera rotation as (pitch, yaw, roll) and adds
            // type-0 entries to it directly, so the values are in degrees.
            entry.x = offset.pitch;
            entry.y = offset.yaw;
            entry.z = 0.0f;
            entry.type = 0;
            offsets.push_back(entry);
        }
    } catch (...) {
        // A failed allocation must never unwind into the game's render loop.
    }
    return offsets;
}

void CameraOverhaulMod::unregisterModule() {
    if (!mModuleRegistered) {
        return;
    }
    mModuleRegistered = false;
    pl::modmenu::unregisterModule(kModuleId);
}

void CameraOverhaulMod::onToggle(std::string_view moduleId, bool enabled) {
    instance().handleToggle(moduleId, enabled);
}

void CameraOverhaulMod::handleToggle(std::string_view moduleId, bool enabled) {
    if (!matchesModule(moduleId)) {
        return;
    }

    mFeatureEnabled.store(enabled, std::memory_order_relaxed);

    auto &self = getSelf();
    bool persisted = false;
    {
        std::lock_guard lock(mConfigMutex);
        if (!mConfigFile) {
            return;
        }
        mConfigFile->value().enabled = enabled;
        persisted = mConfigFile->save();
    }

    if (persisted) {
        self.getLogger().info("Camera Overhaul {}", enabled ? "enabled" : "disabled");
    } else {
        self.getLogger().warn("Failed to persist module toggle");
    }
}

void CameraOverhaulMod::onConfigChanged(std::string_view moduleId,
                                        std::string_view key,
                                        std::string_view value) {
    instance().handleConfigChanged(moduleId, key, value);
}

void CameraOverhaulMod::handleConfigChanged(std::string_view moduleId,
                                            std::string_view key,
                                            std::string_view value) {
    if (!matchesModule(moduleId)) {
        return;
    }

    auto &self = getSelf();
    ModConfig updated;
    bool changed = false;
    bool persisted = false;

    {
        std::lock_guard lock(mConfigMutex);
        if (!mConfigFile) {
            return;
        }

        auto &config = mConfigFile->value();
        if (key == kFovKey) {
            config.fov = clampFov(parseInt(value, config.fov));
            mFov.store(config.fov, std::memory_order_relaxed);
            changed = true;
        } else if (key == kShakeEnabledKey) {
            config.shake_enabled = parseBool(value, config.shake_enabled);
            changed = true;
        } else if (key == kShakeIntensityKey) {
            config.shake_intensity =
                clampShakeIntensity(parseFloat(value, config.shake_intensity));
            changed = true;
        } else if (key == kShakeSpeedKey) {
            config.shake_speed =
                clampShakeSpeed(parseFloat(value, config.shake_speed));
            changed = true;
        } else if (key == kShakeRoughnessKey) {
            config.shake_roughness = clampShakeRoughness(
                parseFloat(value, config.shake_roughness));
            changed = true;
        } else if (key == kShakeIdleKey) {
            config.shake_idle =
                clampShakeIdle(parseFloat(value, config.shake_idle));
            changed = true;
        }

        if (!changed) {
            return;
        }

        updated = config;
        persisted = mConfigFile->save();
    }

    applyShakeSettings(updated);

    if (persisted) {
        self.getLogger().info("Persisted config after {}", key);
    } else {
        self.getLogger().warn("Failed to persist config after {}", key);
    }
}

} // namespace camera_overhaul
