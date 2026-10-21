// KULLANICI COMPONENT KATALOGU — JSON kalicilik + registry/dialog kayit mantigi.
// OCCT'siz; Linux OCC-off ctest'te kosar. Bagimlilik: nlohmann/json + component_engine.

#include "model_maker/user_catalog.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <fstream>
#include <sstream>

namespace mm {
namespace {

using json = nlohmann::json;

// --- UTF-8 <-> wchar_t (BMP/ASCII; mmw8_codec'teki ile ayni algoritma) --------
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
            ++i;  // gecersiz bayt: atla
            continue;
        }
        if (i + static_cast<std::size_t>(extra) >= s.size()) break;
        for (int k = 1; k <= extra; ++k) {
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0x3F);
        }
        out.push_back(static_cast<wchar_t>(cp));
        i += static_cast<std::size_t>(extra) + 1;
    }
    return out;
}

// --- CompParamType <-> string ------------------------------------------------
const char* paramTypeName(CompParamType t) noexcept {
    switch (t) {
    case CompParamType::Length: return "Length";
    case CompParamType::Angle: return "Angle";
    case CompParamType::Integer: return "Integer";
    case CompParamType::Boolean: return "Boolean";
    case CompParamType::Enum: return "Enum";
    case CompParamType::String: return "String";
    case CompParamType::Profile: return "Profile";
    case CompParamType::Material: return "Material";
    case CompParamType::EntityRef: return "EntityRef";
    }
    return "String";
}

CompParamType paramTypeFromName(const std::string& n) noexcept {
    if (n == "Length") return CompParamType::Length;
    if (n == "Angle") return CompParamType::Angle;
    if (n == "Integer") return CompParamType::Integer;
    if (n == "Boolean") return CompParamType::Boolean;
    if (n == "Enum") return CompParamType::Enum;
    if (n == "Profile") return CompParamType::Profile;
    if (n == "Material") return CompParamType::Material;
    if (n == "EntityRef") return CompParamType::EntityRef;
    return CompParamType::String;
}

// --- Spec <-> JSON -----------------------------------------------------------
json specToJson(const CompParamSpec& s) {
    json j;
    j["name"] = toUtf8(s.name);
    j["type"] = paramTypeName(s.type);
    if (!s.unit.empty()) j["unit"] = toUtf8(s.unit);
    if (!s.description.empty()) j["description"] = toUtf8(s.description);
    if (s.type == CompParamType::Length || s.type == CompParamType::Angle) {
        j["defDouble"] = s.defDouble;
        if (s.hasMinDouble) { j["minDouble"] = s.minDouble; }
        if (s.hasMaxDouble) { j["maxDouble"] = s.maxDouble; }
        j["hasMinDouble"] = s.hasMinDouble;
        j["hasMaxDouble"] = s.hasMaxDouble;
    }
    if (s.type == CompParamType::Integer) {
        j["defInt"] = s.defInt;
        if (s.hasMinInt) j["minInt"] = s.minInt;
        if (s.hasMaxInt) j["maxInt"] = s.maxInt;
        j["hasMinInt"] = s.hasMinInt;
        j["hasMaxInt"] = s.hasMaxInt;
    }
    if (s.type == CompParamType::Boolean) j["defBool"] = s.defBool;
    if (s.type == CompParamType::Enum || s.type == CompParamType::String ||
        s.type == CompParamType::Profile || s.type == CompParamType::Material) {
        if (!s.defString.empty()) j["defString"] = toUtf8(s.defString);
    }
    if (s.type == CompParamType::Enum) {
        json c = json::array();
        for (const auto& ch : s.choices) c.push_back(toUtf8(ch));
        j["choices"] = c;
    }
    return j;
}

