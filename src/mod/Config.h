#pragma once

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
    int fov = 70;
};

/** Lower bound of the field-of-view slider exposed in the Mod Menu. */
inline constexpr int kMinFov = 30;

/** Upper bound of the field-of-view slider exposed in the Mod Menu. */
inline constexpr int kMaxFov = 140;

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
                    .readOnly = true};
        if (name == "enabled")
            return {.title = "Enabled",
                    .description = "Turns the Camera Overhaul module on or off."};
        if (name == "fov")
            return {.title = "Field of View",
                    .description = "Camera field of view in degrees.",
                    .minimum = camera_overhaul::kMinFov,
                    .maximum = camera_overhaul::kMaxFov};
        return {};
    }
};
