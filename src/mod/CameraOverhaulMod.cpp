#include "mod/CameraOverhaulMod.h"
#include "mod/MotionBlur.h"

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
constexpr const char *kTiltEnabledKey = "tilt_enabled";
constexpr const char *kTiltIntensityKey = "tilt_intensity";
constexpr const char *kTiltSmoothnessKey = "tilt_smoothness";
constexpr const char *kMotionBlurEnabledKey = "motion_blur_enabled";
constexpr const char *kMotionBlurIntensityKey = "motion_blur_intensity";

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

std::string formatFloat(float value) { return fmt::format("{:.2f}", value); }

void normalizeConfig(ModConfig &config) {
    config.fov = clampFov(config.fov);
    config.tilt_intensity =
        std::clamp(config.tilt_intensity, kMinTiltIntensity, kMaxTiltIntensity);
    config.tilt_smoothness =
        std::clamp(config.tilt_smoothness, kMinTiltSmoothness, kMaxTiltSmoothness);
    config.motion_blur_intensity =
        std::clamp(config.motion_blur_intensity, kMinMotionBlurIntensity, kMaxMotionBlurIntensity);
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

        try {
            MotionBlur::instance().setEnabled(config.motion_blur_enabled);
            MotionBlur::instance().setIntensity(config.motion_blur_intensity);
            if (MotionBlur::instance().installHook()) {
                self.getLogger().info("Installed native Motion Blur hook on eglSwapBuffers");
            } else {
                self.getLogger().warn("Failed to install native Motion Blur hook");
            }
        } catch (const std::exception &ex) {
            self.getLogger().warn("Exception setting up Motion Blur: {}", ex.what());
        } catch (...) {
            self.getLogger().warn("Unknown exception setting up Motion Blur");
        }

        try {
            pl::input::registerTouchCallback([](const pl::input::TouchEvent &ev) {
                return instance().handleTouch(ev);
            });
            pl::input::registerKeyCallback([](const pl::input::KeyEvent &ev) {
                return instance().handleKey(ev);
            });
            self.getLogger().info("Registered input callbacks for dynamic lean/strafe");
        } catch (...) {
            self.getLogger().warn("Failed to register input callbacks");
        }

        try {
            const bool registered =
                pl::modmenu::ModuleBuilder(kModuleId, "Camera Overhaul")
                    .modId(self.getId())
                    .description(
                        "Dynamic camera lean and tilt mod for Minecraft Bedrock.")
                    .defaultEnabled(config.enabled)
                    .config(kFovKey, "Field of View", pl::modmenu::ConfigType::SliderInt,
                            std::to_string(config.fov), std::to_string(kMinFov),
                            std::to_string(kMaxFov))
                    .config(kTiltEnabledKey, "Camera Lean / Tilt",
                            pl::modmenu::ConfigType::Toggle,
                            config.tilt_enabled ? "true" : "false")
                    .config(kTiltIntensityKey, "Tilt Intensity",
                            pl::modmenu::ConfigType::SliderFloat,
                            formatFloat(config.tilt_intensity),
                            formatFloat(kMinTiltIntensity),
                            formatFloat(kMaxTiltIntensity), kTiltEnabledKey)
                    .config(kTiltSmoothnessKey, "Tilt Smoothness",
                            pl::modmenu::ConfigType::SliderFloat,
                            formatFloat(config.tilt_smoothness),
                            formatFloat(kMinTiltSmoothness),
                            formatFloat(kMaxTiltSmoothness), kTiltEnabledKey)
                    .config(kMotionBlurEnabledKey, "Motion Blur",
                            pl::modmenu::ConfigType::Toggle,
                            config.motion_blur_enabled ? "true" : "false")
                    .config(kMotionBlurIntensityKey, "Blur Intensity",
                            pl::modmenu::ConfigType::SliderFloat,
                            formatFloat(config.motion_blur_intensity),
                            formatFloat(kMinMotionBlurIntensity),
                            formatFloat(kMaxMotionBlurIntensity), kMotionBlurEnabledKey)
                    .onToggle(onToggle)
                    .onConfigChanged(onConfigChanged)
                    .registerModule();

            if (registered) {
                mModuleRegistered = true;
                self.getLogger().info("Registered Mod Menu module {}", kModuleId);
            } else {
                self.getLogger().warn("Failed to register Mod Menu module {}", kModuleId);
            }
        } catch (const std::exception &ex) {
            self.getLogger().warn("Exception registering Mod Menu module: {}", ex.what());
        } catch (...) {
            self.getLogger().warn("Unknown exception registering Mod Menu module");
        }

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
        MotionBlur::instance().removeHook();
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
        MotionBlur::instance().removeHook();
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
    TiltSettings settings;
    settings.tiltEnabled = config.tilt_enabled;
    settings.tiltIntensity = config.tilt_intensity;
    settings.tiltSmoothness = config.tilt_smoothness;
    mShake.configure(settings);
}

