#include "mod/CameraShake.h"

#include <algorithm>
#include <cmath>

namespace camera_overhaul {
namespace {

constexpr float kMaxRollDegrees = 8.0f;

} // namespace

void CameraShake::configure(const TiltSettings &settings) {
    mTiltEnabled.store(settings.tiltEnabled, std::memory_order_relaxed);
    mTiltIntensity.store(std::clamp(settings.tiltIntensity, 0.0f, 3.0f),
                         std::memory_order_relaxed);
    mTiltSmoothness.store(std::clamp(settings.tiltSmoothness, 2.0f, 25.0f),
                          std::memory_order_relaxed);
}

TiltSettings CameraShake::settings() const {
    TiltSettings result;
    result.tiltEnabled = mTiltEnabled.load(std::memory_order_relaxed);
    result.tiltIntensity = mTiltIntensity.load(std::memory_order_relaxed);
    result.tiltSmoothness = mTiltSmoothness.load(std::memory_order_relaxed);
    return result;
}

void CameraShake::addTurnDelta(float dx) {
    mTurnAccumulator.fetch_add(dx, std::memory_order_relaxed);
}

void CameraShake::setTouchStrafe(float strafe) {
    mTouchStrafe.store(std::clamp(strafe, -1.0f, 1.0f), std::memory_order_relaxed);
}

void CameraShake::setKeyStrafe(int direction) {
    mKeyStrafeDir.store(std::clamp(direction, -1, 1), std::memory_order_relaxed);
}

void CameraShake::reset() {
    mTurnAccumulator.store(0.0f, std::memory_order_relaxed);
    mTouchStrafe.store(0.0f, std::memory_order_relaxed);
    mKeyStrafeDir.store(0, std::memory_order_relaxed);
    mFilteredTurnVelocity = 0.0f;
    mCurrentRoll = 0.0f;
}

ShakeOffset CameraShake::sample(double /*timeSeconds*/, float dt) {
    ShakeOffset result{};

    if (!mTiltEnabled.load(std::memory_order_relaxed)) {
        mCurrentRoll = 0.0f;
        mFilteredTurnVelocity = 0.0f;
        return result;
    }

    const float intensity = mTiltIntensity.load(std::memory_order_relaxed);
    const float smoothness = mTiltSmoothness.load(std::memory_order_relaxed);
    const float frameDt = std::clamp(dt, 0.001f, 0.1f);

    // 1. Turning tilt (look swipe)
    // Low-pass filter the turn velocity to eliminate touch stutter / frame drops
    const float turnDelta = mTurnAccumulator.exchange(0.0f, std::memory_order_relaxed);
    const float instantaneousVelocity = turnDelta / frameDt; // pixels/sec

    // 12.0 rad/s filter cuts out touch event timing jitter while remaining responsive
    const float filterFactor = 1.0f - std::exp(-12.0f * frameDt);
    mFilteredTurnVelocity += (instantaneousVelocity - mFilteredTurnVelocity) * filterFactor;

    // Turning right (dragging finger left, turnDelta < 0) leans camera right (+roll)
    // Turning left (dragging finger right, turnDelta > 0) leans camera left (-roll)
    const float turnRoll = std::clamp(-mFilteredTurnVelocity * 0.0020f, -3.5f, 3.5f);

    // 2. Strafing tilt (continuous joystick or A/D keys)
    const float touchStrafe = mTouchStrafe.load(std::memory_order_relaxed);
    const int keyStrafe = mKeyStrafeDir.load(std::memory_order_relaxed);

    float strafeInput = touchStrafe;
    if (keyStrafe != 0) {
        strafeInput = static_cast<float>(keyStrafe);
    }

    // Moving right leans camera right (+roll), moving left leans left (-roll)
    const float strafeRoll = strafeInput * 3.0f;

    // 3. Combined target roll
    const float targetRoll = std::clamp((turnRoll + strafeRoll) * intensity,
                                        -kMaxRollDegrees, kMaxRollDegrees);

    // 4. Exponential spring interpolation towards target roll
    const float springFactor = 1.0f - std::exp(-smoothness * frameDt);
    mCurrentRoll += (targetRoll - mCurrentRoll) * springFactor;

    if (std::abs(mCurrentRoll) < 0.005f && std::abs(targetRoll) < 0.005f) {
        mCurrentRoll = 0.0f;
    }

    result.roll = mCurrentRoll;
    return result;
}

} // namespace camera_overhaul
