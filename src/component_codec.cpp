#include "model_maker/component_codec.hpp"

#include "model_maker/sha256.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace mm {

using json = nlohmann::json;

// ============================================================================
// UTF-8 (wchar_t 16/32-bit portable)
// ============================================================================

namespace {

void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7Fu) {
        out += static_cast<char>(cp);
    } else if (cp <= 0x7FFu) {
        out += static_cast<char>(0xC0u | (cp >> 6));
        out += static_cast<char>(0x80u | (cp & 0x3Fu));
    } else if (cp <= 0xFFFFu) {
        out += static_cast<char>(0xE0u | (cp >> 12));
        out += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
        out += static_cast<char>(0x80u | (cp & 0x3Fu));
    } else {
        out += static_cast<char>(0xF0u | (cp >> 18));
        out += static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu));
        out += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
        out += static_cast<char>(0x80u | (cp & 0x3Fu));
    }
}

} // namespace

std::string utf8FromWide(const std::wstring& w) {
    std::string out;
    out.reserve(w.size() * 2);
    if constexpr (sizeof(wchar_t) >= 4) {
        for (wchar_t ch : w) appendUtf8(out, static_cast<std::uint32_t>(ch));
    } else {
        std::size_t i = 0;
        while (i < w.size()) {
            std::uint32_t cp = static_cast<std::uint16_t>(w[i]);
            if (cp >= 0xD800u && cp <= 0xDBFFu && i + 1 < w.size()) {
                const std::uint32_t lo = static_cast<std::uint16_t>(w[i + 1]);
                if (lo >= 0xDC00u && lo <= 0xDFFFu) {
                    cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                    ++i;
                }
            }
            appendUtf8(out, cp);
            ++i;
        }
    }
    return out;
}

std::wstring wideFromUtf8(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    auto appendCp = [&out](std::uint32_t cp) {
        if constexpr (sizeof(wchar_t) >= 4) {
            out += static_cast<wchar_t>(cp);
        } else {
            if (cp <= 0xFFFFu) {
                out += static_cast<wchar_t>(cp);
            } else {
                cp -= 0x10000u;
                out += static_cast<wchar_t>(0xD800u + (cp >> 10));
                out += static_cast<wchar_t>(0xDC00u + (cp & 0x3FFu));
            }
        }
    };
    std::size_t i = 0;
    while (i < s.size()) {
        const std::uint8_t c = static_cast<std::uint8_t>(s[i]);
        if (c <= 0x7Fu) {
            appendCp(c);
            ++i;
        } else if ((c & 0xE0u) == 0xC0u && i + 1 < s.size()) {
            const std::uint32_t cp = ((c & 0x1Fu) << 6) |
                                     (static_cast<std::uint8_t>(s[i + 1]) & 0x3Fu);
            appendCp(cp);
            i += 2;
        } else if ((c & 0xF0u) == 0xE0u && i + 2 < s.size()) {
            const std::uint32_t cp = ((c & 0x0Fu) << 12) |
                                     ((static_cast<std::uint8_t>(s[i + 1]) & 0x3Fu) << 6) |
                                     (static_cast<std::uint8_t>(s[i + 2]) & 0x3Fu);
            appendCp(cp);
            i += 3;
        } else if ((c & 0xF8u) == 0xF0u && i + 3 < s.size()) {
            const std::uint32_t cp = ((c & 0x07u) << 18) |
                                     ((static_cast<std::uint8_t>(s[i + 1]) & 0x3Fu) << 12) |
                                     ((static_cast<std::uint8_t>(s[i + 2]) & 0x3Fu) << 6) |
                                     (static_cast<std::uint8_t>(s[i + 3]) & 0x3Fu);
            appendCp(cp);
            i += 4;
        } else {
            appendCp(0xFFFDu); // geçersiz bayt -> yer tutucu
            ++i;
        }
    }
    return out;
}

// ============================================================================
// Vec3 / EntityId / temel yardimcilar
// ============================================================================

namespace {

json vec3ToJson(const Vec3& v) { return json::array({v.x, v.y, v.z}); }

Vec3 vec3FromJson(const json& j, const Vec3& def = {}) {
    Vec3 v = def;
    if (j.is_array() && j.size() >= 3) {
        v.x = j[0].is_number() ? j[0].get<double>() : def.x;
        v.y = j[1].is_number() ? j[1].get<double>() : def.y;
        v.z = j[2].is_number() ? j[2].get<double>() : def.z;
    }
    return v;
}

json entityToJson(EntityId id) { return static_cast<std::uint64_t>(id); }
EntityId entityFromJson(const json& j, EntityId def = kInvalidEntityId) {
    return j.is_number() ? static_cast<EntityId>(j.get<std::uint64_t>()) : def;
}

json wstrToJson(const std::wstring& w) { return utf8FromWide(w); }
std::wstring wstrFromJson(const json& j, const std::wstring& def = {}) {
    return j.is_string() ? wideFromUtf8(j.get<std::string>()) : def;
}

json pointToJson(const DiagramPoint& p) {
    return json{{"x", p.x}, {"y", p.y}};
}
DiagramPoint pointFromJson(const json& j) {
    DiagramPoint p;
    p.x = j.value("x", 0.0);
    p.y = j.value("y", 0.0);
    return p;
}

// --- enum <-> string ---------------------------------------------------------

const char* compParamTypeName(CompParamType t) {
    switch (t) {
        case CompParamType::Length:    return "Length";
        case CompParamType::Angle:     return "Angle";
        case CompParamType::Integer:   return "Integer";
        case CompParamType::Boolean:   return "Boolean";
        case CompParamType::Enum:      return "Enum";
        case CompParamType::String:    return "String";
        case CompParamType::Profile:   return "Profile";
        case CompParamType::Material:  return "Material";
        case CompParamType::EntityRef: return "EntityRef";
    }
    return "Length";
}
CompParamType compParamTypeFromName(const std::string& s) {
    if (s == "Angle")     return CompParamType::Angle;
    if (s == "Integer")   return CompParamType::Integer;
    if (s == "Boolean")   return CompParamType::Boolean;
    if (s == "Enum")      return CompParamType::Enum;
    if (s == "String")    return CompParamType::String;
    if (s == "Profile")   return CompParamType::Profile;
    if (s == "Material")  return CompParamType::Material;
    if (s == "EntityRef") return CompParamType::EntityRef;
    return CompParamType::Length;
}

const char* cutKindName(CutKind k) {
    switch (k) {
        case CutKind::PlaneCut:   return "PlaneCut";
        case CutKind::Fitting:    return "Fitting";
        case CutKind::PolygonCut: return "PolygonCut";
        case CutKind::BooleanCut: return "BooleanCut";
        case CutKind::LineCut:    return "LineCut";
        case CutKind::Hole:       return "Hole";
    }
    return "PlaneCut";
}
CutKind cutKindFromName(const std::string& s) {
    if (s == "Fitting")    return CutKind::Fitting;
    if (s == "PolygonCut") return CutKind::PolygonCut;
    if (s == "BooleanCut") return CutKind::BooleanCut;
    if (s == "LineCut")    return CutKind::LineCut;
    if (s == "Hole")       return CutKind::Hole;
    return CutKind::PlaneCut;
}

const char* primitiveKindName(DiagramPrimitiveKind k) {
    switch (k) {
        case DiagramPrimitiveKind::Rect:     return "Rect";
        case DiagramPrimitiveKind::Line:     return "Line";
        case DiagramPrimitiveKind::Circle:   return "Circle";
        case DiagramPrimitiveKind::Text:     return "Text";
        case DiagramPrimitiveKind::Polyline: return "Polyline";
    }
    return "Line";
}
DiagramPrimitiveKind primitiveKindFromName(const std::string& s) {
    if (s == "Rect")     return DiagramPrimitiveKind::Rect;
    if (s == "Circle")   return DiagramPrimitiveKind::Circle;
    if (s == "Text")     return DiagramPrimitiveKind::Text;
    if (s == "Polyline") return DiagramPrimitiveKind::Polyline;
    return DiagramPrimitiveKind::Line;
}

const char* weldRowTypeName(WeldRowType t) {
    return t == WeldRowType::Fillet ? "Fillet" : "Butt";
}
WeldRowType weldRowTypeFromName(const std::string& s) {
    return s == "Butt" ? WeldRowType::Butt : WeldRowType::Fillet;
}

const char* placedControlKindName(PlacedControlKind k) {
    switch (k) {
        case PlacedControlKind::Edit:         return "Edit";
        case PlacedControlKind::Combo:        return "Combo";
        case PlacedControlKind::IconCombo:    return "IconCombo";
        case PlacedControlKind::Check:        return "Check";
        case PlacedControlKind::DefaultCheck: return "DefaultCheck";
    }
    return "Edit";
}
PlacedControlKind placedControlKindFromName(const std::string& s) {
    if (s == "Combo")        return PlacedControlKind::Combo;
    if (s == "IconCombo")    return PlacedControlKind::IconCombo;
    if (s == "Check")        return PlacedControlKind::Check;
    if (s == "DefaultCheck") return PlacedControlKind::DefaultCheck;
    return PlacedControlKind::Edit;
}

} // namespace

