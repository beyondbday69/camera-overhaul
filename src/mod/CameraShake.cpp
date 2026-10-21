#include "mod/CameraShake.h"

#include <algorithm>
#include <cmath>

namespace camera_overhaul {
namespace {

namespace math_utils {

inline double clamp(double value, double min, double max) {
    return value < min ? min : (value > max ? max : value);
}

inline double clamp01(double value) {
    return value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
}

inline double lerp(double a, double b, double time) {
    return a + (b - a) * clamp01(time);
}

inline double dampStep(double smoothing, double dt) {
    return 1.0 - std::pow(smoothing * smoothing, dt);
}

inline double damp(double source, double destination, double smoothing, double dt) {
    return lerp(source, destination, dampStep(smoothing, dt));
}

inline double stepTowards(double current, double target, double step) {
    if (current < target) {
        return std::min(current + step, target);
    } else if (current > target) {
        return std::max(current - step, target);
    }
    return current;
}

// Mirsario's easeInOutCubic: https://easings.net/#easeInOutCubic
inline double turningEasing(double x) {
    return x < 0.5 ? (4.0 * x * x * x) : (1.0 - std::pow(-2.0 * x + 2.0, 3.0) / 2.0);
}

} // namespace math_utils

// Mirsario Base Constants
constexpr double kBaseTurningRollAccumulation = 0.0048;
constexpr double kBaseTurningRollIntensity = 1.25;
constexpr double kBaseTurningRollSmoothing = 0.0825;

constexpr double kBaseStrafingRollSmoothing = 0.008;
constexpr double kBaseForwardPitchSmoothing = 0.008;

constexpr double kBaseVerticalPitchSmoothing = 0.00004;
constexpr double kVerticalPitchThreshold = 0.4;
constexpr double kCameraSwayFadingSmoothness = 3.0;

} // namespace

void CameraShake::configure(const MirsarioConfig &cfg) {
    mCfg = cfg;
}

MirsarioConfig CameraShake::config() const {
    return mCfg;
}

void CameraShake::addTurnDelta(float dx, float dy) {
    mTurnDeltaX.fetch_add(dx, std::memory_order_relaxed);
    mTurnDeltaY.fetch_add(dy, std::memory_order_relaxed);
}

void CameraShake::setMovementInput(float strafe, float forward) {
    mTouchStrafe.store(std::clamp(strafe, -1.0f, 1.0f), std::memory_order_relaxed);
    mTouchForward.store(std::clamp(forward, -1.0f, 1.0f), std::memory_order_relaxed);
}

void CameraShake::setKeyInput(int strafe, int forward) {
    mKeyStrafe.store(std::clamp(strafe, -1, 1), std::memory_order_relaxed);
    mKeyForward.store(std::clamp(forward, -1, 1), std::memory_order_relaxed);
}

void CameraShake::setVerticalVelocity(float vy) {
    mVerticalVelocity.store(vy, std::memory_order_relaxed);
}

void CameraShake::notifyOfPlayerAction(double timeSeconds) {
    mLastActionTime = timeSeconds;
}

void CameraShake::reset() {
    mTurnDeltaX.store(0.0f, std::memory_order_relaxed);
    mTurnDeltaY.store(0.0f, std::memory_order_relaxed);
    mTouchStrafe.store(0.0f, std::memory_order_relaxed);
    mTouchForward.store(0.0f, std::memory_order_relaxed);
    mKeyStrafe.store(0, std::memory_order_relaxed);
    mKeyForward.store(0, std::memory_order_relaxed);
    mVerticalVelocity.store(0.0f, std::memory_order_relaxed);

    mTurningRollTargetOffset = 0.0;
    mPrevStrafingRollOffset = 0.0;
    mPrevForwardPitchOffset = 0.0;
    mPrevVerticalPitchOffset = 0.0;
    mLastActionTime = 0.0;
    mCameraSwayFactor = 0.0;
    mCameraSwayFactorTarget = 0.0;
}

ShakeOffset CameraShake::sample(double timeSeconds, float dt) {
    ShakeOffset output{};

    if (!mCfg.enabled) {
        return output;
    }

    const double deltaTime = math_utils::clamp(static_cast<double>(dt), 0.001, 0.1);

    // Fetch and reset input deltas
    const float turnDx = mTurnDeltaX.exchange(0.0f, std::memory_order_relaxed);
    const float turnDy = mTurnDeltaY.exchange(0.0f, std::memory_order_relaxed);

    const float touchStrafe = mTouchStrafe.load(std::memory_order_relaxed);
    const float touchForward = mTouchForward.load(std::memory_order_relaxed);
    const int keyStrafe = mKeyStrafe.load(std::memory_order_relaxed);
    const int keyForward = mKeyForward.load(std::memory_order_relaxed);
    const float vy = mVerticalVelocity.load(std::memory_order_relaxed);

    const float strafeVal = (keyStrafe != 0) ? static_cast<float>(keyStrafe) : touchStrafe;
    const float forwardVal = (keyForward != 0) ? static_cast<float>(keyForward) : touchForward;

    if (std::abs(turnDx) > 0.001f || std::abs(turnDy) > 0.001f ||
        std::abs(strafeVal) > 0.01f || std::abs(forwardVal) > 0.01f) {
        notifyOfPlayerAction(timeSeconds);
    }

    // -------------------------------------------------------------------------
    // 1. Mirsario Turning Roll Offset (Z Axis)
    // -------------------------------------------------------------------------
    {
        const double decaySmoothing = kBaseTurningRollSmoothing * mCfg.turningRollSmoothing;
        const double intensity = kBaseTurningRollIntensity * mCfg.turningRollIntensity;
        const double accumulation = kBaseTurningRollAccumulation * mCfg.turningRollAccumulation;

        // In touch/mouse look, dragging left turns player right (positive yaw delta)
        const double yawDelta = -static_cast<double>(turnDx) * 0.10;

        // Decay
        mTurningRollTargetOffset = math_utils::damp(mTurningRollTargetOffset, 0.0, decaySmoothing, deltaTime);

        // Accumulation
        mTurningRollTargetOffset = math_utils::clamp(
            mTurningRollTargetOffset + (yawDelta * accumulation), -1.0, 1.0);

        // Apply easing curve
        const double sign = (mTurningRollTargetOffset >= 0.0) ? 1.0 : -1.0;
        const double turningRoll = math_utils::clamp01(
            math_utils::turningEasing(std::abs(mTurningRollTargetOffset))) * intensity * sign;

        output.roll += static_cast<float>(turningRoll);
    }

    // -------------------------------------------------------------------------
    // 2. Mirsario Strafing Roll Offset (Z Axis)
    // -------------------------------------------------------------------------
    {
        const double multiplier = mCfg.strafingRollFactor;
        const double smoothing = kBaseStrafingRollSmoothing * mCfg.horizontalVelocitySmoothingFactor;

        // Relative velocity X: moving right leans camera right (+roll), left leans left (-roll)
        const double target = static_cast<double>(strafeVal) * (multiplier * 0.35);
        const double currentOffset = math_utils::damp(mPrevStrafingRollOffset, target, smoothing, deltaTime);

        output.roll += static_cast<float>(currentOffset);
        mPrevStrafingRollOffset = currentOffset;
    }

    // -------------------------------------------------------------------------
    // 3. Mirsario Forward Velocity Pitch Offset (X Axis)
    // -------------------------------------------------------------------------
    {
        const double multiplier = mCfg.forwardVelocityPitchFactor;
        const double smoothing = kBaseForwardPitchSmoothing * mCfg.horizontalVelocitySmoothingFactor;

        const double target = static_cast<double>(forwardVal) * (multiplier * 0.20);
        const double currentOffset = math_utils::damp(mPrevForwardPitchOffset, target, smoothing, deltaTime);

        output.pitch += static_cast<float>(currentOffset);
        mPrevForwardPitchOffset = currentOffset;
    }

    // -------------------------------------------------------------------------
    // 4. Mirsario Vertical Velocity Pitch Offset (X Axis)
    // -------------------------------------------------------------------------
    {
        const double multiplier = mCfg.verticalVelocityPitchFactor;
        const double smoothing = kBaseVerticalPitchSmoothing * mCfg.verticalVelocitySmoothingFactor;

        double target = static_cast<double>(vy) * multiplier;
        if (std::abs(target) < kVerticalPitchThreshold) {
            target = 0.0;
        }
        const double currentOffset = math_utils::damp(mPrevVerticalPitchOffset, target, smoothing, deltaTime);

        output.pitch += static_cast<float>(currentOffset);
        mPrevVerticalPitchOffset = currentOffset;
    }

    // -------------------------------------------------------------------------
    // 5. Mirsario Idle Sway / Noise Offset
    // -------------------------------------------------------------------------
    if (mCfg.cameraSwayEnabled) {
        const float noiseTime = static_cast<float>(timeSeconds * mCfg.cameraSwayFrequency);

        // Fade out if player recently moved or looked
        if ((timeSeconds - mLastActionTime) < mCfg.cameraSwayFadeInDelay) {
            mCameraSwayFactorTarget = 0.0; // Fade-out
        } else if (mCameraSwayFactor == mCameraSwayFactorTarget) {
            mCameraSwayFactorTarget = 1.0; // Fade-in
        }

        const double fadeLength = (mCameraSwayFactorTarget > 0.0)
                                      ? mCfg.cameraSwayFadeInLength
                                      : mCfg.cameraSwayFadeOutLength;
        const double fadeStep = (fadeLength > 0.0) ? (deltaTime / fadeLength) : 1.0;
        mCameraSwayFactor = math_utils::stepTowards(mCameraSwayFactor, mCameraSwayFactorTarget, fadeStep);

        const double scaledIntensity = mCfg.cameraSwayIntensity *
                                       std::pow(mCameraSwayFactor, kCameraSwayFadingSmoothness);

        // Multi-frequency harmonic sway
        const float swayPitch = std::sin(noiseTime * 6.2831853f * 0.7f) * 0.7f +
                                std::sin(noiseTime * 6.2831853f * 1.3f + 1.2f) * 0.3f;
        const float swayYaw = std::cos(noiseTime * 6.2831853f * 0.5f + 0.4f) * 0.6f +
                              std::sin(noiseTime * 6.2831853f * 1.1f + 2.1f) * 0.4f;

        output.pitch += static_cast<float>(swayPitch * scaledIntensity);
        output.yaw += static_cast<float>(swayYaw * scaledIntensity);
    }

    return output;
}

} // namespace camera_overhaul
