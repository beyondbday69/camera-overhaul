#pragma once

#include <atomic>
#include <cstddef>

namespace camera_overhaul {

struct ModConfig;

/**
 * @brief User-tunable parameters for dynamic camera lean/tilt.
 */
struct TiltSettings {
    bool tiltEnabled = true;
    float tiltIntensity = 1.0f;
    float tiltSmoothness = 8.0f;
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
 * @brief Dynamic camera motion engine:
 *  - Buttery smooth lean/roll tilt when turning (looking left/right)
 *  - Continuous lean/roll tilt when strafing (moving left/right via joystick or keys A/D)
 *  - Exponential spring centering when stationary
 */
class CameraShake {
  public:
    CameraShake() = default;

    void configure(const TiltSettings &settings);
    [[nodiscard]] TiltSettings settings() const;

    /** Ingests look turn delta in screen pixels. */
    void addTurnDelta(float dx);

    /** Ingests continuous touch strafe (-1.0 to 1.0). */
    void setTouchStrafe(float strafe);

    /** Ingests keyboard strafe: -1 = left (Key A), +1 = right (Key D), 0 = none. */
    void setKeyStrafe(int direction);

    /**
     * @brief Samples rotation offset (pitch, yaw, roll) for the current frame.
     * @param timeSeconds Monotonic time in seconds.
     * @param dt Elapsed time since previous sample in seconds.
     */
    [[nodiscard]] ShakeOffset sample(double timeSeconds, float dt);

    void reset();

  private:
    std::atomic<bool> mTiltEnabled{true};
    std::atomic<float> mTiltIntensity{1.0f};
    std::atomic<float> mTiltSmoothness{8.0f};

    // Continuous strafe inputs
    std::atomic<float> mTouchStrafe{0.0f};
    std::atomic<int> mKeyStrafeDir{0};

    // Look/turn accumulator (screen delta from touch)
    std::atomic<float> mTurnAccumulator{0.0f};

    // Low-pass filtered turn velocity to eliminate touch stutter/jitter
    float mFilteredTurnVelocity{0.0f};

    // Current smoothed camera roll angle (in degrees)
    float mCurrentRoll{0.0f};
};

} // namespace camera_overhaul