// ============================================================================
// CompParamSpec
// ============================================================================

namespace {

json compParamSpecToJsonImpl(const CompParamSpec& s) {
    json j;
    j["name"] = utf8FromWide(s.name);
    j["type"] = compParamTypeName(s.type);
    j["unit"] = utf8FromWide(s.unit);
    j["description"] = utf8FromWide(s.description);
    j["defDouble"] = s.defDouble;
    j["minDouble"] = s.minDouble;
    j["maxDouble"] = s.maxDouble;
    j["hasMinDouble"] = s.hasMinDouble;
    j["hasMaxDouble"] = s.hasMaxDouble;
    j["defInt"] = s.defInt;
    j["minInt"] = s.minInt;
    j["maxInt"] = s.maxInt;
    j["hasMinInt"] = s.hasMinInt;
    j["hasMaxInt"] = s.hasMaxInt;
    j["defBool"] = s.defBool;
    j["defString"] = utf8FromWide(s.defString);
    json choices = json::array();
    for (const auto& c : s.choices) choices.push_back(utf8FromWide(c));
    j["choices"] = std::move(choices);
    j["optional"] = s.optional;
    j["documentedOnly"] = s.documentedOnly;
    return j;
}

CompParamSpec compParamSpecFromJsonImpl(const json& j) {
    CompParamSpec s;
    s.name = wideFromUtf8(j.value("name", std::string()));
    s.type = compParamTypeFromName(j.value("type", std::string("Length")));
    s.unit = wideFromUtf8(j.value("unit", std::string()));
    s.description = wideFromUtf8(j.value("description", std::string()));
    s.defDouble = j.value("defDouble", 0.0);
    s.minDouble = j.value("minDouble", 0.0);
    s.maxDouble = j.value("maxDouble", 0.0);
    s.hasMinDouble = j.value("hasMinDouble", false);
    s.hasMaxDouble = j.value("hasMaxDouble", false);
    s.defInt = j.value("defInt", 0);
    s.minInt = j.value("minInt", 0);
    s.maxInt = j.value("maxInt", 0);
    s.hasMinInt = j.value("hasMinInt", false);
    s.hasMaxInt = j.value("hasMaxInt", false);
    s.defBool = j.value("defBool", false);
    s.defString = wideFromUtf8(j.value("defString", std::string()));
    s.choices.clear();
    if (j.contains("choices") && j["choices"].is_array()) {
        for (const auto& c : j["choices"]) s.choices.push_back(wideFromUtf8(c.get<std::string>()));
    }
    s.optional = j.value("optional", false);
    s.documentedOnly = j.value("documentedOnly", false);
    return s;
}

} // namespace

nlohmann::json compParamSpecToJson(const CompParamSpec& spec) {
    json j = compParamSpecToJsonImpl(spec);
    j["formatVersion"] = kCodecFormatVersion;
    return j;
}

CompParamSpec compParamSpecFromJson(const nlohmann::json& j) {
    return compParamSpecFromJsonImpl(j);
}

// ============================================================================
// CompParam (tipli deger)
// ============================================================================

namespace {

json compParamValueToJson(const CompParamValue& v) {
    json o;
    if (std::holds_alternative<double>(v)) {
        o["type"] = "double";
        o["value"] = std::get<double>(v);
    } else if (std::holds_alternative<int>(v)) {
        o["type"] = "int";
        o["value"] = std::get<int>(v);
    } else if (std::holds_alternative<bool>(v)) {
        o["type"] = "bool";
        o["value"] = std::get<bool>(v);
    } else if (std::holds_alternative<std::wstring>(v)) {
        o["type"] = "string";
        o["value"] = utf8FromWide(std::get<std::wstring>(v));
    } else {
        o["type"] = "entity";
        o["value"] = static_cast<std::uint64_t>(std::get<EntityId>(v));
    }
    return o;
}

CompParamValue compParamValueFromJson(const json& j) {
    const std::string t = j.value("type", std::string("double"));
    if (t == "int")    return j.value("value", 0);
    if (t == "bool")   return static_cast<bool>(j.value("value", false));
    if (t == "string") return wideFromUtf8(j.value("value", std::string()));
    if (t == "entity") return static_cast<EntityId>(j.value("value", std::uint64_t(0)));
    return j.value("value", 0.0); // double
}

json compParamToJsonImpl(const CompParam& p) {
    json j;
    j["name"] = utf8FromWide(p.name);
    j["value"] = compParamValueToJson(p.value);
    return j;
}

CompParam compParamFromJsonImpl(const json& j) {
    CompParam p;
    p.name = wideFromUtf8(j.value("name", std::string()));
    p.value = compParamValueFromJson(j.value("value", json::object()));
    return p;
}

} // namespace

nlohmann::json compParamToJson(const CompParam& p) {
    json j = compParamToJsonImpl(p);
    j["formatVersion"] = kCodecFormatVersion;
    return j;
}

CompParam compParamFromJson(const nlohmann::json& j) {
    return compParamFromJsonImpl(j);
}

// ============================================================================
// ComponentCatalogMeta + ComponentType
// ============================================================================

