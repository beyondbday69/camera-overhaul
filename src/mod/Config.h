#pragma once

#include <optional>
#include <string_view>

#include <pl/Config.hpp>

namespace camera_overhaul {

/**
 * @brief Typed configuration for the Camera Overhaul mod.
 */
struct ModConfig {
    int version = 1;
    bool enabled = true;

    /** Field of view override in degrees. */
    int fov = 70;

    /** Enables dynamic camera lean / tilt when turning and strafing. */
    bool tilt_enabled = true;

    /** Multiplier for camera roll tilt angle. */
    float tilt_intensity = 1.0f;

    /** How smoothly camera leans and returns to center. */
    float tilt_smoothness = 8.0f;

    /** Enables native GPU accumulation motion blur. */
    bool motion_blur_enabled = false;

    /** Motion blur trail strength (0.05 to 0.90). */
    float motion_blur_intensity = 0.55f;
};

/** Lower bound of the field-of-view slider exposed in the Mod Menu. */
inline constexpr int kMinFov = 30;

/** Upper bound of the field-of-view slider exposed in the Mod Menu. */
inline constexpr int kMaxFov = 140;

/** Bounds of the tilt sliders. */
inline constexpr float kMinTiltIntensity = 0.0f;
inline constexpr float kMaxTiltIntensity = 3.0f;
inline constexpr float kMinTiltSmoothness = 2.0f;
inline constexpr float kMaxTiltSmoothness = 25.0f;

/** Bounds of the motion blur sliders. */
inline constexpr float kMinMotionBlurIntensity = 0.05f;
inline constexpr float kMaxMotionBlurIntensity = 0.90f;

nlohmann::json makeDefaultConfigJson();
nlohmann::json makeConfigSchemaJson();

} // namespace camera_overhaul

template <> struct pl::config::Schema<camera_overhaul::ModConfig> {
    static constexpr std::string_view title = "Camera Overhaul";
    static constexpr std::string_view description =
        "Dynamic camera lean, tilt, and native motion blur mod for Minecraft Bedrock.";

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
                    .description = "Smoothly tilts the camera when turning and strafing.",
                    .minimum = std::nullopt,
                    .maximum = std::nullopt,
                    .readOnly = false};
        if (name == "tilt_intensity")
            return {.title = "Tilt Intensity",
                    .description = "Multiplier for camera roll angle.",
                    .minimum = camera_overhaul::kMinTiltIntensity,
                    .maximum = camera_overhaul::kMaxTiltIntensity,
                    .readOnly = false};
        if (name == "tilt_smoothness")
            return {.title = "Tilt Smoothness",
                    .description = "How smoothly the camera returns to center.",
                    .minimum = camera_overhaul::kMinTiltSmoothness,
                    .maximum = camera_overhaul::kMaxTiltSmoothness,
                    .readOnly = false};
        if (name == "motion_blur_enabled")
            return {.title = "Motion Blur",
                    .description = "Enables cinematic GPU accumulation motion blur.",
                    .minimum = std::nullopt,
                    .maximum = std::nullopt,
                    .readOnly = false};
        if (name == "motion_blur_intensity")
            return {.title = "Blur Intensity",
                    .description = "Motion blur trail strength.",
                    .minimum = camera_overhaul::kMinMotionBlurIntensity,
                    .maximum = camera_overhaul::kMaxMotionBlurIntensity,
                    .readOnly = false};
        return {};
    }
};
