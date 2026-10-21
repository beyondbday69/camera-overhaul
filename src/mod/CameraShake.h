#pragma once

#include <atomic>
#include <cstdint>

namespace camera_overhaul {

/**
 * @brief Configuration parameters matching Mirsario's Minecraft-CameraOverhaul.
 * https://github.com/Mirsario/Minecraft-CameraOverhaul
 */
struct MirsarioConfig {
    bool enabled = true;

    // Turning Roll parameters
    double turningRollAccumulation = 1.0;
    double turningRollIntensity = 1.25;
    double turningRollSmoothing = 1.0;

    // Contextual velocity factors
    double strafingRollFactor = 10.0;
    double forwardVelocityPitchFactor = 7.0;
    double verticalVelocityPitchFactor = 2.5;
    double horizontalVelocitySmoothingFactor = 1.0;
    double verticalVelocitySmoothingFactor = 1.0;

    // Idle camera sway
    bool cameraSwayEnabled = true;
    double cameraSwayIntensity = 0.60;
    double cameraSwayFrequency = 0.16;
    double cameraSwayFadeInDelay = 0.15;
    double cameraSwayFadeInLength = 5.0;
    double cameraSwayFadeOutLength = 0.75;
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
 * @brief Exact C++ engine of Mirsario's CameraOverhaul CameraSystem.
 */
class CameraShake {
  public:
    CameraShake() = default;

    void configure(const MirsarioConfig &cfg);
    [[nodiscard]] MirsarioConfig config() const;

    /** Ingests look turn delta in screen pixels. */
    void addTurnDelta(float dx, float dy);

    /** Ingests continuous movement input: strafe (-1 to 1) and forward (-1 to 1). */
    void setMovementInput(float strafe, float forward);

    /** Ingests keyboard movement: keys A/D and W/S. */
    void setKeyInput(int strafe, int forward);

    /** Ingests vertical velocity (jumping / falling). */
    void setVerticalVelocity(float vy);

    /** Notifies player action to fade out camera sway. */
    void notifyOfPlayerAction(double timeSeconds);

    /**
     * @brief Samples rotation offset (pitch, yaw, roll) for the current frame.
     * @param timeSeconds Monotonic time in seconds.
     * @param dt Elapsed time since previous sample in seconds.
     */
    [[nodiscard]] ShakeOffset sample(double timeSeconds, float dt);

    void reset();

  private:
    MirsarioConfig mCfg;

    std::atomic<float> mTurnDeltaX{0.0f};
    std::atomic<float> mTurnDeltaY{0.0f};
    std::atomic<float> mTouchStrafe{0.0f};
    std::atomic<float> mTouchForward{0.0f};
    std::atomic<int> mKeyStrafe{0};
    std::atomic<int> mKeyForward{0};
    std::atomic<float> mVerticalVelocity{0.0f};

    // Mirsario internal state
    double mTurningRollTargetOffset{0.0};
    double mPrevStrafingRollOffset{0.0};
    double mPrevForwardPitchOffset{0.0};
    double mPrevVerticalPitchOffset{0.0};

    // Mirsario sway state
    double mLastActionTime{0.0};
    double mCameraSwayFactor{0.0};
    double mCameraSwayFactorTarget{0.0};
};

} // namespace camera_overhaul