namespace {

json catalogMetaToJsonImpl(const ComponentCatalogMeta& m) {
    json j;
    j["sourceComponentNumber"] = utf8FromWide(m.sourceComponentNumber);
    j["category"] = utf8FromWide(m.category);
    j["primaryMemberType"] = utf8FromWide(m.primaryMemberType);
    j["secondaryMemberType"] = utf8FromWide(m.secondaryMemberType);
    json feats = json::array();
    for (const auto& f : m.supportedFeatures) feats.push_back(utf8FromWide(f));
    j["supportedFeatures"] = std::move(feats);
    j["coverageStatus"] = utf8FromWide(m.coverageStatus);
    j["pickCount"] = m.pickCount;
    j["catalogVisible"] = m.catalogVisible;
    return j;
}

ComponentCatalogMeta catalogMetaFromJsonImpl(const json& j) {
    ComponentCatalogMeta m;
    m.sourceComponentNumber = wideFromUtf8(j.value("sourceComponentNumber", std::string()));
    m.category = wideFromUtf8(j.value("category", std::string()));
    m.primaryMemberType = wideFromUtf8(j.value("primaryMemberType", std::string()));
    m.secondaryMemberType = wideFromUtf8(j.value("secondaryMemberType", std::string()));
    if (j.contains("supportedFeatures") && j["supportedFeatures"].is_array()) {
        for (const auto& f : j["supportedFeatures"])
            m.supportedFeatures.push_back(wideFromUtf8(f.get<std::string>()));
    }
    m.coverageStatus = wideFromUtf8(j.value("coverageStatus", std::string()));
    m.pickCount = j.value("pickCount", 2);
    m.catalogVisible = j.value("catalogVisible", false);
    return m;
}

json componentTypeToJsonImpl(const ComponentType& t) {
    json j;
    j["typeId"] = utf8FromWide(t.typeId);
    j["schemaVersion"] = t.schemaVersion;
    j["displayName"] = utf8FromWide(t.displayName);
    json params = json::array();
    for (const auto& s : t.params) params.push_back(compParamSpecToJsonImpl(s));
    j["params"] = std::move(params);
    j["catalog"] = catalogMetaToJsonImpl(t.catalog);
    return j;
}

ComponentType componentTypeFromJsonImpl(const json& j) {
    ComponentType t;
    t.typeId = wideFromUtf8(j.value("typeId", std::string()));
    t.schemaVersion = j.value("schemaVersion", 1);
    t.displayName = wideFromUtf8(j.value("displayName", std::string()));
    if (j.contains("params") && j["params"].is_array()) {
        for (const auto& s : j["params"]) t.params.push_back(compParamSpecFromJsonImpl(s));
    }
    if (j.contains("catalog") && j["catalog"].is_object())
        t.catalog = catalogMetaFromJsonImpl(j["catalog"]);
    return t;
}

} // namespace

nlohmann::json componentCatalogMetaToJson(const ComponentCatalogMeta& m) {
    json j = catalogMetaToJsonImpl(m);
    j["formatVersion"] = kCodecFormatVersion;
    return j;
}

ComponentCatalogMeta componentCatalogMetaFromJson(const nlohmann::json& j) {
    return catalogMetaFromJsonImpl(j);
}

nlohmann::json componentTypeToJson(const ComponentType& t) {
    json j = componentTypeToJsonImpl(t);
    j["formatVersion"] = kCodecFormatVersion;
    return j;
}

ComponentType componentTypeFromJson(const nlohmann::json& j) {
    return componentTypeFromJsonImpl(j);
}

// ============================================================================
// ComponentDialogSchema
// ============================================================================

