#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace camera_overhaul {

/**
 * @brief User-tunable parameters of the procedural camera shake.
 */
struct ShakeSettings {
    bool enabled = true;
    /** Base angular amplitude in degrees. */
    float intensity = 0.35f;
    /** Frequency multiplier; 1.0 is a natural handheld cadence. */
    float speed = 1.0f;
    /** 0 = smooth drift, 1 = gritty high-frequency jitter. */
    float roughness = 0.55f;
    /** Extra low-frequency sway while standing still, 0..1. */
    float idle = 0.40f;
};

/**
 * @brief Rotation offset in degrees added on top of the camera rotation.
 */
struct ShakeOffset {
    float yaw = 0.0f;
    float pitch = 0.0f;
};

/**
 * @brief Deterministic, frame-rate independent handheld camera shake.
 *
 * The engine is a sum of smooth value-noise octaves (slow drift, medium sway,
 * high-frequency jitter) plus decaying impulse responses. The signal is a pure
 * function of time, so a variable frame rate cannot change its character.
 *
 * Settings and impulses may be written from any thread; `sample()` is meant to
 * run on the camera thread.
 */
class CameraShake {
  public:
    CameraShake() = default;

    void configure(const ShakeSettings &settings);
    [[nodiscard]] ShakeSettings settings() const;

    /** Queues an impulse (landing, hit, ...) with the given strength in degrees. */
    void impulse(float strength);

    /**
     * @brief Samples the offset for the given monotonically increasing time.
     * @param timeSeconds Seconds since an arbitrary fixed epoch.
     */
    [[nodiscard]] ShakeOffset sample(double timeSeconds);

    void reset();

  private:
    struct Impulse {
        double start = 0.0;
        float strength = 0.0f;
    };

    static constexpr std::size_t kMaxImpulses = 8;

    std::atomic<bool> mEnabled{true};
    std::atomic<float> mIntensity{0.35f};
    std::atomic<float> mSpeed{1.0f};
    std::atomic<float> mRoughness{0.55f};
    std::atomic<float> mIdle{0.40f};

    std::atomic<float> mPendingImpulse{0.0f};

    std::array<Impulse, kMaxImpulses> mImpulses{};
    std::size_t mNextImpulse = 0;
};

} // namespace camera_overhaul
