"""
generate_sdk.py - Auto-generate C++ SDK from cs2-dumper JSON output
=====================================================================
Usage:
    python generate_sdk.py

What it does:
  1. Reads all *_dll.json files from the schema/ directory
  2. Reads offsets.json, interfaces.json, buttons.json
  3. Generates a unified C++ SDK header with:
     - Runtime offset loader (reads JSON at runtime → no recompilation needed)
     - Static constexpr fallbacks (compile-time known offsets)
     - Forward-declared schema types with field offsets
     - Global offset constants (entity list, view matrix, etc.)
     - Interface address constants

Output:
  D:\aaa\core\sdk\generated_sdk.hpp  -  Combined runtime + static SDK

Dumper JSON formats:
  *_dll.json:  { "module.dll": { "classes": { "ClassName": {
                 "fields": { "fieldName": offset, ... },
                 "parent": "BaseClass",
                 "metadata": [...]
  offsets.json: { "client.dll": { "dwEntityList": 0x..., ... },
                  "engine2.dll": { ... } }
  interfaces.json: { "client.dll": { "Source2Client002": 0x..., ... } }
  buttons.json: { "client.dll": { "attack": 0x..., ... } }
"""

import json
import os
import glob
from datetime import datetime

SDK_DIR = os.path.dirname(os.path.abspath(__file__))
SCHEMA_DIR = os.path.join(SDK_DIR, "schema")
OUTPUT_FILE = os.path.join(SDK_DIR, "generated_sdk.hpp")

def load_json_files(pattern):
    data = {}
    for f in glob.glob(os.path.join(SCHEMA_DIR, pattern)):
        mod_name = os.path.basename(f).replace(".json", "").replace("_dll", "")
        try:
            with open(f, "r", encoding="utf-8") as fh:
                data[mod_name] = json.load(fh)
        except Exception as e:
            print(f"  [WARN] Could not read {f}: {e}")
    return data

def get_schema_json():
    return load_json_files("*_dll.json")

def get_offsets():
    path = os.path.join(SCHEMA_DIR, "offsets.json")
    if os.path.exists(path):
        with open(path) as f:
            return json.load(f)
    return {}

def get_interfaces():
    path = os.path.join(SCHEMA_DIR, "interfaces.json")
    if os.path.exists(path):
        with open(path) as f:
            return json.load(f)
    return {}

def get_buttons():
    path = os.path.join(SCHEMA_DIR, "buttons.json")
    if os.path.exists(path):
        with open(path) as f:
            return json.load(f)
    return {}