namespace {

json primitiveToJson(const DiagramPrimitive& p) {
    json j;
    j["kind"] = primitiveKindName(p.kind);
    j["a"] = pointToJson(p.a);
    j["b"] = pointToJson(p.b);
    j["r"] = p.r;
    j["text"] = utf8FromWide(p.text);
    json pts = json::array();
    for (const auto& q : p.pts) pts.push_back(pointToJson(q));
    j["pts"] = std::move(pts);
    j["closed"] = p.closed;
    j["dashed"] = p.dashed;
    j["filled"] = p.filled;
    return j;
}
DiagramPrimitive primitiveFromJson(const json& j) {
    DiagramPrimitive p;
    p.kind = primitiveKindFromName(j.value("kind", std::string("Line")));
    p.a = pointFromJson(j.value("a", json::object()));
    p.b = pointFromJson(j.value("b", json::object()));
    p.r = j.value("r", 0.0);
    p.text = wideFromUtf8(j.value("text", std::string()));
    if (j.contains("pts") && j["pts"].is_array()) {
        for (const auto& q : j["pts"]) p.pts.push_back(pointFromJson(q));
    }
    p.closed = j.value("closed", false);
    p.dashed = j.value("dashed", false);
    p.filled = j.value("filled", false);
    return p;
}

json diagramItemToJson(const ComponentDiagramItem& it) {
    json j;
    j["id"] = utf8FromWide(it.id);
    json prims = json::array();
    for (const auto& p : it.primitives) prims.push_back(primitiveToJson(p));
    j["primitives"] = std::move(prims);
    return j;
}
ComponentDiagramItem diagramItemFromJson(const json& j) {
    ComponentDiagramItem it;
    it.id = wideFromUtf8(j.value("id", std::string()));
    if (j.contains("primitives") && j["primitives"].is_array()) {
        for (const auto& p : j["primitives"]) it.primitives.push_back(primitiveFromJson(p));
    }
    return it;
}

json hotspotToJson(const DiagramHotspot& h) {
    json j;
    j["id"] = utf8FromWide(h.id);
    j["position"] = pointToJson(h.position);
    j["radius"] = h.radius;
    if (h.paramName) j["paramName"] = utf8FromWide(*h.paramName);
    if (h.label)     j["label"] = utf8FromWide(*h.label);
    if (h.weldRow)   j["weldRow"] = *h.weldRow;
    return j;
}
DiagramHotspot hotspotFromJson(const json& j) {
    DiagramHotspot h;
    h.id = wideFromUtf8(j.value("id", std::string()));
    h.position = pointFromJson(j.value("position", json::object()));
    h.radius = j.value("radius", 8.0);
    if (j.contains("paramName") && j["paramName"].is_string())
        h.paramName = wideFromUtf8(j["paramName"].get<std::string>());
    if (j.contains("label") && j["label"].is_string())
        h.label = wideFromUtf8(j["label"].get<std::string>());
    if (j.contains("weldRow") && j["weldRow"].is_number_integer())
        h.weldRow = j["weldRow"].get<int>();
    return h;
}

json weldRowToJson(const WeldTableRow& r) {
    json j;
    j["enabled"] = r.enabled;
    j["number"] = r.number;
    j["size"] = r.size;
    j["type"] = weldRowTypeName(r.type);
    j["angle"] = r.angle;
    j["contour"] = utf8FromWide(r.contour);
    j["finish"] = utf8FromWide(r.finish);
    j["effThroat"] = r.effThroat;
    j["rootOpening"] = r.rootOpening;
    j["placement"] = utf8FromWide(r.placement);
    j["prep"] = utf8FromWide(r.prep);
    j["length"] = r.length;
    j["pitch"] = r.pitch;
    j["refText"] = utf8FromWide(r.refText);
    return j;
}
WeldTableRow weldRowFromJson(const json& j) {
    WeldTableRow r;
    r.enabled = j.value("enabled", true);
    r.number = j.value("number", 1);
    r.size = j.value("size", 5.0);
    r.type = weldRowTypeFromName(j.value("type", std::string("Fillet")));
    r.angle = j.value("angle", 45.0);
    r.contour = wideFromUtf8(j.value("contour", std::string("None")));
    r.finish = wideFromUtf8(j.value("finish", std::string("None")));
    r.effThroat = j.value("effThroat", 0.0);
    r.rootOpening = j.value("rootOpening", 0.0);
    r.placement = wideFromUtf8(j.value("placement", std::string("Auto")));
    r.prep = wideFromUtf8(j.value("prep", std::string("None")));
    r.length = j.value("length", 0.0);
    r.pitch = j.value("pitch", 0.0);
    r.refText = wideFromUtf8(j.value("refText", std::string()));
    return r;
}

json figureToJson(const DialogFigure& f) {
    json j;
    j["resource"] = utf8FromWide(f.resource);
    j["caption"] = utf8FromWide(f.caption);
    return j;
}
DialogFigure figureFromJson(const json& j) {
    DialogFigure f;
    f.resource = wideFromUtf8(j.value("resource", std::string()));
    f.caption = wideFromUtf8(j.value("caption", std::string()));
    return f;
}

json partRowToJson(const DialogPartRow& r) {
    json j;
    j["label"] = utf8FromWide(r.label);
    j["t"] = utf8FromWide(r.t);
    j["b"] = utf8FromWide(r.b);
    j["h"] = utf8FromWide(r.h);
    j["profile"] = utf8FromWide(r.profile);
    j["posPrefix"] = utf8FromWide(r.posPrefix);
    j["posStart"] = utf8FromWide(r.posStart);
    j["asmPrefix"] = utf8FromWide(r.asmPrefix);
    j["asmStart"] = utf8FromWide(r.asmStart);
    j["material"] = utf8FromWide(r.material);
    j["name"] = utf8FromWide(r.name);
    j["finish"] = utf8FromWide(r.finish);
    return j;
}
DialogPartRow partRowFromJson(const json& j) {
    DialogPartRow r;
    r.label = wideFromUtf8(j.value("label", std::string()));
    r.t = wideFromUtf8(j.value("t", std::string()));
    r.b = wideFromUtf8(j.value("b", std::string()));
    r.h = wideFromUtf8(j.value("h", std::string()));
    r.profile = wideFromUtf8(j.value("profile", std::string()));
    r.posPrefix = wideFromUtf8(j.value("posPrefix", std::string()));
    r.posStart = wideFromUtf8(j.value("posStart", std::string()));
    r.asmPrefix = wideFromUtf8(j.value("asmPrefix", std::string()));
    r.asmStart = wideFromUtf8(j.value("asmStart", std::string()));
    r.material = wideFromUtf8(j.value("material", std::string()));
    r.name = wideFromUtf8(j.value("name", std::string()));
    r.finish = wideFromUtf8(j.value("finish", std::string()));
    return r;
}

json placedControlToJson(const DialogPlacedControl& c) {
    json j;
    j["param"] = utf8FromWide(c.param);
    j["kind"] = placedControlKindName(c.kind);
    j["x"] = c.x;
    j["y"] = c.y;
    j["w"] = c.w;
    j["h"] = c.h;
    j["greyed"] = c.greyed;
    json gov = json::array();
    for (const auto& g : c.governs) gov.push_back(utf8FromWide(g));
    j["governs"] = std::move(gov);
    return j;
}
DialogPlacedControl placedControlFromJson(const json& j) {
    DialogPlacedControl c;
    c.param = wideFromUtf8(j.value("param", std::string()));
    c.kind = placedControlKindFromName(j.value("kind", std::string("Edit")));
    c.x = j.value("x", 0);
    c.y = j.value("y", 0);
    c.w = j.value("w", 0);
    c.h = j.value("h", 0);
    c.greyed = j.value("greyed", false);
    if (j.contains("governs") && j["governs"].is_array()) {
        for (const auto& g : j["governs"]) c.governs.push_back(wideFromUtf8(g.get<std::string>()));
    }
    return c;
}

json groupToJson(const ComponentDialogGroup& g) {
    json j;
    j["id"] = utf8FromWide(g.id);
    j["title"] = utf8FromWide(g.title);
    json names = json::array();
    for (const auto& n : g.paramNames) names.push_back(utf8FromWide(n));
    j["paramNames"] = std::move(names);
    j["asTab"] = g.asTab;
    json figs = json::array();
    for (const auto& f : g.figures) figs.push_back(figureToJson(f));
    j["figures"] = std::move(figs);
    json rows = json::array();
    for (const auto& r : g.partRows) rows.push_back(partRowToJson(r));
    j["partRows"] = std::move(rows);
    j["keepIfEmpty"] = g.keepIfEmpty;
    j["emptyNote"] = utf8FromWide(g.emptyNote);
    j["layout"] = utf8FromWide(g.layout);
    j["prefix"] = utf8FromWide(g.prefix);
    j["background"] = utf8FromWide(g.background);
    j["canvasWidth"] = g.canvasWidth;
    j["canvasHeight"] = g.canvasHeight;
    json placed = json::array();
    for (const auto& c : g.placed) placed.push_back(placedControlToJson(c));
    j["placed"] = std::move(placed);
    return j;
}
ComponentDialogGroup groupFromJson(const json& j) {
    ComponentDialogGroup g;
    g.id = wideFromUtf8(j.value("id", std::string()));
    g.title = wideFromUtf8(j.value("title", std::string()));
    if (j.contains("paramNames") && j["paramNames"].is_array()) {
        for (const auto& n : j["paramNames"]) g.paramNames.push_back(wideFromUtf8(n.get<std::string>()));
    }
    g.asTab = j.value("asTab", false);
    if (j.contains("figures") && j["figures"].is_array()) {
        for (const auto& f : j["figures"]) g.figures.push_back(figureFromJson(f));
    }
    if (j.contains("partRows") && j["partRows"].is_array()) {
        for (const auto& r : j["partRows"]) g.partRows.push_back(partRowFromJson(r));
    }
    g.keepIfEmpty = j.value("keepIfEmpty", false);
    g.emptyNote = wideFromUtf8(j.value("emptyNote", std::string()));
    g.layout = wideFromUtf8(j.value("layout", std::string()));
    g.prefix = wideFromUtf8(j.value("prefix", std::string()));
    g.background = wideFromUtf8(j.value("background", std::string()));
    g.canvasWidth = j.value("canvasWidth", 0);
    g.canvasHeight = j.value("canvasHeight", 0);
    if (j.contains("placed") && j["placed"].is_array()) {
        for (const auto& c : j["placed"]) g.placed.push_back(placedControlFromJson(c));
    }
    return g;
}

json dialogSchemaToJsonImpl(const ComponentDialogSchema& s) {
    json j;
    j["typeId"] = utf8FromWide(s.typeId);
    json items = json::array();
    for (const auto& it : s.items) items.push_back(diagramItemToJson(it));
    j["items"] = std::move(items);
    json hotspots = json::array();
    for (const auto& h : s.hotspots) hotspots.push_back(hotspotToJson(h));
    j["hotspots"] = std::move(hotspots);
    json groups = json::array();
    for (const auto& g : s.groups) groups.push_back(groupToJson(g));
    j["groups"] = std::move(groups);
    j["useTabs"] = s.useTabs;
    json rows = json::array();
    for (const auto& r : s.weldRows) rows.push_back(weldRowToJson(r));
    j["weldRows"] = std::move(rows);
    // MM-6: preview (dimensions + toggles) — bos ise yazilmaz (eski paketler ayni).
    if (!s.preview.empty()) {
        json preview = json::object();
        json dims = json::array();
        for (const auto& d : s.preview.dimensions) {
            json o;
            o["param"] = utf8FromWide(d.param);
            o["from"] = utf8FromWide(d.from);
            o["to"] = utf8FromWide(d.to);
            o["axis"] = std::string(1, static_cast<char>(d.axis));
            if (!d.label.empty()) o["label"] = utf8FromWide(d.label);
            dims.push_back(std::move(o));
        }
        preview["dimensions"] = std::move(dims);
        json toggles = json::array();
        for (const auto& t : s.preview.toggles) {
            json o;
            o["param"] = utf8FromWide(t.param);
            o["rolePrefix"] = utf8FromWide(t.rolePrefix);
            toggles.push_back(std::move(o));
        }
        preview["toggles"] = std::move(toggles);
        j["preview"] = std::move(preview);
    }
    return j;
}

ComponentDialogSchema dialogSchemaFromJsonImpl(const json& j) {
    ComponentDialogSchema s;
    s.typeId = wideFromUtf8(j.value("typeId", std::string()));
    if (j.contains("items") && j["items"].is_array()) {
        for (const auto& it : j["items"]) s.items.push_back(diagramItemFromJson(it));
    }
    if (j.contains("hotspots") && j["hotspots"].is_array()) {
        for (const auto& h : j["hotspots"]) s.hotspots.push_back(hotspotFromJson(h));
    }
    if (j.contains("groups") && j["groups"].is_array()) {
        for (const auto& g : j["groups"]) s.groups.push_back(groupFromJson(g));
    }
    s.useTabs = j.value("useTabs", false);
    if (j.contains("weldRows") && j["weldRows"].is_array()) {
        for (const auto& r : j["weldRows"]) s.weldRows.push_back(weldRowFromJson(r));
    }
    // MM-6: preview (dimensions + toggles). Eksikse bos kalir (geriye uyumlu).
    if (j.contains("preview") && j["preview"].is_object()) {
        const json& pv = j["preview"];
        if (pv.contains("dimensions") && pv["dimensions"].is_array()) {
            for (const auto& d : pv["dimensions"]) {
                if (!d.is_object()) continue;
                PreviewDimension dim;
                dim.param = wideFromUtf8(d.value("param", std::string()));
                dim.from = wideFromUtf8(d.value("from", std::string()));
                dim.to = wideFromUtf8(d.value("to", std::string()));
                const std::string ax = d.value("axis", std::string("u"));
                dim.axis = ax.empty() ? L'u' : static_cast<wchar_t>(ax.front());
                dim.label = wideFromUtf8(d.value("label", std::string()));
                s.preview.dimensions.push_back(std::move(dim));
            }
        }
        if (pv.contains("toggles") && pv["toggles"].is_array()) {
            for (const auto& t : pv["toggles"]) {
                if (!t.is_object()) continue;
                PreviewToggle tg;
                tg.param = wideFromUtf8(t.value("param", std::string()));
                tg.rolePrefix = wideFromUtf8(t.value("rolePrefix", std::string()));
                s.preview.toggles.push_back(std::move(tg));
            }
        }
    }
    return s;
}

} // namespace