CompParamSpec specFromJson(const json& j) {
    CompParamSpec s;
    s.name = fromUtf8(j.value("name", ""));
    s.type = paramTypeFromName(j.value("type", "String"));
    s.unit = fromUtf8(j.value("unit", ""));
    s.description = fromUtf8(j.value("description", ""));
    if (j.contains("defDouble")) s.defDouble = j["defDouble"].get<double>();
    if (j.contains("minDouble")) s.minDouble = j["minDouble"].get<double>();
    if (j.contains("maxDouble")) s.maxDouble = j["maxDouble"].get<double>();
    s.hasMinDouble = j.value("hasMinDouble", false);
    s.hasMaxDouble = j.value("hasMaxDouble", false);
    if (j.contains("defInt")) s.defInt = j["defInt"].get<int>();
    if (j.contains("minInt")) s.minInt = j["minInt"].get<int>();
    if (j.contains("maxInt")) s.maxInt = j["maxInt"].get<int>();
    s.hasMinInt = j.value("hasMinInt", false);
    s.hasMaxInt = j.value("hasMaxInt", false);
    if (j.contains("defBool")) s.defBool = j["defBool"].get<bool>();
    s.defString = fromUtf8(j.value("defString", ""));
    if (j.contains("choices") && j["choices"].is_array()) {
        for (const auto& c : j["choices"]) s.choices.push_back(fromUtf8(c.get<std::string>()));
    }
    return s;
}

// --- FNV-1a 64-bit (typeId icin kararli hash) --------------------------------
std::uint64_t fnv1a64(const std::wstring& ws) {
    const std::string s = toUtf8(ws);
    std::uint64_t h = 1469598103934665603ULL;
    for (const unsigned char c : s) {
        h ^= static_cast<std::uint64_t>(c);
        h *= 1099511628211ULL;
    }
    return h;
}

std::wstring hex8(std::uint64_t v) {
    static const wchar_t* digits = L"0123456789abcdef";
    std::wstring out(8, L'0');
    for (int i = 7; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = digits[v & 0xF];
        v >>= 4;
    }
    return out;
}

} // namespace

// --- entry -> ComponentType ---------------------------------------------------

ComponentType userCatalogEntryToComponentType(const UserCatalogEntry& e) noexcept {
    ComponentType t;
    t.typeId = e.typeId;
    t.schemaVersion = e.schemaVersion;
    t.displayName = e.displayName;
    t.params = e.specs;
    t.catalog.sourceComponentNumber = e.sourceComponentNumber;
    t.catalog.category = e.category;
    t.catalog.primaryMemberType = e.primaryMemberType;
    t.catalog.secondaryMemberType = e.secondaryMemberType;
    t.catalog.supportedFeatures = e.supportedFeatures;
    t.catalog.coverageStatus = e.coverageStatus;
    t.catalog.pickCount = e.pickCount > 0 ? e.pickCount : 2;
    t.catalog.catalogVisible = true;
    return t;
}

// --- derive ------------------------------------------------------------------

UserCatalogEntry deriveComponentTemplate(const ComponentType& source,
                                         const std::wstring& displayName,
                                         const std::vector<CompParam>& defaultOverrides,
                                         bool isReferenceOnly,
                                         const std::wstring& referenceNote) {
    UserCatalogEntry e;
    e.displayName = displayName.empty() ? source.displayName : displayName;
    e.sourceTypeId = source.typeId;
    e.generatorFamily = source.typeId;
    e.sourceComponentNumber = source.catalog.sourceComponentNumber;
    e.schemaVersion = source.schemaVersion;
    e.specs = source.params;
    e.category = source.catalog.category;
    e.primaryMemberType = source.catalog.primaryMemberType;
    e.secondaryMemberType = source.catalog.secondaryMemberType;
    e.supportedFeatures = source.catalog.supportedFeatures;
    e.coverageStatus = source.catalog.coverageStatus;
    e.pickCount = source.catalog.pickCount;
    e.isReferenceOnly = isReferenceOnly;
    e.referenceNote = referenceNote;

    // Varsayilanlari override'larla ez (yalnizca semada VAR olan adlar).
    for (const CompParam& p : defaultOverrides) {
        for (CompParamSpec& s : e.specs) {
            if (s.name != p.name) continue;
            if (const auto* d = std::get_if<double>(&p.value)) {
                if (s.type == CompParamType::Length || s.type == CompParamType::Angle)
                    s.defDouble = *d;
            } else if (const auto* i = std::get_if<int>(&p.value)) {
                if (s.type == CompParamType::Integer) s.defInt = *i;
            } else if (const auto* b = std::get_if<bool>(&p.value)) {
                if (s.type == CompParamType::Boolean) s.defBool = *b;
            } else if (const auto* str = std::get_if<std::wstring>(&p.value)) {
                if (s.type == CompParamType::Enum || s.type == CompParamType::String ||
                    s.type == CompParamType::Profile || s.type == CompParamType::Material)
                    s.defString = *str;
            }
            break;
        }
    }

    // Benzersiz + kararli typeId. Cagiran (UserCatalog) cakismayi cözer; burada
    // deterministik taban uretilir.
    const std::wstring seed = e.sourceTypeId + L"|" + e.displayName;
    e.typeId = L"user_" + hex8(fnv1a64(seed));
    return e;
}

