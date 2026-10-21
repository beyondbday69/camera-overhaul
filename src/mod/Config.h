#pragma once

#include <optional>
#include <string_view>

#include <pl/Config.hpp>

namespace camera_overhaul {

/**
 * @brief Typed configuration for the Camera Overhaul mod.
 *
 * The field layout is the single source of truth: the packaged
 * config/config.json and config/config.schema.json are produced from this
 * struct by src/config_generator.cpp.
 */
struct ModConfig {
    int version = 1;
    bool enabled = true;

    /** Field of view override in degrees. */
    int fov = 70;

    /** Enables dynamic camera lean/tilt when turning and strafing. */
    bool tilt_enabled = true;

    /** Multiplier for camera roll tilt angle. */
    float tilt_intensity = 1.0f;

    /** Speed at which tilt smoothly follows motion. */
    float tilt_smoothness = 12.0f;

    /** Enables over-the-shoulder third-person camera mode. */
    bool shoulder_cam_enabled = true;

    /** Sideways shoulder offset in blocks (+ right shoulder, - left shoulder). */
    float shoulder_side_offset = 0.75f;

    /** Vertical shoulder height offset in blocks. */
    float shoulder_height_offset = 0.20f;

    /** Forward/back distance offset in blocks. */
    float shoulder_back_offset = -0.15f;

    /** Enables procedural handheld camera shake (off by default). */
    bool shake_enabled = false;

    /** Rotational shake amplitude in degrees. */
    float shake_intensity = 0.35f;

    /** Frequency multiplier of the handheld motion. */
    float shake_speed = 1.0f;

    /** Amount of high-frequency jitter, 0 = smooth, 1 = gritty. */
    float shake_roughness = 0.55f;

    /** Low-frequency breathing sway while standing still, 0..1. */
    float shake_idle = 0.40f;
};

/** Lower bound of the field-of-view slider exposed in the Mod Menu. */
inline constexpr int kMinFov = 30;

/** Upper bound of the field-of-view slider exposed in the Mod Menu. */
inline constexpr int kMaxFov = 140;

/** Bounds of the tilt sliders. */
inline constexpr float kMinTiltIntensity = 0.0f;
inline constexpr float kMaxTiltIntensity = 3.0f;
inline constexpr float kMinTiltSmoothness = 2.0f;
inline constexpr float kMaxTiltSmoothness = 30.0f;

/** Bounds of the shoulder cam sliders. */
inline constexpr float kMinShoulderSide = -1.5f;
inline constexpr float kMaxShoulderSide = 1.5f;
inline constexpr float kMinShoulderHeight = -0.5f;
inline constexpr float kMaxShoulderHeight = 1.0f;
inline constexpr float kMinShoulderBack = -1.0f;
inline constexpr float kMaxShoulderBack = 1.0f;

/** Bounds of the shake sliders, shared by the config schema and the runtime. */
inline constexpr float kMinShakeIntensity = 0.0f;
inline constexpr float kMaxShakeIntensity = 3.0f;
inline constexpr float kMinShakeSpeed = 0.2f;
inline constexpr float kMaxShakeSpeed = 3.0f;
inline constexpr float kMinShakeRoughness = 0.0f;
inline constexpr float kMaxShakeRoughness = 1.0f;
inline constexpr float kMinShakeIdle = 0.0f;
inline constexpr float kMaxShakeIdle = 1.0f;

nlohmann::json makeDefaultConfigJson();
nlohmann::json makeConfigSchemaJson();

} // namespace camera_overhaul

