#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <mutex>
#include <unordered_set>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include "CS2_Logger_ENI.hpp"

namespace sca {
namespace offsets {

class JsonOffsetLoader {
public:
    static JsonOffsetLoader& get() {
        static JsonOffsetLoader inst;
        return inst;
    }

    bool load(const std::string& json_path) {
        std::lock_guard lock(mutex_);
        if (loaded_.count(json_path)) return true;

        std::ifstream ifs(json_path);
        if (!ifs.is_open()) {
            LOG_WARNING("offset_loader: cannot open {}", json_path);
            return false;
        }

        std::stringstream ss;
        ss << ifs.rdbuf();
        std::string raw = ss.str();
        parse_json(raw, json_path);
        loaded_.insert(json_path);
        return true;
    }

    bool load_all() {
        std::vector<std::string> search_paths;
        char mod_path[MAX_PATH]{};
        GetModuleFileNameA(nullptr, mod_path, MAX_PATH);
        std::string base(mod_path);
        size_t sep = base.find_last_of('\\');
        if (sep != std::string::npos) base = base.substr(0, sep);
        search_paths.push_back(base + "\\sdk\\offsets\\");
        search_paths.push_back(base + "\\offsets\\");
        search_paths.push_back(base + "\\data\\offsets\\");

        size_t total_count = 0;
        for (const auto& dir : search_paths) {
            WIN32_FIND_DATAA ffd;
            HANDLE hFind = FindFirstFileA((dir + "*.json").c_str(), &ffd);
            if (hFind == INVALID_HANDLE_VALUE) continue;

            size_t count = 0;
            do {
                if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    std::string fname = ffd.cFileName;
                    if (fname != "buttons.json" && fname != "interfaces.json") {
                        load(dir + fname);
                        count++;
                    }
                }
            } while (FindNextFileA(hFind, &ffd) != 0);
            FindClose(hFind);
            total_count += count;
            if (count > 0) {
                LOG_INFO("offset_loader: loaded {} files from {}", count, dir);
                break;
            }
        }

        if (total_count == 0)
            LOG_WARNING("offset_loader: no offset JSON files found in any search path");
        else
            LOG_INFO("offset_loader: total {} offset files", total_count);
        return total_count > 0;
    }

    uint32_t get_offset(const std::string_view class_name, const std::string_view field_name) {
        std::lock_guard lock(mutex_);
        auto key = std::string(class_name) + "::" + std::string(field_name);
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;

        auto it2 = cache_.find(std::string(field_name));
        if (it2 != cache_.end()) return it2->second;

        return 0;
    }

    bool has_offset(const std::string_view class_name, const std::string_view field_name) {
        return get_offset(class_name, field_name) != 0;
    }

    size_t cache_size() const {
        std::lock_guard lock(mutex_);
        return cache_.size();
    }

    void clear() {
        std::lock_guard lock(mutex_);
        cache_.clear();
        loaded_.clear();
    }

private:
    // Robust JSON parser supporting dumper-cs2 format + standard JSON
    void parse_json(const std::string& raw, const std::string& source) {
        size_t pos = 0;
        std::string current_class;

        auto skip_ws = [&]() { while (pos < raw.size() && (raw[pos]==' '||raw[pos]=='\t'||raw[pos]=='\n'||raw[pos]=='\r')) pos++; };
        auto read_str = [&]() -> std::string {
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
        auto read_val = [&]() -> std::string {
            skip_ws();
            if (pos >= raw.size()) return {};
            if (raw[pos] == '"') return read_str();
            size_t start = pos;
            while (pos < raw.size() && raw[pos]!=',' && raw[pos]!='}' && raw[pos]!=']' && raw[pos]!='\n' && raw[pos]!='\r') pos++;
            std::string v = raw.substr(start, pos-start);
            while (!v.empty() && (v.back()==' '||v.back()=='\t'||v.back()=='\n'||v.back()=='\r')) v.pop_back();
            return v;
        };
        auto parse_hex = [](const std::string& s) -> uint32_t {
            if (s.empty()) return 0;
            std::string clean;
            for (auto c : s) if (c != ' ' && c != '\t') clean += c;
            if (clean.size() > 2 && (clean[0]=='0'&&(clean[1]=='x'||clean[1]=='X')))
                return static_cast<uint32_t>(std::stoull(clean, nullptr, 16));
            if (clean.size() > 1 && clean[0] == '0' && clean[1] == 'X')
                return static_cast<uint32_t>(std::stoull(clean, nullptr, 16));
            return static_cast<uint32_t>(std::stoul(clean));
        };

        skip_ws();
        if (pos < raw.size() && raw[pos] == '{') pos++;

        while (pos < raw.size()) {
            skip_ws();
            if (pos >= raw.size()) break;
            if (raw[pos] == '}') { pos++; break; }
            if (raw[pos] == ',') { pos++; continue; }

            std::string key = read_str();
            if (key.empty()) { pos++; continue; }
            skip_ws();
            if (pos >= raw.size()) break;
            if (raw[pos] != ':') { pos++; continue; }
            pos++; skip_ws();
            if (pos >= raw.size()) break;

            if (raw[pos] == '{') {
                current_class = key;
                pos++;
                int depth = 1;
                while (depth > 0 && pos < raw.size()) {
                    skip_ws();
                    if (pos >= raw.size()) break;
                    if (raw[pos] == '{') { depth++; pos++; continue; }
                    if (raw[pos] == '}') { depth--; pos++; continue; }
                    if (raw[pos] == ',') { pos++; continue; }
                    if (raw[pos] == '"') {
                        std::string fk = read_str();
                        if (fk.empty()) { pos++; continue; }
                        if (pos < raw.size() && raw[pos] == ':') {
                            pos++; skip_ws();
                            std::string fv = read_val();
                            if (!fv.empty()) {
                                uint32_t val = parse_hex(fv);
                                if (!current_class.empty())
                                    cache_[current_class + "::" + fk] = val;
                                cache_[fk] = val;
                            }
                            if (pos < raw.size() && raw[pos] == ',') pos++;
                        }
                    } else pos++;
                }
            } else if (raw[pos] == '[') {
                pos++; int arr_depth = 1;
                while (arr_depth > 0 && pos < raw.size()) {
                    if (raw[pos] == '[') arr_depth++;
                    else if (raw[pos] == ']') arr_depth--;
                    pos++;
                }
            } else {
                read_val();
            }
        }
    }

    mutable std::mutex mutex_;
    std::unordered_map<std::string, uint32_t> cache_;
    std::unordered_set<std::string> loaded_;
};

inline bool load_json(const std::string& path) {
    return JsonOffsetLoader::get().load(path);
}

inline bool load_all_json() {
    return JsonOffsetLoader::get().load_all();
}

} // namespace offsets
} // namespace sca