// --- UserCatalog -------------------------------------------------------------

const UserCatalogEntry* UserCatalog::find(const std::wstring& typeId) const noexcept {
    for (const auto& e : entries_)
        if (e.typeId == typeId) return &e;
    return nullptr;
}

bool UserCatalog::addOrUpdate(const UserCatalogEntry& e) noexcept {
    if (e.typeId.empty()) return false;
    for (auto& cur : entries_) {
        if (cur.typeId == e.typeId) {
            cur = e;
            return false;
        }
    }
    entries_.push_back(e);
    return true;
}

bool UserCatalog::remove(const std::wstring& typeId) noexcept {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->typeId == typeId) {
            entries_.erase(it);
            return true;
        }
    }
    return false;
}

void UserCatalog::ensureUnique(UserCatalogEntry& e) const noexcept {
    const auto collides = [&](const std::wstring& id) {
        for (const auto& cur : entries_)
            if (cur.typeId == id && cur.displayName != e.displayName) return true;
        return false;
    };
    if (!collides(e.typeId)) return;
    const std::wstring base = e.typeId;
    for (int k = 2;; ++k) {
        const std::wstring cand = base + L"_" + std::to_wstring(k);
        if (!collides(cand)) {
            e.typeId = cand;
            return;
        }
    }
}

bool UserCatalog::load(const std::filesystem::path& path, std::string& error) noexcept {
    entries_.clear();
    error.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return true;  // dosya yok => bos katalog (ilk calistirma)
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    if (text.empty()) return true;
    try {
        const json root = json::parse(text);
        if (!root.is_object() || !root.contains("entries") || !root["entries"].is_array()) {
            error = "katalog JSON kok 'entries' dizisi icermiyor";
            return false;
        }
        for (const auto& je : root["entries"]) {
            if (!je.is_object()) continue;
            UserCatalogEntry e;
            e.typeId = fromUtf8(je.value("typeId", ""));
            if (e.typeId.empty()) continue;
            e.displayName = fromUtf8(je.value("displayName", ""));
            e.sourceTypeId = fromUtf8(je.value("sourceTypeId", ""));
            e.generatorFamily = fromUtf8(je.value("generatorFamily", toUtf8(e.sourceTypeId)));
            e.sourceComponentNumber = fromUtf8(je.value("sourceComponentNumber", ""));
            e.schemaVersion = je.value("schemaVersion", 1);
            e.category = fromUtf8(je.value("category", ""));
            e.primaryMemberType = fromUtf8(je.value("primaryMemberType", ""));
            e.secondaryMemberType = fromUtf8(je.value("secondaryMemberType", ""));
            e.pickCount = je.value("pickCount", 2);
            e.isReferenceOnly = je.value("isReferenceOnly", false);
            e.referenceNote = fromUtf8(je.value("referenceNote", ""));
            if (je.contains("supportedFeatures") && je["supportedFeatures"].is_array()) {
                for (const auto& f : je["supportedFeatures"])
                    e.supportedFeatures.push_back(fromUtf8(f.get<std::string>()));
            }
            e.coverageStatus = fromUtf8(je.value("coverageStatus", ""));
            if (je.contains("specs") && je["specs"].is_array()) {
                for (const auto& js : je["specs"]) e.specs.push_back(specFromJson(js));
            }
            entries_.push_back(std::move(e));
        }
        return true;
    } catch (const std::exception& ex) {
        error = std::string("katalog JSON parse hatasi: ") + ex.what();
        entries_.clear();
        return false;
    }
}