template <> struct pl::config::Schema<camera_overhaul::ModConfig> {
    static constexpr std::string_view title = "Camera Overhaul";
    static constexpr std::string_view description =
        "Configuration for the Camera Overhaul native mod.";

    static constexpr FieldSchema field(std::string_view name) {
        if (name == "version")
            return {.title = "Version",
                    .description = "Config schema version managed by the mod.",
                    .minimum = std::nullopt,
                    .maximum = std::nullopt,
                    .readOnly = true};
        if (name == "enabled")
            return {.title = "Enabled",
                    .description = "Turns the Camera Overhaul module on or off.",
                    .minimum = std::nullopt,
                    .maximum = std::nullopt,
                    .readOnly = false};
        if (name == "fov")
            return {.title = "Field of View",
                    .description = "Camera field of view in degrees.",
                    .minimum = camera_overhaul::kMinFov,
                    .maximum = camera_overhaul::kMaxFov,
                    .readOnly = false};
        if (name == "tilt_enabled")
            return {.title = "Camera Lean / Tilt",
                    .description = "Tilts the camera sideways when turning and strafing.",
                    .minimum = std::nullopt,
                    .maximum = std::nullopt,
                    .readOnly = false};
        if (name == "tilt_intensity")
            return {.title = "Tilt Intensity",
                    .description = "Multiplier for how much the camera tilts.",
                    .minimum = camera_overhaul::kMinTiltIntensity,
                    .maximum = camera_overhaul::kMaxTiltIntensity,
                    .readOnly = false};
        if (name == "tilt_smoothness")
            return {.title = "Tilt Smoothness",
                    .description = "How smoothly the camera returns to center.",
                    .minimum = camera_overhaul::kMinTiltSmoothness,
                    .maximum = camera_overhaul::kMaxTiltSmoothness,
                    .readOnly = false};
        if (name == "shoulder_cam_enabled")
            return {.title = "Shoulder Camera",
                    .description = "Shifts third-person camera to the shoulder for clear crosshair aiming.",
                    .minimum = std::nullopt,
                    .maximum = std::nullopt,
                    .readOnly = false};
        if (name == "shoulder_side_offset")
            return {.title = "Shoulder Side Offset",
                    .description = "Sideways offset (+ for right shoulder, - for left).",
                    .minimum = camera_overhaul::kMinShoulderSide,
                    .maximum = camera_overhaul::kMaxShoulderSide,
                    .readOnly = false};
        if (name == "shoulder_height_offset")
            return {.title = "Shoulder Height",
                    .description = "Vertical camera height offset above shoulder.",
                    .minimum = camera_overhaul::kMinShoulderHeight,
                    .maximum = camera_overhaul::kMaxShoulderHeight,
                    .readOnly = false};
        if (name == "shoulder_back_offset")
            return {.title = "Shoulder Distance",
                    .description = "Forward/backward distance offset.",
                    .minimum = camera_overhaul::kMinShoulderBack,
                    .maximum = camera_overhaul::kMaxShoulderBack,
                    .readOnly = false};
        if (name == "shake_enabled")
            return {.title = "Handheld Noise Shake",
                    .description = "Optional procedural jitter/noise shake.",
                    .minimum = std::nullopt,
                    .maximum = std::nullopt,
                    .readOnly = false};
        if (name == "shake_intensity")
            return {.title = "Shake Intensity",
                    .description = "Rotational shake amplitude in degrees.",
                    .minimum = camera_overhaul::kMinShakeIntensity,
                    .maximum = camera_overhaul::kMaxShakeIntensity,
                    .readOnly = false};
        if (name == "shake_speed")
            return {.title = "Shake Speed",
                    .description = "How fast the handheld motion drifts.",
                    .minimum = camera_overhaul::kMinShakeSpeed,
                    .maximum = camera_overhaul::kMaxShakeSpeed,
                    .readOnly = false};
        if (name == "shake_roughness")
            return {.title = "Shake Roughness",
                    .description = "Amount of high-frequency jitter.",
                    .minimum = camera_overhaul::kMinShakeRoughness,
                    .maximum = camera_overhaul::kMaxShakeRoughness,
                    .readOnly = false};
        if (name == "shake_idle")
            return {.title = "Idle Sway",
                    .description = "Low-frequency sway while standing still.",
                    .minimum = camera_overhaul::kMinShakeIdle,
                    .maximum = camera_overhaul::kMaxShakeIdle,
                    .readOnly = false};
        return {};
    }
};