nlohmann::json componentDialogSchemaToJson(const ComponentDialogSchema& s) {
    json j = dialogSchemaToJsonImpl(s);
    j["formatVersion"] = kCodecFormatVersion;
    return j;
}

ComponentDialogSchema componentDialogSchemaFromJson(const nlohmann::json& j) {
    return dialogSchemaFromJsonImpl(j);
}

// ============================================================================
// GenerationPlan
// ============================================================================

namespace {

json plateSpecToJson(const PlateSpec& p) {
    json j;
    j["center"] = vec3ToJson(p.center);
    j["u"] = vec3ToJson(p.u);
    j["v"] = vec3ToJson(p.v);
    j["axis"] = vec3ToJson(p.axis);
    j["width"] = p.width;
    j["height"] = p.height;
    j["thickness"] = p.thickness;
    j["name"] = utf8FromWide(p.name);
    j["material"] = utf8FromWide(p.material);
    j["finish"] = utf8FromWide(p.finish);
    j["posNo"] = utf8FromWide(p.posNo);
    json chamfers = json::array();
    for (const auto& c : p.chamfers)
        chamfers.push_back(json{{"corner", c.corner}, {"type", c.type}, {"h", c.h}, {"v", c.v}});
    j["chamfers"] = std::move(chamfers);
    json outline = json::array();
    for (const auto& pt : p.outline) outline.push_back(json::array({pt.first, pt.second}));
    j["outline"] = std::move(outline);
    return j;
}

PlateSpec plateSpecFromJson(const json& j) {
    PlateSpec p;
    p.center = vec3FromJson(j.value("center", json::array()));
    p.u = vec3FromJson(j.value("u", json::array()), {1.0, 0.0, 0.0});
    p.v = vec3FromJson(j.value("v", json::array()), {0.0, 1.0, 0.0});
    p.axis = vec3FromJson(j.value("axis", json::array()), {0.0, 0.0, 1.0});
    p.width = j.value("width", 0.0);
    p.height = j.value("height", 0.0);
    p.thickness = j.value("thickness", 0.0);
    p.name = wideFromUtf8(j.value("name", std::string()));
    p.material = wideFromUtf8(j.value("material", std::string()));
    p.finish = wideFromUtf8(j.value("finish", std::string()));
    p.posNo = wideFromUtf8(j.value("posNo", std::string()));
    if (j.contains("chamfers") && j["chamfers"].is_array()) {
        for (const auto& c : j["chamfers"]) {
            PlateSpec::Chamfer ch;
            ch.corner = c.value("corner", 0);
            ch.type = c.value("type", 1);
            ch.h = c.value("h", 0.0);
            ch.v = c.value("v", 0.0);
            p.chamfers.push_back(ch);
        }
    }
    if (j.contains("outline") && j["outline"].is_array()) {
        for (const auto& pt : j["outline"]) {
            if (pt.is_array() && pt.size() >= 2)
                p.outline.emplace_back(pt[0].get<double>(), pt[1].get<double>());
        }
    }
    return p;
}

json boltParamsToJson(const BoltParams& b) {
    json j;
    j["nominalDiameter"] = b.nominalDiameter;
    j["holeDiameter"] = b.holeDiameter;
    j["grade"] = b.grade;
    j["hasHead"] = b.hasHead;
    j["hasNut"] = b.hasNut;
    j["hasWasher"] = b.hasWasher;
    j["standard"] = b.standard;
    j["threadInMat"] = b.threadInMat;
    j["siteWorkshop"] = b.siteWorkshop;
    j["cutLengthMode"] = b.cutLengthMode;
    j["cutLength"] = b.cutLength;
    j["lengthIncrease"] = b.lengthIncrease;
    j["direction"] = b.direction;
    j["comment"] = b.comment;
    return j;
}

BoltParams boltParamsFromJson(const json& j) {
    BoltParams b;
    b.nominalDiameter = j.value("nominalDiameter", 20.0);
    b.holeDiameter = j.value("holeDiameter", 22.0);
    b.grade = j.value("grade", std::string("8.8"));
    b.hasHead = j.value("hasHead", true);
    b.hasNut = j.value("hasNut", true);
    b.hasWasher = j.value("hasWasher", true);
    b.standard = j.value("standard", std::string());
    b.threadInMat = j.value("threadInMat", false);
    b.siteWorkshop = j.value("siteWorkshop", 0);
    b.cutLengthMode = j.value("cutLengthMode", 0);
    b.cutLength = j.value("cutLength", 0.0);
    b.lengthIncrease = j.value("lengthIncrease", 0.0);
    b.direction = j.value("direction", 1);
    b.comment = j.value("comment", std::string());
    return b;
}

json boltGroupToJson(const BoltGroup& b) {
    json j;
    j["id"] = entityToJson(b.id);
    json parts = json::array();
    for (const auto& id : b.parts) parts.push_back(entityToJson(id));
    j["parts"] = std::move(parts);
    json roles = json::array();
    for (const auto& r : b.partRoles) roles.push_back(r);
    j["partRoles"] = std::move(roles);
    j["targetOwnedPlateRole"] = b.targetOwnedPlateRole;
    j["origin"] = vec3ToJson(b.origin);
    j["axis"] = vec3ToJson(b.axis);
    j["xDir"] = vec3ToJson(b.xDir);
    j["params"] = boltParamsToJson(b.params);
    j["displayEntityId"] = entityToJson(b.displayEntityId);
    j["countX"] = b.countX;
    j["countY"] = b.countY;
    j["spacingX"] = b.spacingX;
    j["spacingY"] = b.spacingY;
    json sx = json::array();
    for (const auto& v : b.spacingXs) sx.push_back(v);
    j["spacingXs"] = std::move(sx);
    json sy = json::array();
    for (const auto& v : b.spacingYs) sy.push_back(v);
    j["spacingYs"] = std::move(sy);
    json del = json::array();
    for (const auto& n : b.deletedBolts) del.push_back(n);
    j["deletedBolts"] = std::move(del);
    j["holeDepth"] = b.holeDepth;
    j["gripLength"] = b.gripLength;
    j["staggerMode"] = b.staggerMode;
    j["staggerOffset"] = b.staggerOffset;
    j["createHoles"] = b.createHoles;
    j["holeShapeTarget"] = b.holeShapeTarget;
    j["shapeHoleDiameter"] = b.shapeHoleDiameter;
    j["slotAlong"] = b.slotAlong;
    j["slotAcross"] = b.slotAcross;
    j["slotDir"] = vec3ToJson(b.slotDir);
    json shapeParts = json::array();
    for (const auto& id : b.shapeParts) shapeParts.push_back(entityToJson(id));
    j["shapeParts"] = std::move(shapeParts);
    j["enabled"] = b.enabled;
    return j;
}

BoltGroup boltGroupFromJson(const json& j) {
    BoltGroup b;
    b.id = entityFromJson(j.value("id", json(0)));
    if (j.contains("parts") && j["parts"].is_array()) {
        for (const auto& id : j["parts"]) b.parts.push_back(entityFromJson(id));
    }
    if (j.contains("partRoles") && j["partRoles"].is_array()) {
        for (const auto& r : j["partRoles"]) b.partRoles.push_back(r.get<std::string>());
    }
    b.targetOwnedPlateRole = j.value("targetOwnedPlateRole", std::string());
    b.origin = vec3FromJson(j.value("origin", json::array()));
    b.axis = vec3FromJson(j.value("axis", json::array()), {0.0, 0.0, 1.0});
    b.xDir = vec3FromJson(j.value("xDir", json::array()), {1.0, 0.0, 0.0});
    b.params = boltParamsFromJson(j.value("params", json::object()));
    b.displayEntityId = entityFromJson(j.value("displayEntityId", json(0)));
    b.countX = j.value("countX", 1);
    b.countY = j.value("countY", 1);
    b.spacingX = j.value("spacingX", 0.0);
    b.spacingY = j.value("spacingY", 0.0);
    if (j.contains("spacingXs") && j["spacingXs"].is_array()) {
        for (const auto& v : j["spacingXs"]) b.spacingXs.push_back(v.get<double>());
    }
    if (j.contains("spacingYs") && j["spacingYs"].is_array()) {
        for (const auto& v : j["spacingYs"]) b.spacingYs.push_back(v.get<double>());
    }
    if (j.contains("deletedBolts") && j["deletedBolts"].is_array()) {
        for (const auto& n : j["deletedBolts"]) b.deletedBolts.push_back(n.get<int>());
    }
    b.holeDepth = j.value("holeDepth", 0.0);
    b.gripLength = j.value("gripLength", 0.0);
    b.staggerMode = j.value("staggerMode", 0);
    b.staggerOffset = j.value("staggerOffset", 0.0);
    b.createHoles = j.value("createHoles", true);
    b.holeShapeTarget = j.value("holeShapeTarget", 0);
    b.shapeHoleDiameter = j.value("shapeHoleDiameter", 0.0);
    b.slotAlong = j.value("slotAlong", 0.0);
    b.slotAcross = j.value("slotAcross", 0.0);
    b.slotDir = vec3FromJson(j.value("slotDir", json::array()));
    if (j.contains("shapeParts") && j["shapeParts"].is_array()) {
        for (const auto& id : j["shapeParts"]) b.shapeParts.push_back(entityFromJson(id));
    }
    b.enabled = j.value("enabled", true);
    return b;
}

json cutModifierToJson(const CutModifier& c) {
    json j;
    j["kind"] = cutKindName(c.kind);
    j["target"] = entityToJson(c.target);
    j["targetRole"] = c.targetRole;
    j["planeOrigin"] = vec3ToJson(c.planeOrigin);
    j["planeNormal"] = vec3ToJson(c.planeNormal);
    j["keepPositive"] = c.keepPositive;
    json poly = json::array();
    for (const auto& p : c.polygon) poly.push_back(vec3ToJson(p));
    j["polygon"] = std::move(poly);
    j["tool"] = entityToJson(c.tool);
    j["source"] = entityToJson(c.source);
    j["enabled"] = c.enabled;
    j["name"] = c.name;
    j["invertSide"] = c.invertSide;
    j["diameter"] = c.diameter;
    j["depth"] = c.depth;
    j["slotAlong"] = c.slotAlong;
    j["slotAcross"] = c.slotAcross;
    j["slotDir"] = vec3ToJson(c.slotDir);
    j["owner"] = entityToJson(c.owner);
    j["hidden"] = c.hidden;
    return j;
}

CutModifier cutModifierFromJson(const json& j) {
    CutModifier c;
    c.kind = cutKindFromName(j.value("kind", std::string("PlaneCut")));
    c.target = entityFromJson(j.value("target", json(0)));
    c.targetRole = j.value("targetRole", std::string());
    c.planeOrigin = vec3FromJson(j.value("planeOrigin", json::array()));
    c.planeNormal = vec3FromJson(j.value("planeNormal", json::array()), {0.0, 0.0, 1.0});
    c.keepPositive = j.value("keepPositive", true);
    if (j.contains("polygon") && j["polygon"].is_array()) {
        for (const auto& p : j["polygon"]) c.polygon.push_back(vec3FromJson(p));
    }
    c.tool = entityFromJson(j.value("tool", json(0)));
    c.source = entityFromJson(j.value("source", json(0)));
    c.enabled = j.value("enabled", true);
    c.name = j.value("name", std::string());
    c.invertSide = j.value("invertSide", false);
    c.diameter = j.value("diameter", 0.0);
    c.depth = j.value("depth", 0.0);
    c.slotAlong = j.value("slotAlong", 0.0);
    c.slotAcross = j.value("slotAcross", 0.0);
    c.slotDir = vec3FromJson(j.value("slotDir", json::array()));
    c.owner = entityFromJson(j.value("owner", json(0)));
    c.hidden = j.value("hidden", false);
    return c;
}

json weldSpecToJson(const WeldSpec& w) {
    json j;
    j["partA"] = entityToJson(w.partA);
    j["partB"] = entityToJson(w.partB);
    j["partARole"] = w.partARole;
    j["partBRole"] = w.partBRole;
    j["throat"] = w.throat;
    j["size"] = w.size;
    j["doubleSided"] = w.doubleSided;
    j["name"] = utf8FromWide(w.name);
    return j;
}

WeldSpec weldSpecFromJson(const json& j) {
    WeldSpec w;
    w.partA = entityFromJson(j.value("partA", json(0)));
    w.partB = entityFromJson(j.value("partB", json(0)));
    w.partARole = j.value("partARole", std::string());
    w.partBRole = j.value("partBRole", std::string());
    w.throat = j.value("throat", 0.0);
    w.size = j.value("size", 5.0);
    w.doubleSided = j.value("doubleSided", false);
    w.name = wideFromUtf8(j.value("name", std::string()));
    return w;
}

json generationPlanToJsonImpl(const GenerationPlan& p) {
    json j;
    json plates = json::array();
    for (const auto& s : p.plates) plates.push_back(plateSpecToJson(s));
    j["plates"] = std::move(plates);
    json bolts = json::array();
    for (const auto& b : p.boltGroups) bolts.push_back(boltGroupToJson(b));
    j["boltGroups"] = std::move(bolts);
    json cuts = json::array();
    for (const auto& c : p.cuts) cuts.push_back(cutModifierToJson(c));
    j["cuts"] = std::move(cuts);
    json welds = json::array();
    for (const auto& w : p.welds) welds.push_back(weldSpecToJson(w));
    j["welds"] = std::move(welds);
    j["plateKeys"] = p.plateKeys;
    j["boltGroupKeys"] = p.boltGroupKeys;
    j["cutKeys"] = p.cutKeys;
    j["weldKeys"] = p.weldKeys;
    // MM-7: kontrol sonuclari raporlama alani. BOS ise yazilmaz (eski paketlerin
    // baytlari/digest'i degismesin). Digest'e GIRMEZ (asagida canonicalJson'dan
    // cikarilir): digest yalniz GEOMETRI esdegerligini olcer.
    if (!p.checks.empty()) {
        json chk = json::array();
        for (const auto& c : p.checks) {
            json cj;
            cj["id"] = c.id;
            cj["pass"] = c.pass;
            cj["value"] = c.value;
            cj["limit"] = c.limit;
            cj["message"] = utf8FromWide(c.message);
            if (c.warning) cj["warning"] = true;
            chk.push_back(std::move(cj));
        }
        j["checks"] = std::move(chk);
    }
    j["ok"] = p.ok;
    j["error"] = utf8FromWide(p.error);
    return j;
}

GenerationPlan generationPlanFromJsonImpl(const json& j) {
    GenerationPlan p;
    if (j.contains("plates") && j["plates"].is_array()) {
        for (const auto& s : j["plates"]) p.plates.push_back(plateSpecFromJson(s));
    }
    if (j.contains("boltGroups") && j["boltGroups"].is_array()) {
        for (const auto& b : j["boltGroups"]) p.boltGroups.push_back(boltGroupFromJson(b));
    }
    if (j.contains("cuts") && j["cuts"].is_array()) {
        for (const auto& c : j["cuts"]) p.cuts.push_back(cutModifierFromJson(c));
    }
    if (j.contains("welds") && j["welds"].is_array()) {
        for (const auto& w : j["welds"]) p.welds.push_back(weldSpecFromJson(w));
    }
    if (j.contains("plateKeys") && j["plateKeys"].is_array()) {
        for (const auto& k : j["plateKeys"]) p.plateKeys.push_back(k.get<std::string>());
    }
    if (j.contains("boltGroupKeys") && j["boltGroupKeys"].is_array()) {
        for (const auto& k : j["boltGroupKeys"]) p.boltGroupKeys.push_back(k.get<std::string>());
    }
    if (j.contains("cutKeys") && j["cutKeys"].is_array()) {
        for (const auto& k : j["cutKeys"]) p.cutKeys.push_back(k.get<std::string>());
    }
    if (j.contains("weldKeys") && j["weldKeys"].is_array()) {
        for (const auto& k : j["weldKeys"]) p.weldKeys.push_back(k.get<std::string>());
    }
    // MM-7: kontrol sonuclari (opsiyonel; yoksa bos).
    if (j.contains("checks") && j["checks"].is_array()) {
        for (const auto& cj : j["checks"]) {
            ComponentCheck c;
            c.id = cj.value("id", std::string());
            c.pass = cj.value("pass", true);
            c.value = cj.value("value", 0.0);
            c.limit = cj.value("limit", 0.0);
            c.message = wideFromUtf8(cj.value("message", std::string()));
            c.warning = cj.value("warning", false);
            p.checks.push_back(std::move(c));
        }
    }
    p.ok = j.value("ok", true);
    p.error = wideFromUtf8(j.value("error", std::string()));
    return p;
}

} // namespace