bool UserCatalog::save(const std::filesystem::path& path, std::string& error) const noexcept {
    error.clear();
    json root;
    root["schemaVersion"] = 1;
    json arr = json::array();
    for (const auto& e : entries_) {
        json je;
        je["typeId"] = toUtf8(e.typeId);
        je["displayName"] = toUtf8(e.displayName);
        je["sourceTypeId"] = toUtf8(e.sourceTypeId);
        je["generatorFamily"] = toUtf8(e.generatorFamily);
        if (!e.sourceComponentNumber.empty()) je["sourceComponentNumber"] = toUtf8(e.sourceComponentNumber);
        je["schemaVersion"] = e.schemaVersion;
        if (!e.category.empty()) je["category"] = toUtf8(e.category);
        if (!e.primaryMemberType.empty()) je["primaryMemberType"] = toUtf8(e.primaryMemberType);
        if (!e.secondaryMemberType.empty()) je["secondaryMemberType"] = toUtf8(e.secondaryMemberType);
        je["pickCount"] = e.pickCount;
        if (!e.supportedFeatures.empty()) {
            json sf = json::array();
            for (const auto& f : e.supportedFeatures) sf.push_back(toUtf8(f));
            je["supportedFeatures"] = sf;
        }
        if (!e.coverageStatus.empty()) je["coverageStatus"] = toUtf8(e.coverageStatus);
        je["isReferenceOnly"] = e.isReferenceOnly;
        if (!e.referenceNote.empty()) je["referenceNote"] = toUtf8(e.referenceNote);
        json specs = json::array();
        for (const auto& s : e.specs) specs.push_back(specToJson(s));
        je["specs"] = specs;
        arr.push_back(std::move(je));
    }
    root["entries"] = std::move(arr);
    try {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            error = "katalog dosyasi acilamadi: " + path.string();
            return false;
        }
        out << root.dump(2) << "\n";
        out.flush();
        return out.good();
    } catch (const std::exception& ex) {
        error = std::string("katalog yazma hatasi: ") + ex.what();
        return false;
    }
}

std::vector<std::wstring> UserCatalog::registerInto(ComponentRegistry& reg,
                                                    ComponentDialogRegistry& dialogs) const noexcept {
    std::vector<std::wstring> warnings;
    for (const auto& e : entries_) {
        if (e.typeId.empty()) continue;
        const ComponentGenerator* gen = reg.generator(e.sourceTypeId);
        if (!gen) {
            warnings.push_back(e.displayName + L" (family '" + e.sourceTypeId +
                               L"' kayitli degil; sablon atlandi)");
            continue;
        }
        const ComponentType t = userCatalogEntryToComponentType(e);
        reg.registerType(t, *gen);
        // Dialog: family dialogunun semasini + sablonun specs'ini derived typeId ile kaydet.
        const ComponentDialogDefinition* src = dialogs.find(e.sourceTypeId);
        if (src) {
            ComponentDialogDefinition d;
            d.typeId = e.typeId;
            d.specs = e.specs;
            d.schema = src->schema;
            d.schema.typeId = e.typeId;
            dialogs.registerDialog(d);
        }
    }
    return warnings;
}

std::vector<std::wstring> UserCatalog::missingTypeIds(
    const std::vector<std::wstring>& typeIds) const noexcept {
    std::vector<std::wstring> missing;
    for (const auto& id : typeIds) {
        if (!find(id)) missing.push_back(id);
    }
    return missing;
}

} // namespace mm
