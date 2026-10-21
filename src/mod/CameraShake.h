#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>

namespace camera_overhaul {

struct ModConfig;

/**
 * @brief User-tunable parameters of the procedural camera shake and tilt.
 */
struct ShakeSettings {
    bool enabled = false;
    float intensity = 0.35f;
    float speed = 1.0f;
    float roughness = 0.55f;
    float idle = 0.40f;

    bool tiltEnabled = true;
    float tiltIntensity = 1.0f;
    float tiltSmoothness = 12.0f;

    bool shoulderEnabled = true;
    float shoulderSide = 0.75f;
    float shoulderHeight = 0.20f;
    float shoulderBack = -0.15f;
};

/**
 * @brief Rotation offset in degrees (pitch, yaw, roll) added on top of the camera.
 */
struct ShakeOffset {
    float pitch = 0.0f;
    float yaw = 0.0f;
    float roll = 0.0f;
};

/**
 * @brief Position offset in blocks added to third-person camera for shoulder view.
 */
struct PositionOffset {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

/**
 * @brief Camera motion engine:
 *  - Dynamic lean / roll tilt when turning and strafing
 *  - Over-the-shoulder 3rd person camera translation
 *  - Optional procedural handheld noise shake
 */
class CameraShake {
  public:
    CameraShake() = default;

    void configure(const ShakeSettings &settings);
    [[nodiscard]] ShakeSettings settings() const;

    /** Ingests touch movement: dx/dy in screen points; isLook=true for aiming side. */
    void addTouchMotion(float dx, float dy, bool isLook);

    /** Ingests keyboard strafe: -1 = left, +1 = right, 0 = none. */
    void setKeyStrafe(int direction);

    /** Toggles shoulder between right (+offset) and left (-offset). */
    void toggleShoulderSide();

    /** Queues an impulse (landing, hit, ...) with the given strength in degrees. */
    void impulse(float strength);

    /**
     * @brief Samples rotation offset (pitch, yaw, roll) for the current frame.
     * @param timeSeconds Monotonic time in seconds.
     * @param dt Elapsed time since previous sample in seconds.
     */
    [[nodiscard]] ShakeOffset sample(double timeSeconds, float dt);

    /**
     * @brief Samples position offset for shoulder camera.
     */
    [[nodiscard]] PositionOffset samplePosition() const;

    void reset();

  private:
    struct Impulse {
        double start = 0.0;
        float strength = 0.0f;
    };

    static constexpr std::size_t kMaxImpulses = 8;

    // Noise shake settings
    std::atomic<bool> mNoiseEnabled{false};
    std::atomic<float> mIntensity{0.35f};
    std::atomic<float> mSpeed{1.0f};
    std::atomic<float> mRoughness{0.55f};
    std::atomic<float> mIdle{0.40f};

    // Tilt settings
    std::atomic<bool> mTiltEnabled{true};
    std::atomic<float> mTiltIntensity{1.0f};
    std::atomic<float> mTiltSmoothness{12.0f};

    // Shoulder cam settings
    std::atomic<bool> mShoulderEnabled{true};
    std::atomic<float> mShoulderSide{0.75f};
    std::atomic<float> mShoulderHeight{0.20f};
    std::atomic<float> mShoulderBack{-0.15f};

    // Dynamic tilt state
    std::atomic<float> mAccumulatedTurnVelocity{0.0f};
    std::atomic<float> mAccumulatedStrafeVelocity{0.0f};
    std::atomic<int> mKeyStrafeDir{0};
    float mCurrentRoll{0.0f};
    float mTargetRoll{0.0f};

    std::atomic<float> mPendingImpulse{0.0f};
    std::array<Impulse, kMaxImpulses> mImpulses{};
    std::size_t mNextImpulse = 0;
};

} // namespace camera_overhaul