nlohmann::json generationPlanToJson(const GenerationPlan& p) {
    json j = generationPlanToJsonImpl(p);
    j["formatVersion"] = kCodecFormatVersion;
    return j;
}

GenerationPlan generationPlanFromJson(const nlohmann::json& j) {
    return generationPlanFromJsonImpl(j);
}

// ============================================================================
// MemberGeometry / ComponentInput — paket altin test GIRDISI
// ============================================================================

nlohmann::json memberGeometryToJson(const MemberGeometry& m) {
    json j;
    j["id"] = static_cast<std::uint64_t>(m.id);
    j["axisFrom"] = vec3ToJson(m.axisFrom);
    j["axisTo"] = vec3ToJson(m.axisTo);
    j["refNormal"] = vec3ToJson(m.refNormal);
    j["secWidth"] = m.secWidth;
    j["secHeight"] = m.secHeight;
    j["secWebThickness"] = m.secWebThickness;
    j["secFlangeThickness"] = m.secFlangeThickness;
    j["sectionCenterOffset"] = m.sectionCenterOffset;
    j["profileRotation"] = m.profileRotation;
    return j;
}

MemberGeometry memberGeometryFromJson(const nlohmann::json& j) {
    MemberGeometry m;
    if (!j.is_object()) return m;
    m.id = entityFromJson(j.value("id", json()), kInvalidEntityId);
    m.axisFrom = vec3FromJson(j.value("axisFrom", json::array()));
    m.axisTo = vec3FromJson(j.value("axisTo", json::array()));
    m.refNormal = vec3FromJson(j.value("refNormal", json::array()), Vec3{0.0, 0.0, 1.0});
    m.secWidth = j.value("secWidth", 0.0);
    m.secHeight = j.value("secHeight", 0.0);
    m.secWebThickness = j.value("secWebThickness", 0.0);
    m.secFlangeThickness = j.value("secFlangeThickness", 0.0);
    m.sectionCenterOffset = j.value("sectionCenterOffset", 0.0);
    m.profileRotation = j.value("profileRotation", 0.0);
    return m;
}

