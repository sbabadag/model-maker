#include "model_maker/component_engine.hpp"

#include "model_maker/end_plate.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace mm {

// --- Parametre yardimcilari --------------------------------------------------

CompParamValue paramDefaultValue(const CompParamSpec& spec) noexcept {
    switch (spec.type) {
        case CompParamType::Length:
        case CompParamType::Angle:
            return CompParamValue{spec.defDouble};
        case CompParamType::Integer:
            return CompParamValue{spec.defInt};
        case CompParamType::Boolean:
            return CompParamValue{spec.defBool};
        case CompParamType::Enum:
        case CompParamType::String:
        case CompParamType::Profile:
        case CompParamType::Material:
            return CompParamValue{spec.defString};
        case CompParamType::EntityRef:
            return CompParamValue{EntityId{kInvalidEntityId}};
    }
    return CompParamValue{spec.defDouble};
}

namespace {

double clampD(double v, double lo, double hi) noexcept {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int clampI(int v, int lo, int hi) noexcept {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Bir double degeri coerce et (double yoksa int/bool'dan; yoksa nullopt).
std::optional<double> toDouble(const CompParamValue& v) noexcept {
    if (const auto* d = std::get_if<double>(&v)) return *d;
    if (const auto* i = std::get_if<int>(&v)) return static_cast<double>(*i);
    if (const auto* b = std::get_if<bool>(&v)) return *b ? 1.0 : 0.0;
    return std::nullopt;
}

std::optional<int> toInt(const CompParamValue& v) noexcept {
    if (const auto* i = std::get_if<int>(&v)) return *i;
    if (const auto* d = std::get_if<double>(&v)) return static_cast<int>(std::llround(*d));
    if (const auto* b = std::get_if<bool>(&v)) return *b ? 1 : 0;
    return std::nullopt;
}

std::optional<bool> toBool(const CompParamValue& v) noexcept {
    if (const auto* b = std::get_if<bool>(&v)) return *b;
    if (const auto* i = std::get_if<int>(&v)) return *i != 0;
    return std::nullopt;
}

std::optional<std::wstring> toWString(const CompParamValue& v) noexcept {
    if (const auto* s = std::get_if<std::wstring>(&v)) return *s;
    return std::nullopt;
}

std::optional<EntityId> toEntity(const CompParamValue& v) noexcept {
    if (const auto* e = std::get_if<EntityId>(&v)) return *e;
    return std::nullopt;
}

} // namespace

CompParamValue validateParamValue(const CompParamSpec& spec,
                                  const CompParamValue& value) noexcept {
    switch (spec.type) {
        case CompParamType::Length:
        case CompParamType::Angle: {
            auto d = toDouble(value);
            if (!d) return paramDefaultValue(spec);
            if (spec.hasMinDouble) d = clampD(*d, spec.minDouble, spec.maxDouble);
            if (spec.hasMaxDouble) d = clampD(*d, spec.minDouble, spec.maxDouble);
            return CompParamValue{*d};
        }
        case CompParamType::Integer: {
            auto i = toInt(value);
            if (!i) return paramDefaultValue(spec);
            if (spec.hasMinInt) i = clampI(*i, spec.minInt, spec.maxInt);
            if (spec.hasMaxInt) i = clampI(*i, spec.minInt, spec.maxInt);
            return CompParamValue{*i};
        }
        case CompParamType::Boolean: {
            auto b = toBool(value);
            if (!b) return paramDefaultValue(spec);
            return CompParamValue{*b};
        }
        case CompParamType::Enum: {
            auto s = toWString(value);
            if (!s) return paramDefaultValue(spec);
            for (const auto& choice : spec.choices)
                if (choice == *s) return CompParamValue{*s};
            return paramDefaultValue(spec);  // gecersiz secenek -> varsayilan
        }
        case CompParamType::String:
        case CompParamType::Profile:
        case CompParamType::Material: {
            auto s = toWString(value);
            if (!s) return paramDefaultValue(spec);
            return CompParamValue{*s};
        }
        case CompParamType::EntityRef: {
            auto e = toEntity(value);
            if (!e) return paramDefaultValue(spec);
            return CompParamValue{*e};
        }
    }
    return paramDefaultValue(spec);
}

std::vector<CompParam> alignParamsToSpecs(const std::vector<CompParamSpec>& specs,
                                          const std::vector<CompParam>& values) noexcept {
    std::vector<CompParam> aligned;
    aligned.reserve(specs.size() + values.size());
    std::vector<bool> matched(values.size(), false);
    for (const auto& spec : specs) {
        CompParam p;
        p.name = spec.name;
        bool found = false;
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (values[i].name == spec.name) {
                p.value = validateParamValue(spec, values[i].value);
                matched[i] = true;
                found = true;
                break;
            }
        }
        if (!found) p.value = paramDefaultValue(spec);
        aligned.push_back(std::move(p));
    }
    // Bilinmeyen parametreleri KORU (sessizce kaybetme): sona ekle.
    for (std::size_t i = 0; i < values.size(); ++i)
        if (!matched[i]) aligned.push_back(values[i]);
    return aligned;
}

const CompParamSpec* findParamSpec(const std::vector<CompParamSpec>& specs,
                                   const std::wstring& name) noexcept {
    for (const auto& s : specs)
        if (s.name == name) return &s;
    return nullptr;
}

const CompParam* findParam(const std::vector<CompParam>& params,
                           const std::wstring& name) noexcept {
    for (const auto& p : params)
        if (p.name == name) return &p;
    return nullptr;
}

double paramAsDouble(const std::vector<CompParam>& params, const std::wstring& name,
                     double fallback) noexcept {
    const CompParam* p = findParam(params, name);
    if (!p) return fallback;
    auto d = toDouble(p->value);
    return d ? *d : fallback;
}

int paramAsInt(const std::vector<CompParam>& params, const std::wstring& name,
               int fallback) noexcept {
    const CompParam* p = findParam(params, name);
    if (!p) return fallback;
    auto i = toInt(p->value);
    return i ? *i : fallback;
}

bool paramAsBool(const std::vector<CompParam>& params, const std::wstring& name,
                 bool fallback) noexcept {
    const CompParam* p = findParam(params, name);
    if (!p) return fallback;
    auto b = toBool(p->value);
    return b ? *b : fallback;
}

std::wstring paramAsString(const std::vector<CompParam>& params,
                           const std::wstring& name, std::wstring fallback) noexcept {
    const CompParam* p = findParam(params, name);
    if (!p) return fallback;
    auto s = toWString(p->value);
    return s ? *s : fallback;
}

EntityId paramAsEntity(const std::vector<CompParam>& params, const std::wstring& name,
                       EntityId fallback) noexcept {
    const CompParam* p = findParam(params, name);
    if (!p) return fallback;
    auto e = toEntity(p->value);
    return e ? *e : fallback;
}

// --- KALICI CHILD KAYDI (C1) -------------------------------------------------

bool isValidComponentChildRecord(const ComponentChildRecord& c) noexcept {
    if (c.roleKey.empty()) return false;
    for (char ch : c.roleKey) {
        const bool alpha = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        const bool digit = (ch >= '0' && ch <= '9');
        if (!alpha && !digit && ch != '_') return false;
    }
    // kind ile TAM BIR payload eslesmeli; diger payload'lar bos olmali.
    switch (c.kind) {
        case ComponentObjectKind::Plate:
            return c.plate.has_value() && !c.boltGroup && !c.cut && !c.weld;
        case ComponentObjectKind::BoltGroup:
            return c.boltGroup.has_value() && !c.plate && !c.cut && !c.weld;
        case ComponentObjectKind::Cut:
            return c.cut.has_value() && !c.plate && !c.boltGroup && !c.weld;
        case ComponentObjectKind::Weld:
            return c.weld.has_value() && !c.plate && !c.boltGroup && !c.cut;
        case ComponentObjectKind::ConstructionPoint:
        case ComponentObjectKind::ConstructionLine:
        case ComponentObjectKind::ConstructionPlane:
            // Construction nesneleri KALICI child degildir (cozum asamasinda,
            // EntityId uretmezler) — kalici tabloya yazilamaz.
            return false;
    }
    return false;
}

// --- Registry ----------------------------------------------------------------

ComponentRegistry& ComponentRegistry::instance() noexcept {
    static ComponentRegistry reg;
    return reg;
}

bool ComponentRegistry::registerType(ComponentType type, ComponentGenerator generator) noexcept {
    if (type.typeId.empty() || generator == nullptr) return false;
    for (const auto& e : entries_)
        if (e.type.typeId == type.typeId) return false;  // tekil
    Entry entry;
    entry.type = std::move(type);
    entry.gen = generator;
    entries_.push_back(std::move(entry));
    return true;
}

const ComponentType* ComponentRegistry::findType(const std::wstring& typeId) const noexcept {
    for (const auto& e : entries_)
        if (e.type.typeId == typeId) return &e.type;
    return nullptr;
}

const ComponentGenerator* ComponentRegistry::generator(const std::wstring& typeId) const noexcept {
    for (const auto& e : entries_)
        if (e.type.typeId == typeId) return &e.gen;
    return nullptr;
}

std::vector<std::wstring> ComponentRegistry::typeIds() const noexcept {
    std::vector<std::wstring> ids;
    ids.reserve(entries_.size());
    for (const auto& e : entries_) ids.push_back(e.type.typeId);
    return ids;
}

GenerationPlan ComponentRegistry::generate(const std::wstring& typeId,
                                           const ComponentInput& input) const noexcept {
    GenerationPlan plan;
    const ComponentType* type = findType(typeId);
    if (!type) {
        plan.ok = false;
        plan.error = L"bilinmeyen component tipi: " + typeId;
        return plan;
    }
    const ComponentGenerator* gen = generator(typeId);
    if (!gen) {
        plan.ok = false;
        plan.error = L"component ureteci yok: " + typeId;
        return plan;
    }
    // Parametreleri semaya hizala (migration) ve generatora gecerli degerler ver.
    ComponentInput aligned = input;
    aligned.params = alignParamsToSpecs(type->params, input.params);
    plan = (*gen)(aligned);
    return plan;
}

// --- Parametre semasi kurulum yardimcilari (yerlesik katalog) ----------------

namespace {

CompParamSpec lengthParam(const std::wstring& name, const std::wstring& unit,
                          double def, double lo, double hi,
                          const std::wstring& desc = {}) noexcept {
    CompParamSpec s;
    s.name = name;
    s.type = CompParamType::Length;
    s.unit = unit;
    s.defDouble = def;
    s.hasMinDouble = true;
    s.hasMaxDouble = true;
    s.minDouble = lo;
    s.maxDouble = hi;
    s.description = desc;
    return s;
}

CompParamSpec intParam(const std::wstring& name, int def, int lo, int hi,
                       const std::wstring& desc = {}) noexcept {
    CompParamSpec s;
    s.name = name;
    s.type = CompParamType::Integer;
    s.defInt = def;
    s.hasMinInt = true;
    s.hasMaxInt = true;
    s.minInt = lo;
    s.maxInt = hi;
    s.description = desc;
    return s;
}

CompParamSpec boolParam(const std::wstring& name, bool def,
                        const std::wstring& desc = {}) noexcept {
    CompParamSpec s;
    s.name = name;
    s.type = CompParamType::Boolean;
    s.defBool = def;
    s.description = desc;
    return s;
}

CompParamSpec enumParam(const std::wstring& name, std::wstring def,
                        std::vector<std::wstring> choices,
                        const std::wstring& desc = {}) noexcept {
    CompParamSpec s;
    s.name = name;
    s.type = CompParamType::Enum;
    s.defString = std::move(def);
    s.choices = std::move(choices);
    s.description = desc;
    return s;
}

} // namespace

// --- End Plate ---------------------------------------------------------------

ComponentType endPlateComponentType() noexcept {
    ComponentType t;
    t.typeId = L"beam_to_column_end_plate";
    t.schemaVersion = 1;
    t.displayName = L"Beam to Column End Plate";
    t.params = {
        lengthParam(L"plateThickness", L"mm", 12.0, 4.0, 100.0, L"levha kalinligi"),
        lengthParam(L"plateExtra", L"mm", 20.0, 0.0, 200.0, L"kesit bbox'ina eklenen pay"),
        lengthParam(L"boltDiameter", L"mm", 20.0, 12.0, 30.0, L"bulon capi"),
        lengthParam(L"holeDiameter", L"mm", 22.0, 12.0, 34.0, L"delik capi"),
        intParam(L"boltCountX", 2, 1, 8, L"genislik yonunde bulon adedi"),
        intParam(L"boltCountY", 4, 1, 8, L"yukseklik yonunde bulon adedi"),
        lengthParam(L"boltEdge", L"mm", 30.0, 10.0, 200.0, L"kenar mesafesi"),
        enumParam(L"grade", L"8.8", {L"8.8", L"10.9"}, L"bulon sinifi"),
        boolParam(L"hasHead", true, L"bas"),
        boolParam(L"hasNut", true, L"somun"),
        boolParam(L"hasWasher", true, L"pul"),
    };
    return t;
}

GenerationPlan generateEndPlate(const ComponentInput& input) noexcept {
    GenerationPlan plan;
    if (input.secondary.empty()) {
        plan.ok = false;
        plan.error = L"ikincil parca (kiris) secilmeli";
        return plan;
    }
    const MemberGeometry& beam = input.secondary[0];

    EndPlateParams ep;
    ep.plateThickness = paramAsDouble(input.params, L"plateThickness", 12.0);
    ep.plateExtra = paramAsDouble(input.params, L"plateExtra", 20.0);
    ep.boltDiameter = paramAsDouble(input.params, L"boltDiameter", 20.0);
    ep.holeDiameter = paramAsDouble(input.params, L"holeDiameter", 22.0);
    ep.boltCountX = paramAsInt(input.params, L"boltCountX", 2);
    ep.boltCountY = paramAsInt(input.params, L"boltCountY", 4);
    ep.boltEdge = paramAsDouble(input.params, L"boltEdge", 30.0);
    {
        std::wstring g = paramAsString(input.params, L"grade", L"8.8");
        std::string narrow;
        narrow.assign(g.begin(), g.end());
        ep.boltGrade = narrow;
    }
    ep.hasHead = paramAsBool(input.params, L"hasHead", true);
    ep.hasNut = paramAsBool(input.params, L"hasNut", true);
    ep.hasWasher = paramAsBool(input.params, L"hasWasher", true);

    const Vec3 beamAxis{beam.axisTo.x - beam.axisFrom.x, beam.axisTo.y - beam.axisFrom.y,
                        beam.axisTo.z - beam.axisFrom.z};

    const EndPlatePlacement placement =
        computeEndPlatePlacement(beam.axisTo, beamAxis, beam.secWidth, beam.secHeight, ep);

    PlateSpec plate;
    plate.center = placement.plateCenter;
    plate.u = placement.u;
    plate.v = placement.v;
    plate.axis = placement.axis;
    plate.width = placement.plateWidth;
    plate.height = placement.plateHeight;
    plate.thickness = placement.plateThickness;
    plate.name = L"Alin Levhasi";
    plan.plates.push_back(plate);

    BoltGroup bolts = placement.bolts;
    // Generator yalniz KULLANICI parcasini bilir (kolon). Levha materializasyonda
    // parts basina eklenir ({levha, kolon}).
    bolts.parts = {input.main.id};
    plan.boltGroups.push_back(bolts);

    plan.ok = true;
    return plan;
}

// --- Fin Plate ---------------------------------------------------------------

ComponentType finPlateComponentType() noexcept {
    ComponentType t;
    t.typeId = L"fin_plate";
    t.schemaVersion = 1;
    t.displayName = L"Fin Plate (Shear Tab)";
    t.params = {
        lengthParam(L"plateThickness", L"mm", 10.0, 4.0, 60.0, L"levha kalinligi"),
        lengthParam(L"plateWidth", L"mm", 120.0, 40.0, 400.0, L"levha genisligi"),
        lengthParam(L"plateHeight", L"mm", 240.0, 60.0, 800.0, L"levha yuksekligi"),
        lengthParam(L"boltDiameter", L"mm", 20.0, 12.0, 30.0, L"bulon capi"),
        lengthParam(L"holeDiameter", L"mm", 22.0, 12.0, 34.0, L"delik capi"),
        intParam(L"boltCount", 2, 1, 8, L"bulon adedi (tek sira)"),
        lengthParam(L"boltEdge", L"mm", 40.0, 10.0, 200.0, L"kenar mesafesi"),
        enumParam(L"grade", L"8.8", {L"8.8", L"10.9"}, L"bulon sinifi"),
    };
    return t;
}

GenerationPlan generateFinPlate(const ComponentInput& input) noexcept {
    GenerationPlan plan;
    if (input.secondary.empty()) {
        plan.ok = false;
        plan.error = L"ikincil parca (kiris) secilmeli";
        return plan;
    }
    const MemberGeometry& beam = input.secondary[0];

    const double thickness = paramAsDouble(input.params, L"plateThickness", 10.0);
    const double width = paramAsDouble(input.params, L"plateWidth", 120.0);
    const double height = paramAsDouble(input.params, L"plateHeight", 240.0);
    const double boltD = paramAsDouble(input.params, L"boltDiameter", 20.0);
    const double holeD = paramAsDouble(input.params, L"holeDiameter", 22.0);
    const int boltCount = std::max(1, paramAsInt(input.params, L"boltCount", 2));
    const double edge = paramAsDouble(input.params, L"boltEdge", 40.0);
    const std::wstring grade = paramAsString(input.params, L"grade", L"8.8");

    const Vec3 beamAxis{beam.axisTo.x - beam.axisFrom.x, beam.axisTo.y - beam.axisFrom.y,
                        beam.axisTo.z - beam.axisFrom.z};

    // Fin levha: kiris ucuna dik, tek bolon sirasi. End Plate ile AYNI cerceve
    // matematigi yeniden kullanilir (ortak eleman — yeniden yazilmaz).
    const EndPlateFrame f = endPlateFrame(beamAxis, Vec3{0.0, 0.0, 1.0});

    PlateSpec plate;
    plate.u = f.u;
    plate.v = f.v;
    plate.axis = f.axis;
    plate.width = width;
    plate.height = height;
    plate.thickness = thickness;
    const double halfT = thickness * 0.5;
    plate.center = Vec3{beam.axisTo.x + f.axis.x * halfT, beam.axisTo.y + f.axis.y * halfT,
                        beam.axisTo.z + f.axis.z * halfT};
    plate.name = L"Fin Levhasi";
    plan.plates.push_back(plate);

    // Bolonlar: levhanin merkez hattinda tek sira (yukseklik yonunde).
    BoltGroup b;
    b.axis = f.axis;
    b.xDir = f.u;
    const double hw = width * 0.5;
    const double hh = height * 0.5;
    b.origin = Vec3{plate.center.x - f.u.x * hw - f.v.x * (hh - edge),
                    plate.center.y - f.u.y * hw - f.v.y * (hh - edge),
                    plate.center.z - f.u.z * hw - f.v.z * (hh - edge)};
    b.countX = 1;
    b.countY = boltCount;
    b.spacingX = 0.0;
    b.spacingY = (boltCount > 1) ? (height - 2.0 * edge) / static_cast<double>(boltCount - 1)
                                 : 0.0;
    b.params.nominalDiameter = boltD;
    b.params.holeDiameter = holeD;
    {
        std::string narrow;
        narrow.assign(grade.begin(), grade.end());
        b.params.grade = narrow;
    }
    b.params.hasHead = true;
    b.params.hasNut = true;
    b.params.hasWasher = true;
    b.parts = {input.main.id};
    plan.boltGroups.push_back(b);

    plan.ok = true;
    return plan;
}

void registerBuiltInComponents(ComponentRegistry& registry) noexcept {
    registry.registerType(endPlateComponentType(), &generateEndPlate);
    registry.registerType(finPlateComponentType(), &generateFinPlate);
}

} // namespace mm
