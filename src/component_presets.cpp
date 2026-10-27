// COMPONENT AYAR SETLERI — JSON kalicilik. OCCT'siz, Qt'siz.
#include "model_maker/component_presets.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>

namespace mm {
namespace {

using json = nlohmann::json;

std::string toUtf8(const std::wstring& ws) {
    std::string out;
    out.reserve(ws.size());
    for (const wchar_t wc : ws) {
        const std::uint32_t cp = static_cast<std::uint32_t>(wc);
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

std::wstring fromUtf8(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::uint32_t cp = 0;
        int extra = 0;
        if (c < 0x80) {
            cp = c;
        } else if ((c >> 5) == 0x6) {
            cp = c & 0x1F;
            extra = 1;
        } else if ((c >> 4) == 0xE) {
            cp = c & 0x0F;
            extra = 2;
        } else if ((c >> 3) == 0x1E) {
            cp = c & 0x07;
            extra = 3;
        } else {
            ++i;
            continue;
        }
        ++i;
        for (int k = 0; k < extra && i < s.size(); ++k, ++i)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
        out.push_back(static_cast<wchar_t>(cp));
    }
    return out;
}

json paramToJson(const CompParam& p) {
    json j;
    j["name"] = toUtf8(p.name);
    if (const auto* d = std::get_if<double>(&p.value)) {
        j["type"] = "double";
        j["value"] = *d;
    } else if (const auto* n = std::get_if<int>(&p.value)) {
        j["type"] = "int";
        j["value"] = *n;
    } else if (const auto* b = std::get_if<bool>(&p.value)) {
        j["type"] = "bool";
        j["value"] = *b;
    } else if (const auto* s = std::get_if<std::wstring>(&p.value)) {
        j["type"] = "string";
        j["value"] = toUtf8(*s);
    } else if (const auto* e = std::get_if<EntityId>(&p.value)) {
        j["type"] = "entity";
        j["value"] = static_cast<std::uint64_t>(*e);
    }
    return j;
}

bool paramFromJson(const json& j, CompParam& out) {
    if (!j.is_object() || !j.contains("name") || !j.contains("type") || !j.contains("value"))
        return false;
    out.name = fromUtf8(j["name"].get<std::string>());
    if (out.name.empty()) return false;
    const std::string type = j["type"].get<std::string>();
    const json& v = j["value"];
    if (type == "double" && v.is_number()) out.value = v.get<double>();
    else if (type == "int" && v.is_number_integer()) out.value = v.get<int>();
    else if (type == "bool" && v.is_boolean()) out.value = v.get<bool>();
    else if (type == "string" && v.is_string()) out.value = fromUtf8(v.get<std::string>());
    else if (type == "entity" && v.is_number_unsigned())
        out.value = static_cast<EntityId>(v.get<std::uint64_t>());
    else return false;
    return true;
}

} // namespace

bool ComponentPresetStore::validName(const std::wstring& name) noexcept {
    const std::wstring t = trimmed(name);
    if (t.empty() || t.size() > 64) return false;
    return std::none_of(t.begin(), t.end(), [](wchar_t c) { return c < 0x20 || c == 0x7F; });
}

std::wstring ComponentPresetStore::trimmed(const std::wstring& name) {
    const auto isSpace = [](wchar_t c) { return c == L' ' || c == L'\t'; };
    std::size_t a = 0, b = name.size();
    while (a < b && isSpace(name[a])) ++a;
    while (b > a && isSpace(name[b - 1])) --b;
    return name.substr(a, b - a);
}

const ComponentPreset* ComponentPresetStore::find(const std::wstring& typeId,
                                                  const std::wstring& name) const noexcept {
    for (const auto& p : presets_)
        if (p.typeId == typeId && p.name == name) return &p;
    return nullptr;
}

std::vector<std::wstring> ComponentPresetStore::names(const std::wstring& typeId) const {
    std::vector<std::wstring> out;
    for (const auto& p : presets_)
        if (p.typeId == typeId && p.name != kStandardPresetName) out.push_back(p.name);
    std::sort(out.begin(), out.end());
    out.insert(out.begin(), kStandardPresetName);
    return out;
}

bool ComponentPresetStore::put(const std::wstring& typeId, const std::wstring& name,
                               const std::vector<CompParam>& params) {
    const std::wstring n = trimmed(name);
    if (typeId.empty() || !validName(n)) return false;
    for (auto& p : presets_) {
        if (p.typeId == typeId && p.name == n) {
            p.params = params;
            return false;
        }
    }
    presets_.push_back(ComponentPreset{typeId, n, params});
    return true;
}

bool ComponentPresetStore::remove(const std::wstring& typeId, const std::wstring& name) {
    for (auto it = presets_.begin(); it != presets_.end(); ++it) {
        if (it->typeId == typeId && it->name == name) {
            presets_.erase(it);
            return true;
        }
    }
    return false;
}

bool ComponentPresetStore::load(const std::filesystem::path& path, std::string& error) noexcept {
    presets_.clear();
    error.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return true;
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    if (text.empty()) return true;
    try {
        const json root = json::parse(text);
        if (!root.is_object() || !root.contains("presets") || !root["presets"].is_array()) {
            error = "ayar seti JSON kok 'presets' dizisi icermiyor";
            return false;
        }
        for (const auto& jp : root["presets"]) {
            if (!jp.is_object()) continue;
            ComponentPreset p;
            p.typeId = fromUtf8(jp.value("typeId", ""));
            p.name = fromUtf8(jp.value("name", ""));
            if (p.typeId.empty() || !validName(p.name)) continue;
            if (jp.contains("params") && jp["params"].is_array()) {
                for (const auto& jv : jp["params"]) {
                    CompParam cp;
                    if (paramFromJson(jv, cp)) p.params.push_back(std::move(cp));
                }
            }
            presets_.push_back(std::move(p));
        }
        return true;
    } catch (const std::exception& ex) {
        error = std::string("ayar seti JSON parse hatasi: ") + ex.what();
        presets_.clear();
        return false;
    }
}

bool ComponentPresetStore::save(const std::filesystem::path& path, std::string& error) const noexcept {
    error.clear();
    try {
        json root;
        root["schemaVersion"] = 1;
        json arr = json::array();
        for (const auto& p : presets_) {
            json jp;
            jp["typeId"] = toUtf8(p.typeId);
            jp["name"] = toUtf8(p.name);
            json params = json::array();
            for (const auto& cp : p.params) params.push_back(paramToJson(cp));
            jp["params"] = std::move(params);
            arr.push_back(std::move(jp));
        }
        root["presets"] = std::move(arr);
        std::filesystem::path tmp = path;
        tmp += ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) {
                error = "ayar seti dosyasi acilamadi: " + tmp.string();
                return false;
            }
            out << root.dump(2) << "\n";
            out.flush();
            if (!out.good()) {
                error = "ayar seti yazilamadi: " + tmp.string();
                return false;
            }
        }
        std::error_code ec;
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            // Bazi sistemlerde var olan dosyanin ustune rename basarisiz olabilir.
            std::filesystem::remove(path, ec);
            ec.clear();
            std::filesystem::rename(tmp, path, ec);
            if (ec) {
                error = "ayar seti yeniden adlandirilamadi: " + ec.message();
                return false;
            }
        }
        return true;
    } catch (const std::exception& ex) {
        error = std::string("ayar seti yazma hatasi: ") + ex.what();
        return false;
    }
}

} // namespace mm