nlohmann::json componentInputToJson(const ComponentInput& in) {
    json j;
    j["main"] = memberGeometryToJson(in.main);
    json sec = json::array();
    for (const auto& m : in.secondary) sec.push_back(memberGeometryToJson(m));
    j["secondary"] = std::move(sec);
    json params = json::array();
    for (const auto& p : in.params) params.push_back(compParamToJsonImpl(p));
    j["params"] = std::move(params);
    return j;
}

ComponentInput componentInputFromJson(const nlohmann::json& j) {
    ComponentInput in;
    if (!j.is_object()) return in;
    in.main = memberGeometryFromJson(j.value("main", json::object()));
    if (j.contains("secondary") && j["secondary"].is_array())
        for (const auto& m : j["secondary"]) in.secondary.push_back(memberGeometryFromJson(m));
    if (j.contains("params") && j["params"].is_array())
        for (const auto& p : j["params"]) in.params.push_back(compParamFromJsonImpl(p));
    return in;
}

// ============================================================================
// RECIPE (MM-5) — recipe.json <-> Recipe
// ============================================================================

namespace {

std::string recipeStr(const json& j, const char* key) {
    if (!j.contains(key)) return {};
    const json& v = j[key];
    if (v.is_string()) return v.get<std::string>();
    return {};
}

// Skaler arguman metni: sayi ise ifade olarak yazilir (2 -> "2", 2.5 -> "2.5").
std::string recipeScalarText(const json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_boolean()) return v.get<bool>() ? "1" : "0";
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_number_unsigned()) return std::to_string(v.get<unsigned long long>());
    if (v.is_number_float()) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.17g", v.get<double>());
        return std::string(buf);
    }
    return v.dump();  // beklenmeyen tur: metin olarak tasi, sema yakalar
}

