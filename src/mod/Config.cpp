#include "mod/Config.h"

namespace camera_overhaul {

nlohmann::json makeDefaultConfigJson() { return pl::config::defaultJson(ModConfig{}); }

nlohmann::json makeConfigSchemaJson() { return pl::config::schema(ModConfig{}); }

} // namespace camera_overhaul
