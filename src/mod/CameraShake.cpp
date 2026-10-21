#include "mod/CameraShake.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace camera_overhaul {
namespace {

constexpr float kTwoPi = 6.283185307179586f;
constexpr float kMaxOffsetDegrees = 12.0f;
constexpr double kImpulseLifetime = 3.0;

inline std::uint32_t mix(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

inline float latticeValue(std::int32_t i, std::uint32_t seed) {
    const std::uint32_t h = mix(static_cast<std::uint32_t>(i) * 0x9e3779b9U ^ seed);
    return static_cast<float>(h >> 8) * (2.0f / 16777216.0f) - 1.0f;
}

inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

inline float valueNoise(float x, std::uint32_t seed) {
    const float base = std::floor(x);
    const std::int32_t i = static_cast<std::int32_t>(base);
    const float f = x - base;
    const float a = latticeValue(i, seed);
    const float b = latticeValue(i + 1, seed);
    return a + (b - a) * fade(f);
}

} // namespace

void CameraShake::configure(const ShakeSettings &settings) {
    mNoiseEnabled.store(settings.enabled, std::memory_order_relaxed);
    mIntensity.store(std::clamp(settings.intensity, 0.0f, 3.0f),
                     std::memory_order_relaxed);
    mSpeed.store(std::clamp(settings.speed, 0.2f, 3.0f), std::memory_order_relaxed);
    mRoughness.store(std::clamp(settings.roughness, 0.0f, 1.0f),
                     std::memory_order_relaxed);
    mIdle.store(std::clamp(settings.idle, 0.0f, 1.0f), std::memory_order_relaxed);

    mTiltEnabled.store(settings.tiltEnabled, std::memory_order_relaxed);
    mTiltIntensity.store(std::clamp(settings.tiltIntensity, 0.0f, 3.0f),
                         std::memory_order_relaxed);
    mTiltSmoothness.store(std::clamp(settings.tiltSmoothness, 2.0f, 30.0f),
                          std::memory_order_relaxed);

    mShoulderEnabled.store(settings.shoulderEnabled, std::memory_order_relaxed);
    mShoulderSide.store(std::clamp(settings.shoulderSide, -1.5f, 1.5f),
                        std::memory_order_relaxed);
    mShoulderHeight.store(std::clamp(settings.shoulderHeight, -0.5f, 1.0f),
                          std::memory_order_relaxed);
    mShoulderBack.store(std::clamp(settings.shoulderBack, -1.0f, 1.0f),
                        std::memory_order_relaxed);
}

ShakeSettings CameraShake::settings() const {
    ShakeSettings result;
    result.enabled = mNoiseEnabled.load(std::memory_order_relaxed);
    result.intensity = mIntensity.load(std::memory_order_relaxed);
    result.speed = mSpeed.load(std::memory_order_relaxed);
    result.roughness = mRoughness.load(std::memory_order_relaxed);
    result.idle = mIdle.load(std::memory_order_relaxed);

    result.tiltEnabled = mTiltEnabled.load(std::memory_order_relaxed);
    result.tiltIntensity = mTiltIntensity.load(std::memory_order_relaxed);
    result.tiltSmoothness = mTiltSmoothness.load(std::memory_order_relaxed);

    result.shoulderEnabled = mShoulderEnabled.load(std::memory_order_relaxed);
    result.shoulderSide = mShoulderSide.load(std::memory_order_relaxed);
    result.shoulderHeight = mShoulderHeight.load(std::memory_order_relaxed);
    result.shoulderBack = mShoulderBack.load(std::memory_order_relaxed);
    return result;
}

void CameraShake::addTouchMotion(float dx, float dy, bool isLook) {
    (void)dy;
    if (isLook) {
        mAccumulatedTurnVelocity.fetch_add(dx, std::memory_order_relaxed);
    } else {
        mAccumulatedStrafeVelocity.fetch_add(dx, std::memory_order_relaxed);
    }
}

void CameraShake::setKeyStrafe(int direction) {
    mKeyStrafeDir.store(direction, std::memory_order_relaxed);
}

void CameraShake::toggleShoulderSide() {
    const float current = mShoulderSide.load(std::memory_order_relaxed);
    mShoulderSide.store(-current, std::memory_order_relaxed);
}

void CameraShake::impulse(float strength) {
    if (!(strength > 0.0f)) {
        return;
    }
    mPendingImpulse.fetch_add(std::min(strength, 4.0f), std::memory_order_relaxed);
}

void CameraShake::reset() {
    mPendingImpulse.store(0.0f, std::memory_order_relaxed);
    mAccumulatedTurnVelocity.store(0.0f, std::memory_order_relaxed);
    mAccumulatedStrafeVelocity.store(0.0f, std::memory_order_relaxed);
    mKeyStrafeDir.store(0, std::memory_order_relaxed);
    mCurrentRoll = 0.0f;
    mTargetRoll = 0.0f;
    mImpulses.fill({});
    mNextImpulse = 0;
}

PositionOffset CameraShake::samplePosition() const {
    if (!mShoulderEnabled.load(std::memory_order_relaxed)) {
        return {};
    }
    PositionOffset pos;
    pos.x = mShoulderSide.load(std::memory_order_relaxed);
    pos.y = mShoulderHeight.load(std::memory_order_relaxed);
    pos.z = mShoulderBack.load(std::memory_order_relaxed);
    return pos;
}

ShakeOffset CameraShake::sample(double timeSeconds, float dt) {
    const float pending = mPendingImpulse.exchange(0.0f, std::memory_order_relaxed);
    const float turnDx = mAccumulatedTurnVelocity.exchange(0.0f, std::memory_order_relaxed);
    const float strafeDx = mAccumulatedStrafeVelocity.exchange(0.0f, std::memory_order_relaxed);
    const int keyDir = mKeyStrafeDir.load(std::memory_order_relaxed);

    ShakeOffset result{};

    // -----------------------------------------------------------------------
    // Dynamic camera lean / roll tilt
    // -----------------------------------------------------------------------
    if (mTiltEnabled.load(std::memory_order_relaxed)) {
        const float intensity = mTiltIntensity.load(std::memory_order_relaxed);
        const float smoothness = mTiltSmoothness.load(std::memory_order_relaxed);

        // Turn roll: turning right leans right, turning left leans left.
        const float turnRoll = std::clamp(-turnDx * 0.12f, -5.0f, 5.0f);

        // Touch strafe roll: joystick swipe left/right.
        const float touchStrafeRoll = std::clamp(-strafeDx * 0.06f, -4.0f, 4.0f);

        // Keyboard strafe roll (keys A / D).
        const float keyStrafeRoll = (keyDir != 0) ? (static_cast<float>(keyDir) * 3.5f) : 0.0f;

        const float target = std::clamp((turnRoll + touchStrafeRoll + keyStrafeRoll) * intensity,
                                        -8.0f * intensity, 8.0f * intensity);

        // Smooth exponential spring towards target
        const float frameDt = std::clamp(dt, 0.001f, 0.1f);
        const float factor = 1.0f - std::exp(-smoothness * frameDt);
        mCurrentRoll += (target - mCurrentRoll) * factor;

        result.roll = std::clamp(mCurrentRoll, -kMaxOffsetDegrees, kMaxOffsetDegrees);
    } else {
        mCurrentRoll = 0.0f;
    }

    // -----------------------------------------------------------------------
    // Optional handheld noise shake (disabled by default)
    // -----------------------------------------------------------------------
    if (mNoiseEnabled.load(std::memory_order_relaxed)) {
        if (pending > 0.0f) {
            mImpulses[mNextImpulse] = Impulse{timeSeconds, pending};
            mNextImpulse = (mNextImpulse + 1) % kMaxImpulses;
        }

        const float intensity = mIntensity.load(std::memory_order_relaxed);
        const float speed = mSpeed.load(std::memory_order_relaxed);
        const float roughness = mRoughness.load(std::memory_order_relaxed);
        const float idle = mIdle.load(std::memory_order_relaxed);

        const float t = static_cast<float>(timeSeconds) * speed;

        float yaw = 0.60f * valueNoise(t * 0.23f + 13.7f, 0x1a2b3c4dU);
        float pitch = 0.50f * valueNoise(t * 0.21f + 41.3f, 0x5e6f7a8bU);

        yaw += 0.28f * valueNoise(t * 1.05f + 3.1f, 0x0badf00dU);
        pitch += 0.26f * valueNoise(t * 0.97f + 9.4f, 0x0ddba11U);

        yaw += roughness * 0.18f * valueNoise(t * 6.3f + 1.7f, 0x00c0ffeeU);
        pitch += roughness * 0.16f * valueNoise(t * 6.9f + 5.9f, 0xdead10ccU);

        yaw *= intensity;
        pitch *= intensity;

        const float breath = idle * intensity * 0.30f;
        yaw += breath * 0.60f * std::sin(t * 0.11f * kTwoPi + 0.3f);
        pitch += breath * std::sin(t * 0.09f * kTwoPi + 1.7f);

        for (const auto &entry : mImpulses) {
            const double age = timeSeconds - entry.start;
            if (entry.strength <= 0.0f || age <= 0.0 || age >= kImpulseLifetime) {
                continue;
            }
            const float a = static_cast<float>(age);
            const float envelope = std::exp(-4.0f * a) * entry.strength;
            yaw += envelope * 0.70f * std::sin(a * kTwoPi * 3.1f);
            pitch += envelope * 0.50f * std::sin(a * kTwoPi * 2.6f + 1.2f);
        }

        const float limit = std::max(0.5f, intensity * 5.0f);
        result.yaw = std::clamp(yaw, -limit, limit);
        result.pitch = std::clamp(pitch, -limit, limit);
        result.yaw = std::clamp(result.yaw, -kMaxOffsetDegrees, kMaxOffsetDegrees);
        result.pitch = std::clamp(result.pitch, -kMaxOffsetDegrees, kMaxOffsetDegrees);
    }

    return result;
}

} // namespace camera_overhaul
