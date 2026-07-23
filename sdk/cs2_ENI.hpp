#pragma once

#include <cstdint>
#include <cstddef>

inline constexpr unsigned int CS2_BUILD_ENI = 14171;

// Pattern library from cs2-dumper (compile-time constexpr patterns)
#include "patterns_ENI.hpp"

// Interface vtable structs from cs2-dumper
#include "interfaces_ENI.hpp"

// Per-module schema class definitions from cs2-dumper
#include "schema/animationsystem_dll.hpp"
#include "schema/engine2_dll.hpp"
#include "schema/host_dll.hpp"
#include "schema/materialsystem2_dll.hpp"
#include "schema/networksystem_dll.hpp"
#include "schema/panorama_dll.hpp"
#include "schema/particles_dll.hpp"
#include "schema/pulse_system_dll.hpp"
#include "schema/rendersystemdx11_dll.hpp"
#include "schema/resourcesystem_dll.hpp"
#include "schema/scenesystem_dll.hpp"
#include "schema/schemasystem_dll.hpp"
#include "schema/client_dll.hpp"
#include "schema/server_dll.hpp"
#include "schema/soundsystem_dll.hpp"
#include "schema/steamaudio_dll.hpp"
#include "schema/vphysics2_dll.hpp"
#include "schema/worldrenderer_dll.hpp"

// Additional dumper modules
// convars.hpp excluded — broken auto-generated comments, runtime via JSON offsets instead
// #include "convars/convars.hpp"
#include "engine/ccsgoinput.h"
#include "engine/cusercmd.h"
#include "engine/cviewsetup.h"
// #include "impl/entity_system.hpp" /* needs dumper offsets -- use sca::schema::get_offset instead */
// #include "protobufs/protobufs.hpp" /* not available */

// JSON-based runtime offset loader (reads offsets/*.json at runtime)
#include "offsets_ENI.hpp"

// JSON/TXT-based signature autoloader (reads signature/* at runtime)
#include "signature_ENI.hpp"

// Auto-load all offsets and signatures into the runtime system
namespace sca {
namespace autoload {

inline bool load_all() {
    bool ok = true;

    if (!offsets::load_all_json()) {
        LOG_WARNING("autoload: no offset JSON files loaded");
        ok = false;
    }

    size_t sig_count = sig::autoload_all_signatures();
    if (sig_count == 0) {
        LOG_WARNING("autoload: no signatures loaded");
        ok = false;
    }

    LOG_INFO("autoload: offsets={}, sigs={}",
           offsets::JsonOffsetLoader::get().cache_size(), sig_count);
    return ok;
}

} // namespace autoload
} // namespace sca
