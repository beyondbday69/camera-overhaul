#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "mod/CameraShake.h"
#include "mod/Config.h"

#include <pl/Input.hpp>
#include <pl/Mod.hpp>
#include <pl/memory/Hook.hpp>

namespace camera_overhaul {

/**
 * @brief One camera shake offset consumed by Minecraft's CameraAPI.
 *
 * Layout verified against Bedrock 1.26.51: three floats followed by a one-byte
 * shake type and three padding bytes. Type 0 is rotational (the game adds the
 * vector to the camera rotation), type 1 is positional.
 */
struct ShakeEntry {
    float x{};
    float y{};
    float z{};
    std::uint8_t type{};
    std::uint8_t padding[3]{};
};

static_assert(sizeof(ShakeEntry) == 16, "CameraAPI shake entries are 16 bytes");
static_assert(offsetof(ShakeEntry, type) == 12,
              "CameraAPI shake type lives at offset 12");

/**
 * @brief C++ lifecycle mod for LeviLaunchroid.
 *
 * The lifecycle order is load() -> enable() -> disable() -> unload(). The mod
 * registers a Mod Menu module and installs two hooks on the game's CameraAPI:
 *
 *  - slot 7 (getFov) is overridden with the configured field of view.
 *  - slot 6 (getShakeOffsets) gets a dynamic roll tilt appended.
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
    using GetFovFn = std::optional<float> (*)(void *self);
    using GetShakeOffsetsFn = std::vector<ShakeEntry> (*)(
        void *self, void *context, float a, float b, float c);

    ll::mod::NativeMod &mSelf;
    mutable std::mutex mConfigMutex;
    std::optional<pl::config::ConfigFile<ModConfig>> mConfigFile;
    bool mModuleRegistered{false};

    pl::memory::HookHandle mFovHook;
    pl::memory::HookHandle mShakeHook;
    GetFovFn mOriginalGetFov{};
    GetShakeOffsetsFn mOriginalGetShakeOffsets{};

    CameraShake mShake;
    std::chrono::steady_clock::time_point mShakeEpoch{};

    std::atomic<bool> mFeatureEnabled{true};
    std::atomic<int> mFov{70};
    std::atomic<bool> mFovHookSeen{false};
    std::atomic<bool> mShakeHookSeen{false};

    [[nodiscard]] ModConfig snapshotConfig() const;
    void applyShakeSettings(const ModConfig &config);
    [[nodiscard]] double shakeTimeSeconds() const;

    void installCameraHooks();
    void removeCameraHooks();

    void unregisterModule();

    double mLastSampleTime{0.0};
    std::mutex mTouchMutex;
    struct TouchPoint {
        float startX = 0.0f;
        float startY = 0.0f;
        float prevX = 0.0f;
        float prevY = 0.0f;
        bool isMovement = false;
    };
    std::unordered_map<int, TouchPoint> mTouchPoints;
    float mMaxSeenX{0.0f};

    bool handleTouch(const pl::input::TouchEvent &ev);
    bool handleKey(const pl::input::KeyEvent &ev);

    static std::optional<float> getFovDetour(void *self);
    static std::vector<ShakeEntry> getShakeOffsetsDetour(void *self,
                                                         void *context, float a,
                                                         float b, float c);

    static void onToggle(std::string_view moduleId, bool enabled);
    void handleToggle(std::string_view moduleId, bool enabled);

    static void onConfigChanged(std::string_view moduleId, std::string_view key,
                                std::string_view value);
    void handleConfigChanged(std::string_view moduleId, std::string_view key,
                             std::string_view value);
};

} // namespace camera_overhaul