RecipeArg recipeArgFromJson(const json& v) {
    RecipeArg a;
    if (v.is_array()) {
        a.isList = true;
        for (const auto& item : v) a.items.push_back(recipeArgFromJson(item));
        return a;
    }
    a.text = recipeScalarText(v);
    return a;
}

json recipeArgToJson(const RecipeArg& a) {
    if (a.isList) {
        json arr = json::array();
        for (const auto& item : a.items) arr.push_back(recipeArgToJson(item));
        return arr;
    }
    return a.text;
}

}  // namespace

nlohmann::json recipeToJson(const Recipe& r) {
    json j;
    j["formatVersion"] = kCodecFormatVersion;
    j["recipeVersion"] = r.recipeVersion;
    json lets = json::object();
    for (const auto& kv : r.lets) lets[kv.first] = kv.second;
    j["let"] = lets;
    json steps = json::array();
    for (const auto& s : r.steps) {
        json o;
        if (!s.id.empty()) o["id"] = s.id;
        if (!s.use.empty()) o["use"] = s.use;
        if (!s.check.empty()) o["check"] = s.check;
        if (!s.message.empty()) o["message"] = s.message;
        if (!s.role.empty()) o["role"] = s.role;
        if (!s.when.empty()) o["when"] = s.when;
        if (!s.args.empty()) {
            json args = json::object();
            for (const auto& kv : s.args) args[kv.first] = recipeArgToJson(kv.second);
            o["args"] = args;
        }
        steps.push_back(o);
    }
    j["steps"] = steps;
    return j;
}

Recipe recipeFromJson(const nlohmann::json& j) {
    Recipe r;
    if (j.contains("recipeVersion") && j["recipeVersion"].is_string())
        r.recipeVersion = j["recipeVersion"].get<std::string>();
    if (j.contains("let") && j["let"].is_object()) {
        for (auto it = j["let"].begin(); it != j["let"].end(); ++it) {
            if (it.value().is_string())
                r.lets.emplace_back(it.key(), it.value().get<std::string>());
        }
    }
    if (j.contains("steps") && j["steps"].is_array()) {
        for (const auto& s : j["steps"]) {
            if (!s.is_object()) continue;
            RecipeStep step;
            step.id = recipeStr(s, "id");
            step.use = recipeStr(s, "use");
            step.role = recipeStr(s, "role");
            step.when = recipeStr(s, "when");
            step.check = recipeStr(s, "check");
            step.message = recipeStr(s, "message");
            if (s.contains("args") && s["args"].is_object()) {
                for (auto it = s["args"].begin(); it != s["args"].end(); ++it)
                    step.args[it.key()] = recipeArgFromJson(it.value());
            }
            r.steps.push_back(std::move(step));
        }
    }
    return r;
}

// ============================================================================
// Kanonik JSON + digest
// ============================================================================

namespace {

std::string canonDouble(double x) {
    double r = std::round(x * 1e6) / 1e6;
    if (r == 0.0) r = 0.0; // -0 -> +0
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6f", r);
    return buf;
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20u) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c); // UTF-8 bayt oldugu gibi
                }
        }
    }
    out += '"';
    return out;
}

void canonAppend(const json& j, std::string& out) {
    if (j.is_object()) {
        out += '{';
        bool first = true;
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (!first) out += ',';
            first = false;
            out += jsonEscape(it.key());
            out += ':';
            canonAppend(it.value(), out);
        }
        out += '}';
    } else if (j.is_array()) {
        out += '[';
        bool first = true;
        for (const auto& e : j) {
            if (!first) out += ',';
            first = false;
            canonAppend(e, out);
        }
        out += ']';
    } else if (j.is_string()) {
        out += jsonEscape(j.get<std::string>());
    } else if (j.is_number_float()) {
        out += canonDouble(j.get<double>());
    } else if (j.is_number_unsigned()) {
        out += std::to_string(j.get<std::uint64_t>());
    } else if (j.is_number_integer()) {
        out += std::to_string(j.get<std::int64_t>());
    } else if (j.is_boolean()) {
        out += j.get<bool>() ? "true" : "false";
    } else {
        out += "null";
    }
}

} // namespace

std::string canonicalJsonString(const nlohmann::json& j) {
    std::string out;
    canonAppend(j, out);
    return out;
}

std::string generationPlanCanonicalJson(const GenerationPlan& p) {
    json j = generationPlanToJsonImpl(p);
    // MM-7: kontrol sonuclari RAPORLAMA alanidir; digest yalniz GEOMETRI
    // esdegerligini olcmeli (checks mesajlarindaki degisiklik digest'i kaydirmamali).
    j.erase("checks");
    // MM-2b: digest KIMLIK-BAGIMSIZ olmalidir. Uye referanslari EntityId yerine
    // SEMANTIK rol anahtariyla hash'lenir ("main", "secondary_0", plateKey):
    // ayni fiziksel girdi, Studio'da ve Model Maker'da farkli EntityId ile de
    // ayni planDigest'i verir. Malzemelesme-zamani kimlikler (bolt id, display,
    // shapeParts, tool/source/owner) digest'e HIC girmez.
    if (j.contains("boltGroups") && j["boltGroups"].is_array()) {
        for (auto& b : j["boltGroups"]) {
            const json parts = b.value("parts", json::array());
            const json saved = b.value("partRoles", json::array());
            json roles = json::array();
            for (std::size_t k = 0; k < parts.size(); ++k) {
                std::string role =
                    (k < saved.size() && !saved[k].get<std::string>().empty())
                        ? saved[k].get<std::string>()
                        : ("part_" + std::to_string(k));
                roles.push_back(std::move(role));
            }
            b["parts"] = std::move(roles);
            b.erase("partRoles");
            b.erase("id");
            b.erase("displayEntityId");
            b.erase("shapeParts");
        }
    }
    if (j.contains("welds") && j["welds"].is_array()) {
        for (auto& w : j["welds"]) {
            const std::string aRole = w.value("partARole", std::string());
            const std::string bRole = w.value("partBRole", std::string());
            w["partA"] = aRole.empty() ? "partA" : aRole;
            w["partB"] = bRole.empty() ? "partB" : bRole;
            w.erase("partARole");
            w.erase("partBRole");
        }
    }
    if (j.contains("cuts") && j["cuts"].is_array()) {
        for (auto& c : j["cuts"]) {
            const std::string role = c.value("targetRole", std::string());
            c["target"] = role.empty() ? "target" : role;
            c.erase("targetRole");
            c.erase("tool");
            c.erase("source");
            c.erase("owner");
        }
    }
    return canonicalJsonString(j);
}

std::string generationPlanDigest(const GenerationPlan& p) {
    const std::string canon = generationPlanCanonicalJson(p);
    return sha256Hex(canon.data(), canon.size());
}

} // namespace mm
