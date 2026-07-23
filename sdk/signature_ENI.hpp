#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include "signature_loader_ENI.hpp"
#include "CS2_Logger_ENI.hpp"
#include "module_ENI.hpp"

namespace sca {
namespace sig {

struct JsonSigEntry {
    std::string name;
    int         index{};
    std::string pattern;
    bool        class_unique{};
    bool        stub{};
    bool        unique{};
};

inline size_t batch_register_from_json(const std::string& json_path) {
    std::ifstream ifs(json_path);
    if (!ifs.is_open()) {
        LOG_WARNING("sig_autoload: cannot open {}", json_path);
        return 0;
    }

    std::stringstream ss;
    ss << ifs.rdbuf();
    std::string raw = ss.str();

    auto& reg = Registry::get();
    size_t count = 0;
    size_t pos = 0;

    auto skip_ws = [&]() { while (pos < raw.size() && (raw[pos]==' '||raw[pos]=='\t'||raw[pos]=='\n'||raw[pos]=='\r')) pos++; };
    auto expect = [&](char c) { skip_ws(); if (pos < raw.size() && raw[pos]==c) { pos++; return true; } return false; };
    auto read_string = [&]() -> std::string {
        skip_ws();
        if (pos >= raw.size() || raw[pos] != '"') return {};
        pos++;
        std::string r;
        while (pos < raw.size() && raw[pos] != '"') {
            if (raw[pos] == '\\') { pos++; if (pos < raw.size()) r += raw[pos++]; }
            else r += raw[pos++];
        }
        if (pos < raw.size()) pos++;
        return r;
    };
    auto read_value = [&]() -> std::string {
        skip_ws();
        if (pos >= raw.size()) return {};
        if (raw[pos] == '"') return read_string();
        size_t start = pos;
        while (pos < raw.size() && raw[pos]!=',' && raw[pos]!='}' && raw[pos]!='\n' && raw[pos]!='\r') pos++;
        std::string v = raw.substr(start, pos-start);
        while (!v.empty() && (v.back()==' '||v.back()=='\t'||v.back()=='\n'||v.back()=='\r')) v.pop_back();
        return v;
    };

    while (pos < raw.size()) {
        std::string key = read_string();
        if (key.empty()) { pos++; continue; }
        if (!expect(':')) { pos++; continue; }

        if (key == "modules") {
            if (!expect('{')) { pos++; continue; }
            continue;
        }

        skip_ws();
        if (pos >= raw.size() || raw[pos] != '{') { pos++; continue; }

        std::string module_name_key = key;
        std::string module_name = module_name_key;
        std::transform(module_name.begin(), module_name.end(), module_name.begin(), ::tolower);
        if (module_name.find('.') == std::string::npos) {
            if (module_name.find("_x64") != std::string::npos)
                module_name.replace(module_name.find("_x64"), 4, "");
            if (module_name.find(".dll") == std::string::npos)
                module_name += ".dll";
        }

        int depth = 1; pos++;
        while (depth > 0 && pos < raw.size()) {
            skip_ws();
            if (pos >= raw.size()) break;
            if (raw[pos] == '{') { depth++; pos++; continue; }
            if (raw[pos] == '}') { depth--; pos++; continue; }
            if (raw[pos] == '"') {
                std::string k = read_string();
                if (k.empty()) { pos++; continue; }
                skip_ws();
                if (pos < raw.size() && raw[pos] == ':') {
                    pos++; skip_ws();
                    if (pos < raw.size() && raw[pos] == '{') {
                        int inner_depth = 1; pos++;
                        JsonSigEntry sig;
                        sig.name = k;
                        while (inner_depth > 0 && pos < raw.size()) {
                            skip_ws();
                            if (pos >= raw.size()) break;
                            if (raw[pos] == '{') { inner_depth++; pos++; continue; }
                            if (raw[pos] == '}') { inner_depth--; pos++; continue; }
                            if (raw[pos] == '"') {
                                std::string fk = read_string();
                                if (fk.empty()) { pos++; continue; }
                                if (!expect(':')) { pos++; continue; }
                                std::string fv = read_value();
                                if (fk == "index") sig.index = std::stoi(fv);
                                else if (fk == "pattern") sig.pattern = fv;
                                else if (fk == "class_unique") sig.class_unique = (fv == "true");
                                else if (fk == "unique") sig.unique = (fv == "true");
                                else if (fk == "stub") sig.stub = (fv == "true");
                                if (expect(',')) {}
                            } else pos++;
                        }
                        if (sig.class_unique && !sig.pattern.empty()) {
                            reg.add(sig.name + "::idx_" + std::to_string(sig.index), module_name, sig.pattern, ResolveType::RAW);
                            count++;
                        }
                    } else if (pos < raw.size() && raw[pos] == '[') {
                        pos++; int arr_depth = 1;
                        while (arr_depth > 0 && pos < raw.size()) {
                            if (raw[pos] == '[') arr_depth++;
                            else if (raw[pos] == ']') arr_depth--;
                            pos++;
                        }
                    } else { while (pos < raw.size() && raw[pos] != '\n' && raw[pos] != '\r') pos++; }
                }
            } else pos++;
        }
    }

    LOG_INFO("sig_autoload: registered {} class-unique signatures from JSON", count);
    return count;
}

inline size_t batch_register_from_txt(const std::string& txt_path, const std::string& module_name) {
    std::ifstream ifs(txt_path);
    if (!ifs.is_open()) {
        LOG_WARNING("sig_autoload: cannot open {}", txt_path);
        return 0;
    }

    auto& reg = Registry::get();
    std::string line;
    size_t count = 0;

    while (std::getline(ifs, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t idx_pos = line.find("::idx_");
        if (idx_pos == std::string::npos) continue;

        std::string class_name = line.substr(0, idx_pos);
        size_t space_pos = line.find(' ', idx_pos + 6);
        if (space_pos == std::string::npos) continue;

        std::string pattern = line.substr(space_pos + 1);
        bool class_unique = pattern.find("[CLASS_UNIQUE]") != std::string::npos;
        if (!class_unique) continue;

        size_t marker_pos = pattern.find("  #");
        if (marker_pos != std::string::npos) pattern = pattern.substr(0, marker_pos);
        marker_pos = pattern.find(" [");
        if (marker_pos != std::string::npos) pattern = pattern.substr(0, marker_pos);
        while (!pattern.empty() && pattern.back() == ' ') pattern.pop_back();
        if (pattern.empty()) continue;

        std::string idx_str = line.substr(idx_pos + 6, space_pos - idx_pos - 6);
        reg.add(class_name + "::idx_" + idx_str, module_name, pattern, ResolveType::RAW);
        count++;
    }

    LOG_INFO("sig_autoload: loaded {} signatures from {}", count, txt_path);
    return count;
}

inline size_t autoload_all_signatures() {
    HMODULE hMod = GetModuleHandleA(nullptr);
    char mod_path[MAX_PATH];
    GetModuleFileNameA(hMod, mod_path, MAX_PATH);
    std::string base(mod_path);
    size_t sep = base.find_last_of('\\');
    if (sep != std::string::npos) base = base.substr(0, sep);

    auto try_load_json = [&](const std::string& rel) {
        std::string full = base + rel;
        std::ifstream t(full);
        if (t.good()) { t.close(); batch_register_from_json(full); }
    };
    auto try_load_txt = [&](const std::string& rel, const std::string& mod) {
        std::string full = base + rel;
        std::ifstream t(full);
        if (t.good()) { t.close(); batch_register_from_txt(full, mod); }
    };

    try_load_json("\\sdk\\signature\\_all-signatures.json");
    try_load_txt("\\sdk\\signature\\client.txt", "client.dll");
    try_load_txt("\\sdk\\signature\\engine2.txt", "engine2.dll");
    try_load_txt("\\sdk\\signature\\server.txt", "server.dll");
    try_load_txt("\\sdk\\signature\\schemasystem.txt", "schemasystem.dll");

    size_t total = Registry::get().count();
    LOG_INFO("sig_autoload: total {} signatures registered", total);
    return total;
}

} // namespace sig
} // namespace sca