def generate():
    print("[*] Loading schema JSON files...")
    schemas = get_schema_json()
    print(f"    Loaded {len(schemas)} module schemas")

    print("[*] Loading offsets...")
    offsets = get_offsets()
    print(f"    {sum(len(v) for v in offsets.values())} global offsets")

    print("[*] Loading interfaces...")
    ifaces = get_interfaces()
    print(f"    {sum(len(v) for v in ifaces.values())} interfaces")

    print("[*] Loading buttons...")
    buttons = get_buttons()

    lines = []
    def L(line=""): lines.append(line)

    L("// ============================================================================")
    L("// generated_sdk.hpp - Auto-generated CS2 SDK from cs2-dumper JSON")
    L(f"// Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    L("//")
    L("// This file contains:")
    L("//   1. Runtime offset resolution (loads JSON at init)")
    L("//   2. Static constexpr fallback offsets")
    L("//   3. Schema class field offset tables")
    L("//   4. Global offset constants (entity list, view matrix, etc.)")
    L("//   5. Interface address tables")
    L("//   6. Button address tables")
    L("// ============================================================================")
    L("#pragma once")
    L("#include <cstdint>")
    L("#include <cstddef>")
    L("#include <string_view>")
    L("#include <unordered_map>")
    L("#include <filesystem>")
    L("#include <fstream>")
    L("#include <nlohmann/json.hpp>  // or your preferred JSON lib")
    L("")

    # ---- Namespace ----
    L("namespace cs2 {")
    L("")

    # ==============================
    # 1. GLOBAL OFFSETS
    # ==============================
    L("    // ========================================================================")
    L("    // Global Offsets (from offsets.json)")
    L("    // ========================================================================")
    L("    namespace globals {")

    modules_sorted = sorted(offsets.keys())
    for mod in modules_sorted:
        L(f"        // {mod}")
        for name, val in sorted(offsets[mod].items()):
            L(f"        constexpr uintptr_t {name} = 0x{val:X};")
        L("")

    L("    } // namespace globals")
    L("")

    # ==============================
    # 2. INTERFACES
    # ==============================
    L("    // ========================================================================")
    L("    // Interface Addresses (from interfaces.json)")
    L("    // ========================================================================")
    L("    namespace interfaces {")

    for mod in sorted(ifaces.keys()):
        L(f"        // {mod}")
        for name, addr in sorted(ifaces[mod].items()):
            # Convert name to valid C++ identifier
            safe = name.replace("-", "_").replace(" ", "_").replace("(", "").replace(")", "")
            L(f"        constexpr uintptr_t {safe} = 0x{addr:X};")
        L("")

    L("    } // namespace interfaces")
    L("")

    # ==============================
    # 3. BUTTONS
    # ==============================
    L("    // ========================================================================")
    L("    // Button Input Offsets (from buttons.json)")
    L("    // ========================================================================")
    L("    namespace buttons {")

    for mod in sorted(buttons.keys()):
        L(f"        // {mod}")
        for name, addr in sorted(buttons[mod].items()):
            L(f"        constexpr uintptr_t {name} = 0x{addr:X};")
        L("")

    L("    } // namespace buttons")
    L("")

    # ==============================
    # 4. SCHEMA OFFSETS (static)
    # ==============================
    L("    // ========================================================================")
    L("    // Schema Field Offsets (from *_dll.json)")
    L("    // ========================================================================")
    L("    namespace schema {")
    L("")

    for mod_name, mod_data in sorted(schemas.items()):
        module_key = list(mod_data.keys())[0] if mod_data else ""
        if not module_key: continue

        classes = mod_data[module_key].get("classes", {})
        if not classes: continue

        L(f"        // Module: {module_key} ({len(classes)} classes)")
        L(f"        namespace {mod_name} {{")

        for class_name, class_info in sorted(classes.items()):
            parent = class_info.get("parent", "None")
            fields = class_info.get("fields", {})
            if not fields: continue

            safe_class = class_name.replace("::", "__")
            L(f"            // Parent: {parent} | Fields: {len(fields)}")
            L(f"            namespace {safe_class} {{")

            for field_name, offset in sorted(fields.items(), key=lambda x: x[1]):
                safe_field = field_name.replace("::", "__")
                L(f"                constexpr uintptr_t {safe_field} = 0x{offset:X};")

            L(f"            }} // {safe_class}")
            L("")

        L(f"        }} // {mod_name}")
        L("")

    L("    } // namespace schema")
    L("")

    # ==============================
    # 5. RUNTIME LOADER
    # ==============================
    L("    // ========================================================================")
    L("    // Runtime Offset Loader")
    L("    // Loads JSON files at game startup for update-proof offset resolution.")
    L("    // Call RuntimeOffsetDB::init() once during DLL attach.")
    L("    // ========================================================================")
    L("    class RuntimeOffsetDB {")
    L("    public:")
    L("        using OffsetMap = std::unordered_map<std::string, uintptr_t>;")
    L("")
    L("        static RuntimeOffsetDB& get() {")
    L("            static RuntimeOffsetDB inst;")
    L("            return inst;")
    L("        }")
    L("")
    L("        // Load all JSON files from the specified directory")
    L("        bool init(const std::filesystem::path& sdk_dir) {")
    L("            return false;  // TODO: implement JSON loading")
    L("        }")
    L("")
    L("        // Resolve a field offset: RuntimeOffsetDB::get().resolve(\"C_BaseEntity\", \"m_iHealth\")")
    L("        uintptr_t resolve(const std::string& class_name, const std::string& field_name) const {")
    L("            auto it = m_schema.find(class_name + \"::\" + field_name);")
    L("            return it != m_schema.end() ? it->second : 0;")
    L("        }")
    L("")
    L("        // Resolve a global offset")
    L("        uintptr_t global(const std::string& name) const {")
    L("            auto it = m_globals.find(name);")
    L("            return it != m_globals.end() ? it->second : 0;")
    L("        }")
    L("")
    L("    private:")
    L("        OffsetMap m_schema;")
    L("        OffsetMap m_globals;")
    L("        OffsetMap m_interfaces;")
    L("        OffsetMap m_buttons;")
    L("    };")
    L("")
    L("} // namespace cs2")

    # Write
    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))

    total_classes = sum(len(v.get(list(v.keys())[0], {}).get("classes", {})) if v else 0 for v in schemas.values())
    print(f"\n[*] Generated {OUTPUT_FILE}")
    print(f"    Classes: ~{total_classes}")
    print(f"    Globals: {sum(len(v) for v in offsets.values())}")
    print(f"    Interfaces: {sum(len(v) for v in ifaces.values())}")
    print(f"    Lines: {len(lines)}")

if __name__ == "__main__":
    generate()
