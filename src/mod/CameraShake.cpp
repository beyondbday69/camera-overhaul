#include "mod/CameraShake.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace camera_overhaul {
namespace {

constexpr float kTwoPi = 6.283185307179586f;

/** Final safety clamp so a bad slider can never fight the player's input. */
constexpr float kMaxOffsetDegrees = 12.0f;

/** How long an impulse keeps ringing, in seconds. */
constexpr double kImpulseLifetime = 3.0;

inline std::uint32_t mix(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

/** Stable pseudo-random lattice value in [-1, 1] for an integer coordinate. */
inline float latticeValue(std::int32_t i, std::uint32_t seed) {
    const std::uint32_t h = mix(static_cast<std::uint32_t>(i) * 0x9e3779b9U ^ seed);
    return static_cast<float>(h >> 8) * (2.0f / 16777216.0f) - 1.0f;
}

/** Quintic fade: zero first and second derivative at the lattice points. */
inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

/** Smooth 1D value noise in roughly [-1, 1]. */
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
    mEnabled.store(settings.enabled, std::memory_order_relaxed);
    mIntensity.store(std::clamp(settings.intensity, 0.0f, 3.0f),
                     std::memory_order_relaxed);
    mSpeed.store(std::clamp(settings.speed, 0.2f, 3.0f), std::memory_order_relaxed);
    mRoughness.store(std::clamp(settings.roughness, 0.0f, 1.0f),
                     std::memory_order_relaxed);
    mIdle.store(std::clamp(settings.idle, 0.0f, 1.0f), std::memory_order_relaxed);
}

ShakeSettings CameraShake::settings() const {
    ShakeSettings result;
    result.enabled = mEnabled.load(std::memory_order_relaxed);
    result.intensity = mIntensity.load(std::memory_order_relaxed);
    result.speed = mSpeed.load(std::memory_order_relaxed);
    result.roughness = mRoughness.load(std::memory_order_relaxed);
    result.idle = mIdle.load(std::memory_order_relaxed);
    return result;
}

void CameraShake::impulse(float strength) {
    if (!(strength > 0.0f)) {
        return;
    }
    // Sum concurrent impulses; sample() drains the accumulator once per frame.
    mPendingImpulse.fetch_add(std::min(strength, 4.0f), std::memory_order_relaxed);
}

void CameraShake::reset() {
    mPendingImpulse.store(0.0f, std::memory_order_relaxed);
    mImpulses.fill({});
    mNextImpulse = 0;
}

ShakeOffset CameraShake::sample(double timeSeconds) {
    const float pending = mPendingImpulse.exchange(0.0f, std::memory_order_relaxed);

    if (!mEnabled.load(std::memory_order_relaxed)) {
        return {};
    }

    if (pending > 0.0f) {
        mImpulses[mNextImpulse] = Impulse{timeSeconds, pending};
        mNextImpulse = (mNextImpulse + 1) % kMaxImpulses;
    }

    const float intensity = mIntensity.load(std::memory_order_relaxed);
    const float speed = mSpeed.load(std::memory_order_relaxed);
    const float roughness = mRoughness.load(std::memory_order_relaxed);
    const float idle = mIdle.load(std::memory_order_relaxed);

    const float t = static_cast<float>(timeSeconds) * speed;

    // Slow handheld drift: the dominant, always-present motion.
    float yaw = 0.60f * valueNoise(t * 0.23f + 13.7f, 0x1a2b3c4dU);
    float pitch = 0.50f * valueNoise(t * 0.21f + 41.3f, 0x5e6f7a8bU);

    // Medium sway: gives the drift some body.
    yaw += 0.28f * valueNoise(t * 1.05f + 3.1f, 0x0badf00dU);
    pitch += 0.26f * valueNoise(t * 0.97f + 9.4f, 0x0ddba11U);

    // High-frequency jitter: the "grit" of a handheld shot.
    yaw += roughness * 0.18f * valueNoise(t * 6.3f + 1.7f, 0x00c0ffeeU);
    pitch += roughness * 0.16f * valueNoise(t * 6.9f + 5.9f, 0xdead10ccU);

    yaw *= intensity;
    pitch *= intensity;

    // Idle breathing: very slow, tiny sway that keeps the frame alive.
    const float breath = idle * intensity * 0.30f;
    yaw += breath * 0.60f * std::sin(t * 0.11f * kTwoPi + 0.3f);
    pitch += breath * std::sin(t * 0.09f * kTwoPi + 1.7f);

    // Impulse responses: damped oscillations on landing/hit.
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
    ShakeOffset result;
    result.yaw = std::clamp(yaw, -limit, limit);
    result.pitch = std::clamp(pitch, -limit, limit);
    result.yaw = std::clamp(result.yaw, -kMaxOffsetDegrees, kMaxOffsetDegrees);
    result.pitch = std::clamp(result.pitch, -kMaxOffsetDegrees, kMaxOffsetDegrees);
    return result;
}

} // namespace camera_overhaul