double CameraOverhaulMod::shakeTimeSeconds() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                         mShakeEpoch)
        .count();
}

bool CameraOverhaulMod::handleTouch(const pl::input::TouchEvent &ev) {
    if (!mFeatureEnabled.load(std::memory_order_relaxed)) {
        return false;
    }
    std::lock_guard lock(mTouchMutex);

    mMaxSeenX = std::max(mMaxSeenX, ev.x);
    const float splitX = (mMaxSeenX > 200.0f) ? (mMaxSeenX * 0.45f) : 600.0f;

    if (ev.action == 0 || ev.action == 5) { // ACTION_DOWN / ACTION_POINTER_DOWN
        const bool isMovement = (ev.x < splitX);
        mTouchPoints[ev.pointerId] = TouchPoint{
            .startX = ev.x,
            .startY = ev.y,
            .prevX = ev.x,
            .prevY = ev.y,
            .isMovement = isMovement,
        };
    } else if (ev.action == 2) { // ACTION_MOVE
        auto it = mTouchPoints.find(ev.pointerId);
        if (it != mTouchPoints.end()) {
            if (it->second.isMovement) {
                // Continuous virtual joystick displacement from touch origin
                const float displacementX = ev.x - it->second.startX;
                const float strafe = std::clamp(displacementX / 75.0f, -1.0f, 1.0f);
                mShake.setTouchStrafe(strafe);
            } else {
                // Aim / look turn delta
                const float dx = ev.x - it->second.prevX;
                mShake.addTurnDelta(dx);
            }
            it->second.prevX = ev.x;
            it->second.prevY = ev.y;
        } else {
            const bool isMovement = (ev.x < splitX);
            mTouchPoints[ev.pointerId] = TouchPoint{
                .startX = ev.x,
                .startY = ev.y,
                .prevX = ev.x,
                .prevY = ev.y,
                .isMovement = isMovement,
            };
        }
    } else if (ev.action == 1 || ev.action == 3 || ev.action == 6) { // ACTION_UP / ACTION_CANCEL / ACTION_POINTER_UP
        auto it = mTouchPoints.find(ev.pointerId);
        if (it != mTouchPoints.end()) {
            if (it->second.isMovement) {
                mShake.setTouchStrafe(0.0f);
            }
            mTouchPoints.erase(it);
        }
    }
    return false;
}

bool CameraOverhaulMod::handleKey(const pl::input::KeyEvent &ev) {
    if (!mFeatureEnabled.load(std::memory_order_relaxed)) {
        return false;
    }
    if (ev.keyCode == 29) { // Key A
        mShake.setKeyStrafe(ev.isKeyDown ? -1 : 0);
    } else if (ev.keyCode == 32) { // Key D
        mShake.setKeyStrafe(ev.isKeyDown ? 1 : 0);
    }
    return false;
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
            mod.getSelf().getLogger().info("Camera tilt hook is live");
        }

        const double now = mod.shakeTimeSeconds();
        const float dt = (mod.mLastSampleTime > 0.0)
                             ? static_cast<float>(now - mod.mLastSampleTime)
                             : 0.016f;
        mod.mLastSampleTime = now;

        // Dynamic Lean / Roll tilt
        const ShakeOffset rotOffset = mod.mShake.sample(now, dt);
        if (std::abs(rotOffset.roll) > 0.005f) {
            ShakeEntry rotEntry;
            rotEntry.x = 0.0f;
            rotEntry.y = 0.0f;
            rotEntry.z = rotOffset.roll; // DYNAMIC ROLL TILT (degrees)
            rotEntry.type = 0;          // Rotational
            offsets.push_back(rotEntry);
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
        } else if (key == kTiltEnabledKey) {
            config.tilt_enabled = parseBool(value, config.tilt_enabled);
            changed = true;
        } else if (key == kTiltIntensityKey) {
            config.tilt_intensity = std::clamp(
                parseFloat(value, config.tilt_intensity), kMinTiltIntensity, kMaxTiltIntensity);
            changed = true;
        } else if (key == kTiltSmoothnessKey) {
            config.tilt_smoothness = std::clamp(
                parseFloat(value, config.tilt_smoothness), kMinTiltSmoothness, kMaxTiltSmoothness);
            changed = true;
        } else if (key == kMotionBlurEnabledKey) {
            config.motion_blur_enabled = parseBool(value, config.motion_blur_enabled);
            MotionBlur::instance().setEnabled(config.motion_blur_enabled);
            changed = true;
        } else if (key == kMotionBlurIntensityKey) {
            config.motion_blur_intensity = std::clamp(
                parseFloat(value, config.motion_blur_intensity),
                kMinMotionBlurIntensity, kMaxMotionBlurIntensity);
            MotionBlur::instance().setIntensity(config.motion_blur_intensity);
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
