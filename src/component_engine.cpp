#include "model_maker/component_engine.hpp"

#include "model_maker/end_plate.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
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

double vdot(const Vec3& a, const Vec3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 vnorm(const Vec3& a) noexcept {
    const double len = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    if (len < 1e-12) return Vec3{0.0, 0.0, 0.0};
    return Vec3{a.x / len, a.y / len, a.z / len};
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

CompParamSpec stringParam(const std::wstring& name, const std::wstring& def,
                          const std::wstring& desc = {}) noexcept {
    CompParamSpec s;
    s.name = name;
    s.type = CompParamType::String;
    s.defString = def;
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
    t.catalog.sourceComponentNumber = L"144";
    t.catalog.category = L"Beam to Column";
    t.catalog.primaryMemberType = L"kolon";
    t.catalog.secondaryMemberType = L"kiris";
    t.catalog.supportedFeatures = {L"alin levhasi", L"bulon grubu"};
    t.catalog.coverageStatus = L"PRODUCTION_READY";
    t.catalog.catalogVisible = false;  // End Plate kendi zengin komut/uretim yolunu kullanir
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
    plan.plateKeys.push_back("end_plate");

    BoltGroup bolts = placement.bolts;
    // Generator yalniz KULLANICI parcasini bilir (kolon). Levha materializasyonda
    // parts basina eklenir ({levha, kolon}).
    bolts.parts = {input.main.id};
    plan.boltGroups.push_back(bolts);
    plan.boltGroupKeys.push_back("beam_bolts");

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
        intParam(L"boltCount", 4, 1, 8, L"bulon adedi (tek sira)"),
        lengthParam(L"boltEdge", L"mm", 40.0, 10.0, 200.0, L"kenar mesafesi"),
        enumParam(L"grade", L"8.8", {L"8.8", L"10.9"}, L"bulon sinifi"),
    };
    t.catalog.sourceComponentNumber = L"";  // genel shear tab (Tekla 146 cekirdegi)
    t.catalog.category = L"Shear connection";
    t.catalog.primaryMemberType = L"kolon";
    t.catalog.secondaryMemberType = L"kiris";
    t.catalog.supportedFeatures = {L"kayma levhasi (shear tab)", L"bulon grubu"};
    t.catalog.coverageStatus = L"GEOMETRY_VERIFIED";
    t.catalog.pickCount = 2;
    t.catalog.catalogVisible = true;
    return t;
}

namespace {

// Ortak shear-tab yerlesim cozumleyici (fin plate + Tekla 146 AYNI cekirdek).
// Tek motor; paralel geometry kernel YOK. ROL TESPITI (dikeylik), kolona EN
// YAKIN kiris ucu, kesit cercevesi (dunya Z), kolon yuzu (destek fonksiyonu)
// ve SectionAnchor ofseti TEK yerden hesaplanir. Parametresizdir (saf geometri);
// levha/bulon olusturma generator'a birakilir.
struct ShearTabPlacement {
    MemberGeometry col;
    MemberGeometry beam;
    bool rolesSwapped{false};
    Vec3 colMid{};
    Vec3 connEnd{};
    Vec3 farEnd{};
    Vec3 lenDir{};
    Vec3 beamDir{};
    double beamLen{0.0};
    ProfileSectionFrame sec{};
    ProfileSectionFrame colSec{};
    double aW{0.0};
    double aH{0.0};
    double halfExtent{0.0};
    double denom{0.0};
    double numer{0.0};
    double t{0.0};
    Vec3 facePoint{};
    bool ok{false};
    std::wstring error;
};

ShearTabPlacement resolveShearTabPlacement(const ComponentInput& input) noexcept {
    ShearTabPlacement p;
    if (input.secondary.empty()) {
        p.error = L"ikincil parca (kiris) secilmeli";
        return p;
    }
    const MemberGeometry& beamPick = input.secondary[0];

    // ROL DUZELTME: destek (kolon) DUSEY, kiris YATAY. Kullanici ters sirada
    // secebilir; dikeylik ile geometriden coz (secim sirasi sonucu degistirmez).
    const auto verticality = [](const MemberGeometry& g) {
        const Vec3 a{g.axisTo.x - g.axisFrom.x, g.axisTo.y - g.axisFrom.y,
                     g.axisTo.z - g.axisFrom.z};
        const double len = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
        return (len > 1e-9) ? std::fabs(a.z) / len : 0.0;  // 1 = dusey, 0 = yatay
    };
    p.rolesSwapped = verticality(input.main) + 1e-6 < verticality(beamPick);
    p.col = p.rolesSwapped ? beamPick : input.main;
    p.beam = p.rolesSwapped ? input.main : beamPick;

    // Kiris ekseni + baglanti ucu: kolona EN YAKIN uc.
    const Vec3 beamAxis{p.beam.axisTo.x - p.beam.axisFrom.x,
                        p.beam.axisTo.y - p.beam.axisFrom.y,
                        p.beam.axisTo.z - p.beam.axisFrom.z};
    p.beamLen = std::sqrt(beamAxis.x * beamAxis.x + beamAxis.y * beamAxis.y +
                          beamAxis.z * beamAxis.z);
    if (p.beamLen < 1e-6) {
        p.error = L"kiris ekseni belirlenemedi (sifir uzunluk)";
        return p;
    }
    p.colMid = Vec3{(p.col.axisFrom.x + p.col.axisTo.x) * 0.5,
                    (p.col.axisFrom.y + p.col.axisTo.y) * 0.5,
                    (p.col.axisFrom.z + p.col.axisTo.z) * 0.5};
    const auto dist2 = [&](const Vec3& q) {
        const double dx = q.x - p.colMid.x, dy = q.y - p.colMid.y, dz = q.z - p.colMid.z;
        return dx * dx + dy * dy + dz * dz;
    };
    const bool toNear = dist2(p.beam.axisTo) <= dist2(p.beam.axisFrom);
    p.connEnd = toNear ? p.beam.axisTo : p.beam.axisFrom;
    p.farEnd = toNear ? p.beam.axisFrom : p.beam.axisTo;

    // Levha uzunluk yonu = kolondan DISARI. Fin levha kolona KAYNAKLI, kiris
    // govdesine CIVATALI; duzlemi govdeye PARALEL. REFERANS NORMAL = DUNYA Z
    // (shear tab her zaman dusey levha).
    p.lenDir = vnorm(Vec3{p.farEnd.x - p.connEnd.x, p.farEnd.y - p.connEnd.y,
                          p.farEnd.z - p.connEnd.z});
    const Vec3 worldUp{0.0, 0.0, 1.0};
    p.sec = profileSectionFrame(p.lenDir, worldUp);
    if (std::fabs(vdot(p.lenDir, worldUp)) > 0.999)
        p.sec = profileSectionFrame(p.lenDir, p.beam.refNormal);

    // Kolon temas yuzu: kolon kesitinin lenDir yonundeki yari kapsami (destek
    // fonksiyonu; SectionAnchor ofseti dahil).
    const Vec3 colAxis{p.col.axisTo.x - p.col.axisFrom.x,
                       p.col.axisTo.y - p.col.axisFrom.y,
                       p.col.axisTo.z - p.col.axisFrom.z};
    const double colLen = std::sqrt(colAxis.x * colAxis.x + colAxis.y * colAxis.y +
                                    colAxis.z * colAxis.z);
    if (colLen > 1e-6) p.colSec = profileSectionFrame(vnorm(colAxis), p.col.refNormal);
    p.aW = vdot(p.lenDir, p.colSec.width);
    p.aH = vdot(p.lenDir, p.colSec.height);
    p.halfExtent = std::fabs(p.aW) * p.col.secWidth * 0.5 +
                   std::fabs(p.aH) * p.col.secHeight * 0.5 +
                   p.col.sectionCenterOffset * p.aH;

    // Temas noktasi: kiris ekseninin kolon yuz duzlemiyle kesisimi.
    p.beamDir = Vec3{beamAxis.x / p.beamLen, beamAxis.y / p.beamLen,
                     beamAxis.z / p.beamLen};
    p.denom = vdot(p.beamDir, p.lenDir);
    p.numer = p.halfExtent -
              vdot(Vec3{p.connEnd.x - p.colMid.x, p.connEnd.y - p.colMid.y,
                        p.connEnd.z - p.colMid.z}, p.lenDir);
    p.t = (std::fabs(p.denom) > 1e-9) ? p.numer / p.denom : 0.0;
    p.facePoint = Vec3{p.connEnd.x + p.beamDir.x * p.t,
                       p.connEnd.y + p.beamDir.y * p.t,
                       p.connEnd.z + p.beamDir.z * p.t};
    // SECTION ANCHOR DUZELTMESI (kiris): eksen kesit USTUNDEN gecerse levha
    // ust basliga oturur; govde merkezine kaydir.
    if (std::fabs(p.beam.sectionCenterOffset) > 1e-9) {
        p.facePoint = Vec3{p.facePoint.x + p.sec.height.x * p.beam.sectionCenterOffset,
                           p.facePoint.y + p.sec.height.y * p.beam.sectionCenterOffset,
                           p.facePoint.z + p.sec.height.z * p.beam.sectionCenterOffset};
    }
    p.ok = true;
    return p;
}

// Boslukla ayrilmis sayi listeleri (Tekla "space separated" alanlari).
std::vector<double> parseSpaceDoubles(const std::wstring& s) noexcept {
    std::vector<double> out;
    std::wstringstream ss(s);
    double v;
    while (ss >> v) out.push_back(v);
    return out;
}

std::vector<int> parseSpaceInts(const std::wstring& s) noexcept {
    std::vector<int> out;
    std::wstringstream ss(s);
    int v;
    while (ss >> v) out.push_back(v);
    return out;
}

} // namespace

GenerationPlan generateFinPlate(const ComponentInput& input) noexcept {
    GenerationPlan plan;
    const ShearTabPlacement pl = resolveShearTabPlacement(input);
    if (!pl.ok) {
        plan.ok = false;
        plan.error = pl.error;
        return plan;
    }
    const MemberGeometry& beam = pl.beam;
    const Vec3& lenDir = pl.lenDir;
    const ProfileSectionFrame& sec = pl.sec;
    const Vec3& facePoint = pl.facePoint;

    const double thickness = paramAsDouble(input.params, L"plateThickness", 10.0);
    const double width = paramAsDouble(input.params, L"plateWidth", 120.0);
    const double height = paramAsDouble(input.params, L"plateHeight", 240.0);
    const double boltD = paramAsDouble(input.params, L"boltDiameter", 20.0);
    const double holeD = paramAsDouble(input.params, L"holeDiameter", 22.0);
    const int boltCount = std::max(1, paramAsInt(input.params, L"boltCount", 2));
    const double edge = paramAsDouble(input.params, L"boltEdge", 40.0);
    const std::wstring grade = paramAsString(input.params, L"grade", L"8.8");

    // Levha: kalinlik sec.width (govdeye dik), uzunluk lenDir, yukseklik sec.height.
    PlateSpec plate;
    plate.axis = sec.width;
    plate.u = lenDir;
    plate.v = sec.height;
    plate.width = width;
    plate.height = height;
    plate.thickness = thickness;
    const Vec3 tabOffset{sec.width.x * thickness, sec.width.y * thickness,
                         sec.width.z * thickness};
    plate.center = Vec3{facePoint.x + lenDir.x * (width * 0.5) + tabOffset.x,
                        facePoint.y + lenDir.y * (width * 0.5) + tabOffset.y,
                        facePoint.z + lenDir.z * (width * 0.5) + tabOffset.z};
    plate.name = L"Fin Levhasi";
    plan.plates.push_back(plate);
    plan.plateKeys.push_back("fin_plate");

    BoltGroup b;
    b.axis = sec.width;
    b.xDir = lenDir;
    const double hh = height * 0.5;
    const Vec3 boltLine{facePoint.x + lenDir.x * (width * 0.5) + tabOffset.x,
                        facePoint.y + lenDir.y * (width * 0.5) + tabOffset.y,
                        facePoint.z + lenDir.z * (width * 0.5) + tabOffset.z};
    b.origin = Vec3{boltLine.x - sec.height.x * (hh - edge),
                    boltLine.y - sec.height.y * (hh - edge),
                    boltLine.z - sec.height.z * (hh - edge)};
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
    b.parts = {beam.id};
    plan.boltGroups.push_back(b);
    plan.boltGroupKeys.push_back("beam_bolts");

    // TESHIS (MM_FINPLATE_DIAG=1).
    if (std::getenv("MM_FINPLATE_DIAG") != nullptr) {
        std::ofstream os("model-maker-finplate-diag.log", std::ios::app);
        if (os) {
            const auto v3 = [](const Vec3& v) {
                std::ostringstream s;
                s << "(" << v.x << ", " << v.y << ", " << v.z << ")";
                return s.str();
            };
            os << "\n=== FIN PLATE DIAG ===\n";
            os << "rolesSwapped=" << (pl.rolesSwapped ? 1 : 0) << "\n";
            os << "col  id=" << pl.col.id << " from=" << v3(pl.col.axisFrom)
               << " to=" << v3(pl.col.axisTo) << " refN=" << v3(pl.col.refNormal)
               << " sec=" << pl.col.secWidth << "x" << pl.col.secHeight << "\n";
            os << "beam id=" << beam.id << " from=" << v3(beam.axisFrom)
               << " to=" << v3(beam.axisTo) << " refN=" << v3(beam.refNormal)
               << " sec=" << beam.secWidth << "x" << beam.secHeight << "\n";
            os << "params w=" << width << " h=" << height << " th=" << thickness
               << " bolts=" << boltCount << " edge=" << edge << "\n";
            os << "toNear=" << (pl.connEnd == pl.beam.axisTo ? 1 : 0)
               << " connEnd=" << v3(pl.connEnd) << " farEnd=" << v3(pl.farEnd)
               << " lenDir=" << v3(lenDir) << "\n";
            os << "sec.axis=" << v3(sec.axis) << " sec.width=" << v3(sec.width)
               << " sec.height=" << v3(sec.height) << "\n";
            os << "colSec.width=" << v3(pl.colSec.width) << " colSec.height=" << v3(pl.colSec.height)
               << " aW=" << pl.aW << " aH=" << pl.aH << " halfExtent=" << pl.halfExtent << "\n";
            os << "denom=" << pl.denom << " numer=" << pl.numer << " t=" << pl.t
               << " facePoint=" << v3(facePoint) << "\n";
            os << "plate center=" << v3(plate.center) << " axis=" << v3(plate.axis)
               << " u=" << v3(plate.u) << " v=" << v3(plate.v) << " w=" << plate.width
               << " h=" << plate.height << " th=" << plate.thickness << "\n";
            os << "bolts origin=" << v3(b.origin) << " axis=" << v3(b.axis)
               << " xDir=" << v3(b.xDir) << " count=" << b.countX << "x" << b.countY
               << " spacing=" << b.spacingX << "," << b.spacingY << "\n";
            os.flush();
        }
    }

    plan.ok = true;
    return plan;
}

void registerBuiltInComponents(ComponentRegistry& registry) noexcept {
    registry.registerType(endPlateComponentType(), &generateEndPlate);
    registry.registerType(finPlateComponentType(), &generateFinPlate);
    registry.registerType(shearPlate146ComponentType(), &generateShearPlateSimple146);
    registry.registerType(haunch40ComponentType(), &generateHaunch40);
}

// --- Tekla 146 (Shear plate simple) -------------------------------------------

ComponentType shearPlate146ComponentType() noexcept {
    // 146 core = fin plate core (tekli ortogonal shear tab) + Tekla 146'nin
    // dogrulanabilir yuzeyi: cift tab, kiris ucu kare kesimi + beam gap, bulon
    // grubu yerlesimi (degisken aralik/olcu/silinen), stiffener, backing bar.
    // DESTEKLENMEYEN (kernel'de karsiligi yok): notch/flange strip/weld access
    // hole/seat angle/slotted hole/chamfer/shearTabOrientation/plateWasher —
    // bunlar blueprint/coverage'ta documented_only olarak durur, uydurulmaz.
    ComponentType t = finPlateComponentType();
    t.typeId = L"tekla_shear_plate_146";
    t.schemaVersion = 1;
    t.displayName = L"Shear Plate Simple (146)";
    t.params = {
        // --- Plates tab (tab plate) ---
        lengthParam(L"plateThickness", L"mm", 10.0, 4.0, 60.0, L"levha kalinligi"),
        lengthParam(L"plateWidth", L"mm", 120.0, 40.0, 400.0, L"levha genisligi"),
        lengthParam(L"plateHeight", L"mm", 240.0, 60.0, 800.0, L"levha yuksekligi"),
        // --- Plates tab (tab position) ---
        enumParam(L"shearTabPosition", L"near_side",
                  {L"near_side", L"far_side", L"near_and_far"}, L"tab tarafi (tek/cift)"),
        lengthParam(L"tabGapSecondary", L"mm", 0.0, 0.0, 100.0,
                    L"tab ile kiris govdesi arasi bosluk"),
        // --- Picture tab (beam cut) ---
        lengthParam(L"beamGap", L"mm", 20.0, 0.0, 200.0, L"kiris ucu ile kolon yuzu arasi bosluk"),
        enumParam(L"beamEndCut", L"square", {L"square", L"none"}, L"kiris ucu kesimi"),
        // --- Bolts tab ---
        lengthParam(L"boltDiameter", L"mm", 20.0, 12.0, 30.0, L"bulon capi"),
        // 0 = bulon katalogundan (standardBoltHoleDiameter); tolerance>0 ise
        // cap + tolerance. (Eski sabit 22 mm bulon capi degisince delikten kucuk
        // kalabiliyordu.)
        lengthParam(L"holeDiameter", L"mm", 0.0, 0.0, 60.0,
                    L"delik capi (0 = bulon katalogu; tolerans>0 ise bulon capi+tolerans)"),
        intParam(L"boltCount", 4, 1, 12, L"bulon adedi (sira; yukseklik yonu)"),
        intParam(L"boltCountX", 1, 1, 12, L"bulon kolon adedi (genislik yonu)"),
        lengthParam(L"pitchX", L"mm", 0.0, 0.0, 2000.0, L"kolon araligi (0 = auto)"),
        lengthParam(L"pitchY", L"mm", 0.0, 0.0, 2000.0, L"sira araligi (0 = boltSpacing/auto)"),
        lengthParam(L"edgeX", L"mm", 0.0, 0.0, 2000.0, L"genislik kenar mesafesi (0 = boltEdge)"),
        lengthParam(L"edgeY", L"mm", 0.0, 0.0, 2000.0, L"yukseklik kenar mesafesi (0 = boltEdge)"),
        // NC1 source-verified asymmetric group placement (zero for all legacy components).
        lengthParam(L"boltGroupOffsetX", L"mm", 0.0, -1000.0, 1000.0,
                    L"bulon grubu yatay merkez kaymasi (NC1)"),
        lengthParam(L"boltGroupOffsetY", L"mm", 0.0, -1000.0, 1000.0,
                    L"bulon grubu dusey merkez kaymasi (NC1)"),
        lengthParam(L"boltEdge", L"mm", 40.0, 10.0, 200.0, L"kenar mesafesi"),
        enumParam(L"grade", L"8.8", {L"8.8", L"10.9"}, L"bulon sinifi"),
        stringParam(L"boltSpacing", L"auto", L"bulon araliklari (mm, boslukla; 'auto' = turet)"),
        enumParam(L"boltVerticalMeasure", L"middle", {L"top", L"middle", L"below"},
                  L"bulon grubunun dusey konum referansi"),
        lengthParam(L"boltVerticalPos", L"mm", 0.0, -1000.0, 1000.0,
                    L"bulon grubunun dusey kaymasi"),
        // Yatay konum: kiris ucundan (veya kolon yuzunden) ilk bulon kolonuna mesafe.
        // 0 = grup levha genisliginde ortalanir (eski davranis).
        lengthParam(L"boltHorizontalPos", L"mm", 0.0, 0.0, 2000.0,
                    L"yatay konum: ilk bulon kolonuna mesafe (0 = levhada ortali)"),
        enumParam(L"boltHorizRef", L"Default", {L"Default", L"Secondary part", L"Main part web"},
                  L"yatay olcu referansi"),
        enumParam(L"boltVertRef", L"Default",
                  {L"Default", L"Secondary part (sloped)", L"Secondary part", L"Main part",
                   L"Main part center line"},
                  L"dusey olcu referansi"),
        stringParam(L"deletedBolts", L"", L"silinecek bulon numaralari (1-based, boslukla)"),
        // Staggering (yardim sayfasi ikonlari) + Bolt group orientation.
        enumParam(L"boltStagger", L"Default",
                  {L"Default", L"Not staggered", L"Staggered type 1", L"Staggered type 2"},
                  L"bulon staggering (tip 1/2)"),
        enumParam(L"boltStaggerExtra", L"Default",
                  {L"Default", L"Staggered type 3", L"Staggered type 4"},
                  L"bulon staggering (tip 3/4)"),
        enumParam(L"boltGroupOrientation", L"Default",
                  {L"Default", L"Automatic", L"Staggered", L"Square", L"Sloped"},
                  L"bulon grubu yonelimi"),
        // --- Tekla 146 "Bolt properties" (resimli diyalogdaki alanlar) -------
        // Tekla diyalogunda her satirin solunda "Default" onay kutusu vardir;
        // isaretsiz birakilan alan varsayilani kullanir (optional=true).
        enumParam(L"boltStandard", L"Default",
                  {L"Default", L"ISO 4014", L"ISO 4032", L"ISO 7412"}, L"bulon standardi"),
        lengthParam(L"tolerance", L"mm", 0.0, 0.0, 10.0,
                    L"delik toleransi (delik capi = bulon capi + tolerans; 0 = katalog)"),
        boolParam(L"threadInMat", true, L"dis bulonlu parcada (thread in mat)"),
        enumParam(L"boltSiteWorkshop", L"Default", {L"Default", L"Site", L"Workshop"},
                  L"montaj yeri (atolye/saha)"),
        enumParam(L"boltingDirection", L"Default",
                  {L"Default", L"Bolting direction 1", L"Bolting direction 2"},
                  L"bulonlama yonu (bas tarafi)"),
        stringParam(L"comment", L"", L"aciklama"),
        // Cut length: bulonun parcalari ARADIGI derinlik (mm). 0 = Tekla varsayilani
        // (tum yigin). Bulon baslangicindan bu mesafeden uzak parcalar delinmez.
        lengthParam(L"cutLength", L"mm", 0.0, 0.0, 5000.0, L"kesme (arama) uzunlugu, 0 = tumu"),
        lengthParam(L"boltLengthIncrease", L"mm", 0.0, 0.0, 200.0, L"bulon boyu artisi"),
        // Slotted holes: delik tipi + yuva olculeri. slotV = dusey, slotH = yatay
        // olcu (delik capina EKLENEN uzunluk); oversized'ta slotH = buyutme payi.
        enumParam(L"holeType", L"Default",
                  {L"Default", L"Oversized", L"Slotted", L"Tapped", L"No hole"}, L"delik tipi"),
        lengthParam(L"slotV", L"mm", 0.0, 0.0, 500.0, L"yuva: dusey olcu (1)"),
        lengthParam(L"slotH", L"mm", 0.0, 0.0, 500.0, L"yuva: yatay olcu / oversized payi (2)"),
        enumParam(L"rotateSlots", L"Default", {L"Default", L"No", L"Yes"}, L"yuvalari dondur"),
        enumParam(L"slotsIn", L"Default", {L"Default", L"Plate", L"Beam"},
                  L"yuvalar hangi parcada"),
        // --- Stiffeners tab ---
        // Tekla Stiffeners sekmesi (karsi-govde berkitme). Default/No stiffeners = yok.
        enumParam(L"stiffenerCreation", L"Default",
                  {L"Default", L"Full", L"Determined by shear tab", L"Partial", L"No stiffeners"},
                  L"karsi govde berkitmesi"),
        enumParam(L"stiffenerOrientation", L"Default",
                  {L"Default", L"Perpendicular to main part", L"Parallel to secondary part"},
                  L"berkitme yonelimi"),
        lengthParam(L"stiffenerT", L"mm", 0.0, 0.0, 60.0, L"berkitme kalinligi t (0 = tab kalinligi)"),
        lengthParam(L"stiffenerW", L"mm", 0.0, 0.0, 1000.0, L"berkitme genisligi b (0 = otomatik)"),
        lengthParam(L"stiffenerH", L"mm", 0.0, 0.0, 2000.0, L"berkitme yuksekligi h (0 = otomatik)"),
        lengthParam(L"stiffenerGapFlange", L"mm", 0.0, 0.0, 200.0, L"berkitme-flans boslugu (1)"),
        lengthParam(L"stiffenerGapWeb", L"mm", 10.0, 0.0, 200.0, L"berkitme-govde boslugu (2)"),
        enumParam(L"stiffenerChamferType", L"Default",
                  {L"Default", L"No chamfer", L"Line chamfer", L"Convex arc chamfer",
                   L"Concave arc chamfer"},
                  L"berkitme pah tipi"),
        lengthParam(L"stiffenerChamferH", L"mm", 0.0, 0.0, 200.0, L"pah yatay olcu (1)"),
        lengthParam(L"stiffenerChamferV", L"mm", 0.0, 0.0, 200.0, L"pah dusey olcu (2)"),
        stringParam(L"stiffenerPosPrefix", L"", L"berkitme Pos_No oneki"),
        intParam(L"stiffenerPosStart", 1, 0, 100000, L"berkitme Pos_No baslangic"),
        stringParam(L"stiffenerMaterial", L"", L"berkitme malzemesi"),
        stringParam(L"stiffenerName", L"", L"berkitme adi"),
        stringParam(L"stiffenerFinish", L"", L"berkitme yuzey islemi"),
        // --- Beam cut tab ---
        lengthParam(L"backingBarT", L"mm", 0.0, 0.0, 60.0, L"kaynak arkalik cubugu kalinligi"),
    };
    t.catalog.sourceComponentNumber = L"146";
    t.catalog.category = L"Shear connection";
    t.catalog.primaryMemberType = L"kolon";
    t.catalog.secondaryMemberType = L"kiris";
    t.catalog.supportedFeatures = {L"kayma levhasi (tek/cift)", L"bulon grubu (degisken aralik)",
                                   L"kiris ucu kesimi", L"berkitme (stiffener)",
                                   L"kaynak arkalik cubugu"};
    t.catalog.coverageStatus = L"PARTIALLY_IMPLEMENTED";
    t.catalog.pickCount = 2;
    t.catalog.catalogVisible = true;
    return t;
}

GenerationPlan generateShearPlateSimple146(const ComponentInput& input) noexcept {
    GenerationPlan plan;
    const ShearTabPlacement pl = resolveShearTabPlacement(input);
    if (!pl.ok) {
        plan.ok = false;
        plan.error = pl.error;
        return plan;
    }
    const MemberGeometry& beam = pl.beam;
    const Vec3& lenDir = pl.lenDir;
    const ProfileSectionFrame& sec = pl.sec;
    const Vec3& facePoint = pl.facePoint;
    const Vec3& colMid = pl.colMid;
    const Vec3 worldUp{0.0, 0.0, 1.0};

    // --- parametreler ---
    const double thickness = paramAsDouble(input.params, L"plateThickness", 10.0);
    double width = paramAsDouble(input.params, L"plateWidth", 120.0);
    const double height = paramAsDouble(input.params, L"plateHeight", 240.0);
    const double boltD = paramAsDouble(input.params, L"boltDiameter", 20.0);
    // Delik capi: tolerance>0 -> bulon capi + tolerans (Tekla "Tolerance: gap between
    // the bolt and the hole"); degilse holeDiameter parametresi; 0 ise bulon katalogu.
    // Delik bulondan kucuk olamaz (bulon capi degisip holeDiameter eski kaldiysa).
    const double tolerance = std::max(0.0, paramAsDouble(input.params, L"tolerance", 0.0));
    const double holeParam = paramAsDouble(input.params, L"holeDiameter", 0.0);
    double holeD = (holeParam > 0.0) ? holeParam : standardBoltHoleDiameter(boltD);
    if (tolerance > 0.0) holeD = boltD + tolerance;
    if (holeD < boltD) holeD = standardBoltHoleDiameter(boltD);
    const int boltCount = std::max(1, paramAsInt(input.params, L"boltCount", 4));
    const double edge = paramAsDouble(input.params, L"boltEdge", 40.0);
    // COK KOLONLU BULON (countX x countY): countX kolon genislik (lenDir) yonunde,
    // countY sira yukseklik (sec.height) yonunde. pitchX/pitchY bagimsiz aralik,
    // edgeX/edgeY bagimsiz kenar mesafesi. 0 degerleri boltEdge'den/auto'dan turer
    // (geriye donuk uyum — tek kolonlu eski baglantilar degismez).
    const int countX = std::clamp(paramAsInt(input.params, L"boltCountX", 1), 1, 12);
    const double pitchX = std::max(0.0, paramAsDouble(input.params, L"pitchX", 0.0));
    const double pitchY = std::max(0.0, paramAsDouble(input.params, L"pitchY", 0.0));
    const double edgeX = std::max(0.0, paramAsDouble(input.params, L"edgeX", 0.0));
    const double edgeY = std::max(0.0, paramAsDouble(input.params, L"edgeY", 0.0));
    const double effEdgeX = edgeX > 0.0 ? edgeX : edge;
    const double effEdgeY = edgeY > 0.0 ? edgeY : edge;
    const double boltGroupOffsetX = paramAsDouble(input.params, L"boltGroupOffsetX", 0.0);
    const double boltGroupOffsetY = paramAsDouble(input.params, L"boltGroupOffsetY", 0.0);
    const std::wstring grade = paramAsString(input.params, L"grade", L"8.8");
    // Bolts tab: Tekla "Bolt properties", yon, yuva, uzunluk artisi, cut length.
    const std::wstring boltStandard = paramAsString(input.params, L"boltStandard", L"Default");
    const bool threadInMat = paramAsBool(input.params, L"threadInMat", true);
    const std::wstring siteWorkshop = paramAsString(input.params, L"boltSiteWorkshop", L"Default");
    const int boltDirection =
        (paramAsString(input.params, L"boltingDirection", L"Default") == L"Bolting direction 2") ? 2 : 1;
    const std::wstring boltComment = paramAsString(input.params, L"comment", L"");
    const double cutLength = std::max(0.0, paramAsDouble(input.params, L"cutLength", 0.0));
    const double boltLengthIncrease =
        std::max(0.0, paramAsDouble(input.params, L"boltLengthIncrease", 0.0));
    const std::wstring holeType = paramAsString(input.params, L"holeType", L"Default");
    const double slotV = std::max(0.0, paramAsDouble(input.params, L"slotV", 0.0));
    const double slotH = std::max(0.0, paramAsDouble(input.params, L"slotH", 0.0));
    const std::wstring rotateSlots = paramAsString(input.params, L"rotateSlots", L"Default");
    const std::wstring slotsIn = paramAsString(input.params, L"slotsIn", L"Default");

    const std::wstring tabPos = paramAsString(input.params, L"shearTabPosition", L"near_side");
    const double tabGap = paramAsDouble(input.params, L"tabGapSecondary", 0.0);
    const double beamGap = paramAsDouble(input.params, L"beamGap", 20.0);
    const std::wstring endCut = paramAsString(input.params, L"beamEndCut", L"square");
    const std::wstring stiffener = paramAsString(input.params, L"stiffenerCreation", L"Default");
    const double stiffGapWeb = std::max(0.0, paramAsDouble(input.params, L"stiffenerGapWeb", 10.0));
    const double stiffGapFlange = std::max(0.0, paramAsDouble(input.params, L"stiffenerGapFlange", 0.0));
    const std::wstring boltSpacing = paramAsString(input.params, L"boltSpacing", L"auto");
    const std::wstring boltMeasure = paramAsString(input.params, L"boltVerticalMeasure", L"middle");
    const double boltVPos = paramAsDouble(input.params, L"boltVerticalPos", 0.0);
    const std::wstring delBolts = paramAsString(input.params, L"deletedBolts", L"");
    const double backingBarT = paramAsDouble(input.params, L"backingBarT", 0.0);
    // Kaynaklar: levha kolona kaynaklanir (kiris levhaya BULONLANIR — kaynak yok).
    // Diyalogdaki \"Welds\" tablosunun onay/size durumu createWelds/weldSize ile
    // generatora tasinir (openComponentParamsDialog resultWeldRows -> param).
    const bool createWelds = paramAsBool(input.params, L"createWelds", true);
    const double weldSize = paramAsDouble(input.params, L"weldSize", 5.0);
    const bool weldDoubleSided = paramAsBool(input.params, L"weldDoubleSided", false);

    // --- bulon grubu konum referanslari (Tekla Bolt group dimensions 1 ve 5) -----
    // Yatay (1): kiris ucundan (varsayilan, kare kesimde kolon yuzu + beamGap) ya da
    // ana parca govdesinden ilk bulon kolonuna mesafe. 0 = grup levhada ortali.
    // Dusey (5): bu surum yalniz ikincil parcaya (kiris) gore olcer; ana parcaya gore
    // olcu egimli / kiris-kiris durumunda anlamli oldugundan SESSIZCE yok sayilmaz,
    // acik hata verilir (fail-closed).
    const double hPos = std::max(0.0, paramAsDouble(input.params, L"boltHorizontalPos", 0.0));
    const std::wstring hRef = paramAsString(input.params, L"boltHorizRef", L"Default");
    const std::wstring vRef = paramAsString(input.params, L"boltVertRef", L"Default");
    if (vRef == L"Main part" || vRef == L"Main part center line") {
        plan.ok = false;
        plan.error = L"boltVertRef '" + vRef +
                     L"': ana parcaya gore dusey olcu bu surumde yok (yalniz 'Secondary part')";
        return plan;
    }
    // Staggering (Tekla icon secenekleri; yardim sayfasi resimlerinden):
    //  1: 1. ve 3. (tek) sira bir kolon eksik, yarim aralik saga kayik (3-4-3)
    //  2: 2. (cift) sira bir kolon eksik, yarim aralik saga kayik (4-3-4)
    //  3: tek siralar yarim aralik saga (zikzak)   4: cift siralar yarim aralik saga
    //  5: Bolt group orientation = Staggered: tek kolonlar yarim dusey aralik yukari
    // Siralar USTTEN sayilir (ikonlarda 1. sira ustte). Sloped: egimli kiris yok.
    const std::wstring staggerSel = paramAsString(input.params, L"boltStagger", L"Default");
    const std::wstring staggerExtra = paramAsString(input.params, L"boltStaggerExtra", L"Default");
    const std::wstring orientSel = paramAsString(input.params, L"boltGroupOrientation", L"Default");
    int staggerMode = 0;
    if (staggerExtra == L"Staggered type 3") staggerMode = 3;
    else if (staggerExtra == L"Staggered type 4") staggerMode = 4;
    else if (staggerSel == L"Staggered type 1") staggerMode = 1;
    else if (staggerSel == L"Staggered type 2") staggerMode = 2;
    else if (orientSel == L"Staggered") staggerMode = 5;
    if (orientSel == L"Sloped") {
        plan.ok = false;
        plan.error = L"boltGroupOrientation 'Sloped': egimli ikincil parca bu surumde yok";
        return plan;
    }
    double xFromFace = -1.0;  // <0: ortali (eski davranis)
    if (hPos > 0.0) {
        if (hRef == L"Main part web") {
            xFromFace = hPos;
        } else {
            if (endCut != L"square") {
                plan.ok = false;
                plan.error = L"boltHorizontalPos kiris ucuna gore olculur: Beam end cut = square olmali";
                return plan;
            }
            xFromFace = beamGap + hPos;
        }
    }
    // Bulon grubu levha genisligine sigmazsa levha buyutulur (Tekla: bulon grubu
    // olculeri levha boyutunu belirler). Yalniz yatay konum verildiginde.
    if (xFromFace >= 0.0) {
        double spanPre = 0.0;
        if (countX > 1)
            spanPre = (pitchX > 0.0) ? static_cast<double>(countX - 1) * pitchX
                                     : std::max(0.0, width - xFromFace - effEdgeX);
        double staggerPre = 0.0;
        if (staggerMode == 3 || staggerMode == 4) {
            const double pitchPre =
                countX > 1 ? ((pitchX > 0.0) ? pitchX : spanPre / static_cast<double>(countX - 1))
                           : pitchX;
            staggerPre = 0.5 * pitchPre;
        }
        width = std::max(width, xFromFace + spanPre + staggerPre + effEdgeX);
    }

    // --- tab taraf ---
    const bool nearTab = (tabPos == L"near_side" || tabPos == L"near_and_far");
    const bool farTab = (tabPos == L"far_side" || tabPos == L"near_and_far");
    // Eksantrisite: near tab merkezi is cizgisinden (govde merkezi) +sec.width*(t+gap);
    // far tab -sec.width*(t+gap). t = kalinlik (yuze t/2 + yari kalinlik).
    const double ecc = thickness + tabGap;
    const Vec3 tabBase{facePoint.x + lenDir.x * (width * 0.5),
                       facePoint.y + lenDir.y * (width * 0.5),
                       facePoint.z + lenDir.z * (width * 0.5)};

    const auto tabPlate = [&](double sign) {
        PlateSpec p;
        p.axis = sec.width;
        p.u = lenDir;
        p.v = sec.height;
        p.width = width;
        p.height = height;
        p.thickness = thickness;
        p.center = Vec3{tabBase.x + sec.width.x * (sign * ecc),
                        tabBase.y + sec.width.y * (sign * ecc),
                        tabBase.z + sec.width.z * (sign * ecc)};
        p.name = (sign > 0.0) ? L"Shear Tab (near)" : L"Shear Tab (far)";
        return p;
    };
    if (nearTab) {
        plan.plates.push_back(tabPlate(+1.0));
        plan.plateKeys.push_back("shear_tab_near");
    }
    if (farTab) {
        plan.plates.push_back(tabPlate(-1.0));
        plan.plateKeys.push_back("shear_tab_far");
    }

    // --- kaynaklar (levha -> kolon) -----------------------------------------
    // 146 kayma levhasi kolona FILLET kaynaklanir; kiris levhaya BULONLANIR.
    // Her uretilen tab icin bir kaynak: partA = kolon (main, EntityId bilinir),
    // partB = levha (owned child, roleKey ile isaretlenir; commit'te cozulur).
    if (createWelds) {
        const auto addPlateWeld = [&](const char* weldKey, const char* plateRole) {
            WeldSpec w;
            w.partA = input.main.id;   // kolon (ana parca)
            w.partBRole = plateRole;   // levha roleKey'i (commit'te EntityId)
            w.size = weldSize;
            w.doubleSided = weldDoubleSided;
            w.name = L"Kaynak: levha - kolon";
            plan.welds.push_back(w);
            plan.weldKeys.push_back(weldKey);
        };
        if (nearTab) addPlateWeld("weld_tab_near", "shear_tab_near");
        if (farTab) addPlateWeld("weld_tab_far", "shear_tab_far");
    }

    // --- bulon grubu (kiriş gövdesini deler; malzemelemede tüm owned levhalar
    //     başa eklenir) ---
    BoltGroup b;
    // EKSEN ISARETI (kalici): BoltGroup'un yerel cercevesi v = axis x xDir olarak
    // kurulur (sag-el). Plaka cercevesi ise sec.height = lenDir x sec.width'dir.
    // Bu yuzden axis = sec.width iken v = sec.width x lenDir = -sec.height olur ve
    // bulonlar YUKARI degil ASAGI dizilir (latent isaret hatasi; testler yalniz
    // origin.z kontrol ettigi icin yakalanmadi). axis'i NEGATIFLEMEK v = +sec.height
    // yapar: bulonlar govde boyunca yukari (dusey +Z) dizilir, origin alt satir
    // (j=0) olur — boltGroupBoltOrigins'in "j=0 en alt satir" sozlesmesiyle tutar.
    b.axis = Vec3{-sec.width.x, -sec.width.y, -sec.width.z};
    b.xDir = lenDir;      // grup x ekseni = levha genislik yonu
    b.countX = countX;
    b.countY = boltCount;

    // Yatay (genislik) aciklik/aralik: countX==1 -> 0 (tek kolon merkezli).
    // pitchX verilmisse sabit aralik; yoksa genislikten 2*effEdgeX dusulur.
    double spanX = 0.0;
    double spacingX = 0.0;
    if (countX > 1) {
        if (pitchX > 0.0) {
            spacingX = pitchX;
            spanX = static_cast<double>(countX - 1) * pitchX;
        } else {
            spanX = std::max(0.0, width - (xFromFace >= 0.0 ? xFromFace : effEdgeX) - effEdgeX);
            spacingX = spanX / static_cast<double>(countX - 1);
        }
    }
    b.spacingX = spacingX;
    double staggerXOffset = 0.0;
    if (staggerMode == 3 || staggerMode == 4) {
        const double pitchEff = (countX > 1) ? spacingX : pitchX;
        if (!(pitchEff > 0.0)) {
            plan.ok = false;
            plan.error = L"Staggered type 3/4: kolon araligi gerekli (boltCountX>1 veya pitchX>0)";
            return plan;
        }
        staggerXOffset = 0.5 * pitchEff;
    }

    // Dusey (yukseklik) aciklik/aralik: pitchY verilmisse sabit; yoksa mevcut
    // boltSpacing (auto veya degisken liste) mantigi.
    const bool autoSpacing = (boltSpacing == L"auto");
    double spanY = 0.0;
    if (pitchY > 0.0) {
        b.spacingY = pitchY;
        spanY = static_cast<double>(boltCount - 1) * pitchY;
    } else if (autoSpacing) {
        b.spacingY = (boltCount > 1)
                         ? (height - 2.0 * effEdgeY) / static_cast<double>(boltCount - 1)
                         : 0.0;
        spanY = (boltCount > 1) ? (height - 2.0 * effEdgeY) : 0.0;
    } else {
        const std::vector<double> vals = parseSpaceDoubles(boltSpacing);
        b.spacingYs.resize(static_cast<std::size_t>(std::max(0, boltCount - 1)));
        for (int i = 0; i < boltCount - 1; ++i) {
            const double v = (i < static_cast<int>(vals.size()) && vals[static_cast<std::size_t>(i)] > 0.0)
                                 ? vals[static_cast<std::size_t>(i)]
                                 : (vals.empty() ? 0.0 : vals.back());
            b.spacingYs[static_cast<std::size_t>(i)] = v;
            spanY += v;
        }
    }
    // Staggered (orientation) : tek kolonlar yarim dusey aralik yukari kayar -> grup
    // dusey acikligi o kadar artar.
    double staggerYOffset = 0.0;
    if (staggerMode == 5) {
        const double pitchEffY =
            (b.spacingY > 0.0) ? b.spacingY : (b.spacingYs.empty() ? 0.0 : b.spacingYs.front());
        if (!(pitchEffY > 0.0)) {
            plan.ok = false;
            plan.error = L"Bolt group orientation 'Staggered': sira araligi gerekli (boltCount>1)";
            return plan;
        }
        staggerYOffset = 0.5 * pitchEffY;
        spanY += staggerYOffset;
    }
    b.staggerMode = staggerMode;
    b.staggerOffset = (staggerMode == 5) ? staggerYOffset : staggerXOffset;
    // Dusey konum: measure top/middle/below + boltVerticalPos. Referans nokta
    // tab merkezi (govde merkezi = tabBase); sec.height yonu = dusey.
    const double hh = height * 0.5;
    double originOffset = -spanY * 0.5;  // middle (varsayilan)
    if (boltMeasure == L"top") {
        originOffset = (hh - effEdgeY) - spanY;   // ust bulon tabTop - edge'de
    } else if (boltMeasure == L"below") {
        originOffset = -(hh - effEdgeY);          // alt bulon tabBottom + edge'de
    }
    originOffset += boltVPos + boltGroupOffsetY;
    // Yatay konum: kolon grubu genislikte MERKEZLENIR (origin = sol-alt bulon).
    const double xOffset = -(spanX + staggerXOffset) * 0.5;
    // BULON EKSEN-YONU KONUMU + GRIP (display): bulonlar levhayi + KIRIS GOVDESINI
    // deler. Yigin = delinen parcalarin eksen (sec.width) boyunca kapladigi aralik;
    // bulon gorseli bu araligin MERKEZINE oturur ki bas/somun yiginin iki dis
    // yuzune otursun. Eksen-yonu kayma YALNIZ display'i etkiler; delikler
    // through-cut (holeDepth=0) oldugundan konumlari degismez. bbox yontemi
    // (Document::boltGripLength) kiris flans genisligini (300 mm) grip'e sokup
    // civatayi gercek disi uzatirdi; yerine yigin boyu kullanilir.
    // secWebThickness bilinmiyorsa (0) eski davranis korunur (origin tabBase'de,
    // gripLength 0 -> commit'te bbox fallback).
    //
    // CUT LENGTH (Tekla): bulon, baslangic (bas) yuzunden itibaren yalniz bu
    // mesafe icindeki parcalari deler. 0 = tum yigin. Kapsam disi kalan KIRIS
    // delinmez (bolt.parts bos); kapsam disi kalan LEVHA acik hata verir
    // (levhalar materializer'da otomatik eklenir, buradan cikarilamaz).
    double boltAxisShift = 0.0;
    bool beamDrilled = true;
    if (beam.secWebThickness > 0.0) {
        const double t2 = thickness * 0.5;
        const double w2 = beam.secWebThickness * 0.5;
        struct Span { double lo, hi; bool isBeam; bool isPlate; };
        std::vector<Span> spans;
        spans.push_back({-w2, w2, true, false});
        if (nearTab) spans.push_back({ecc - t2, ecc + t2, false, true});
        if (farTab) spans.push_back({-ecc - t2, -ecc + t2, false, true});
        if (cutLength > 0.0) {
            // bas yuzu: yonde 1 -> +sec.width ucu (bulon ekseni = -sec.width, bas
            // -eksen tarafinda), yonde 2 -> -sec.width ucu.
            const bool headHigh = (boltDirection != 2);
            double wStart = headHigh ? -1.0e18 : 1.0e18;
            for (const auto& sp : spans)
                wStart = headHigh ? std::max(wStart, sp.hi) : std::min(wStart, sp.lo);
            std::vector<Span> kept;
            for (const auto& sp : spans) {
                const double d = headHigh ? (wStart - sp.hi) : (sp.lo - wStart);
                if (d < cutLength) {
                    kept.push_back(sp);
                } else if (sp.isPlate) {
                    plan.ok = false;
                    plan.error = L"cutLength (" + std::to_wstring(static_cast<int>(cutLength)) +
                                 L" mm) levhayi kapsamiyor: bulon yigini daha uzun";
                    return plan;
                } else if (sp.isBeam) {
                    beamDrilled = false;
                }
            }
            spans = std::move(kept);
        }
        double lo = 1.0e18, hi = -1.0e18;
        for (const auto& sp : spans) {
            lo = std::min(lo, sp.lo);
            hi = std::max(hi, sp.hi);
        }
        b.gripLength = hi - lo;
        boltAxisShift = 0.5 * (hi + lo);
    }
    // Yatay konum: ilk bulon kolonu kolon yuzunden xFromFace (varsa) ya da grup
    // levha genisliginde ortali (varsayilan).
    const double xFromFaceFinal = ((xFromFace >= 0.0) ? xFromFace
                                                             : (width * 0.5 + xOffset))
                                  + boltGroupOffsetX;
    b.origin = Vec3{facePoint.x + sec.width.x * boltAxisShift + lenDir.x * xFromFaceFinal + sec.height.x * originOffset,
                    facePoint.y + sec.width.y * boltAxisShift + lenDir.y * xFromFaceFinal + sec.height.y * originOffset,
                    facePoint.z + sec.width.z * boltAxisShift + lenDir.z * xFromFaceFinal + sec.height.z * originOffset};
    b.deletedBolts = parseSpaceInts(delBolts);
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
    // --- Tekla "Bolt basic properties" / yon / yuva (boltStandard..slotsIn) ---
    {
        const auto narrowStr = [](const std::wstring& w) {
            std::string n;
            n.assign(w.begin(), w.end());
            return n;
        };
        b.params.standard = (boltStandard == L"Default") ? std::string() : narrowStr(boltStandard);
        b.params.threadInMat = threadInMat;
        b.params.siteWorkshop = (siteWorkshop == L"Workshop") ? 1 : 0;
        b.params.direction = boltDirection;
        b.params.lengthIncrease = boltLengthIncrease;
        b.params.cutLength = cutLength;
        b.params.comment = narrowStr(boltComment);
    }
    // --- Delik tipi (Hole type) + Slots in ---
    b.createHoles = (holeType != L"No hole");
    {
        // Slots in: Default = kayma levhasi, Beam = kiris govdesi.
        const int target = (slotsIn == L"Beam") ? 2 : 1;
        if (holeType == L"Oversized" && slotH > 0.0) {
            b.holeShapeTarget = target;
            b.shapeHoleDiameter = holeD + slotH;  // slotH = buyutme payi
        } else if (holeType == L"Slotted" && (slotV > 0.0 || slotH > 0.0)) {
            b.holeShapeTarget = target;
            b.shapeHoleDiameter = holeD;
            const bool rot = (rotateSlots == L"Yes");
            b.slotAlong = rot ? slotH : slotV;   // slotDir = dusey (sec.height)
            b.slotAcross = rot ? slotV : slotH;
            b.slotDir = sec.height;
        } else if (holeType == L"Tapped") {
            b.holeShapeTarget = target;
            b.shapeHoleDiameter = boltD;          // disli delik = nominal cap
        }
    }
    if (beamDrilled) b.parts = {beam.id};
    else b.parts.clear();
    plan.boltGroups.push_back(b);
    plan.boltGroupKeys.push_back("beam_bolts");

    // --- kiris ucu kare kesimi (beam end cut) ---
    if (endCut == L"square") {
        CutModifier cut;
        cut.kind = CutKind::PlaneCut;
        cut.target = beam.id;
        cut.planeOrigin = Vec3{facePoint.x + lenDir.x * beamGap,
                               facePoint.y + lenDir.y * beamGap,
                               facePoint.z + lenDir.z * beamGap};
        cut.planeNormal = lenDir;
        cut.keepPositive = true;  // korunan taraf = kiris govdesi (uzak taraf)
        cut.hidden = true;        // component ic motor elemani — kesme duzlemi cizilmez/secilmez
        cut.name = "Beam end cut (square)";
        plan.cuts.push_back(cut);
        plan.cutKeys.push_back("cut_beam_end");
    }

    // --- STIFFENER (Tekla "opposite web stiffener") ---------------------------
    // Ana parcanin GOVDESININ, kayma levhasina GORE KARSI tarafinda duran berkitme.
    // Kesit cercevesi (colSec): W = govde normali (flans genisligi yonu), H = govde
    // yonu (kesit yuksekligi). Govde W in [-tw/2, tw/2], flanslar H = +-(h/2-tf..h/2).
    //  - Kiris govdeye dik gelirse (|lenDir.W| > |lenDir.H|, "Case B"): karsi taraf
    //    -lenDir; kiris flans yuzune gelirse (Case A): karsi taraf levhanin yanal
    //    ofsetinin tersi.
    //  - Orientation: Default / "Parallel to secondary part" = DUSEY levha (kayma
    //    levhasi duzleminde, tabin hizasinda); "Perpendicular to main part" = ana
    //    parca eksenine DIK (yatay) sureklilik levhasi, flanslar arasinda.
    //  - Gap 1 = flanslara, Gap 2 = govdeye bosluk (Tekla yardim sayfasi resmi).
    //  - Creation: Full = kiris govdesinin temiz derinligi; Determined by shear tab =
    //    tab yuksekligi (alt kenarlar hizali); Partial = ust Full ile, alt tab alt
    //    kenarinda. h/b/t sifir ise otomatik.
    //  Pah: govde tarafindaki koseler (Case A'da dort kose).
    const bool wantStiff = (stiffener == L"Full" || stiffener == L"Determined by shear tab" ||
                            stiffener == L"Partial");
    if (wantStiff) {
        const std::wstring orient = paramAsString(input.params, L"stiffenerOrientation", L"Default");
        const bool horizontal = (orient == L"Perpendicular to main part");
        const double sT = paramAsDouble(input.params, L"stiffenerT", 0.0);
        const double sW = paramAsDouble(input.params, L"stiffenerW", 0.0);
        const double sH = paramAsDouble(input.params, L"stiffenerH", 0.0);
        const double st_t = sT > 0.0 ? sT : thickness;

        // Ana parca kesit olculeri; bilinmiyorsa (0) katalogsuz kaba oranlar.
        const double colW = std::max(1.0, pl.col.secWidth);
        const double colHt = std::max(1.0, pl.col.secHeight);
        const double tw = pl.col.secWebThickness > 0.0 ? pl.col.secWebThickness : 0.04 * colW;
        const double tf = pl.col.secFlangeThickness > 0.0 ? pl.col.secFlangeThickness : 0.06 * colHt;
        const Vec3 Wd = pl.colSec.width;
        const Vec3 Hd = pl.colSec.height;
        const bool caseB = std::fabs(pl.aW) > std::fabs(pl.aH);
        const double tabSign = nearTab ? 1.0 : -1.0;  // ilk uretilen tab tarafi

        // KARSI taraf yonu (O = govde normali boyunca, kayma levhasinin tersine).
        double sigma;
        if (caseB) sigma = (pl.aW >= 0.0) ? -1.0 : 1.0;
        else sigma = (vdot(sec.width, Wd) * tabSign >= 0.0) ? -1.0 : 1.0;
        const Vec3 O{Wd.x * sigma, Wd.y * sigma, Wd.z * sigma};

        // Dusey aralik (zc merkez, [zlo, zhi] kiris eksenine gore).
        const double beamClear =
            beam.secWebThickness > 0.0 && beam.secFlangeThickness > 0.0
                ? std::max(1.0, beam.secHeight - 2.0 * beam.secFlangeThickness)
                : std::max(1.0, beam.secHeight);
        double zhi = beamClear * 0.5 - stiffGapFlange;
        double zlo = -beamClear * 0.5 + stiffGapFlange;
        if (stiffener == L"Determined by shear tab") {
            zhi = height * 0.5;
            zlo = -height * 0.5;
        } else if (stiffener == L"Partial") {
            zlo = std::max(zlo, -height * 0.5);
        }
        if (sH > 0.0 && !horizontal) {
            const double mid = 0.5 * (zhi + zlo);
            zhi = mid + sH * 0.5;
            zlo = mid - sH * 0.5;
        }
        const double vLen = std::max(0.0, zhi - zlo);
        const double vMid = 0.5 * (zhi + zlo);

        // Govde disina uzanan "b" (govdeden uc ucuna) ve flanslar arasi "h" (H yonu).
        const double bOut = std::max(0.0, colW * 0.5 - tw * 0.5 - stiffGapWeb);
        const double hBetween = std::max(0.0, colHt - 2.0 * tf - 2.0 * stiffGapFlange);

        // Kolon kesit merkezi: eksen SectionAnchor'a (varsayilan Top) gore kesitin
        // USTUNDEN gecebilir -> kesit merkezi eksenden Hd*sectionCenterOffset kadar
        // kayiktir (halfExtent ile ayni isaret). Berkitme GERCEK kesit icinde durmali.
        const Vec3 colC{colMid.x + Hd.x * pl.col.sectionCenterOffset,
                        colMid.y + Hd.y * pl.col.sectionCenterOffset,
                        colMid.z + Hd.z * pl.col.sectionCenterOffset};
        // Kesit duzlemi (W,H) koordinati: P -> (w,h) kolon merkezine gore.
        const auto toW = [&](const Vec3& q) {
            return vdot(Vec3{q.x - colC.x, q.y - colC.y, q.z - colC.z}, Wd);
        };
        const auto toH = [&](const Vec3& q) {
            return vdot(Vec3{q.x - colC.x, q.y - colC.y, q.z - colC.z}, Hd);
        };
        const auto fromWH = [&](double w, double h, double z) {
            return Vec3{colC.x + Wd.x * w + Hd.x * h, colC.y + Wd.y * w + Hd.y * h, z};
        };

        // Ortak parca ozellikleri / pah (her berkitme levhasina).
        const std::wstring chType = paramAsString(input.params, L"stiffenerChamferType", L"Default");
        const double chH = std::max(0.0, paramAsDouble(input.params, L"stiffenerChamferH", 0.0));
        const double chV = std::max(0.0, paramAsDouble(input.params, L"stiffenerChamferV", 0.0));
        const std::wstring prefix = paramAsString(input.params, L"stiffenerPosPrefix", L"");
        const int posStart = paramAsInt(input.params, L"stiffenerPosStart", 1);
        const std::wstring stfMaterial = paramAsString(input.params, L"stiffenerMaterial", L"");
        const std::wstring stfFinish = paramAsString(input.params, L"stiffenerFinish", L"");
        const std::wstring stfName = paramAsString(input.params, L"stiffenerName", L"");
        int stfCount = 0;
        const auto addStiffener = [&](PlateSpec stf, bool allCorners) -> bool {
            if (chType != L"No chamfer" && chH > 0.0 && chV > 0.0) {
                const int type = (chType == L"Convex arc chamfer") ? 2
                                 : (chType == L"Concave arc chamfer") ? 3 : 1;
                for (int ci = 0; ci < 4; ++ci) {
                    if (!allCorners && ci != 0 && ci != 3) continue;
                    stf.chamfers.push_back(PlateSpec::Chamfer{ci, type, chH, chV});
                }
            }
            if (!prefix.empty()) stf.posNo = prefix + std::to_wstring(posStart + stfCount);
            stf.material = stfMaterial;
            stf.finish = stfFinish;
            stf.name = stfName.empty() ? L"Stiffener" : stfName;
            if (stf.width <= 0.0 || stf.height <= 0.0 || stf.thickness <= 0.0) {
                plan.ok = false;
                plan.error = L"berkitme olculeri gecersiz (genislik/yukseklik/kalinlik > 0 olmali)";
                return false;
            }
            plan.plates.push_back(stf);
            plan.plateKeys.push_back(stfCount == 0 ? std::string("stiffener")
                                                   : "stiffener" + std::to_string(stfCount + 1));
            ++stfCount;
            return true;
        };

        if (horizontal) {
            // "Perpendicular to main part": XY duzlemine PARALEL (yatay) sureklilik
            // levhalari, KOLONUN ICINDE (flanslar arasi, govdenin yaninda):
            //  - Full: kirisin ust ve alt BASLIK seviyelerinde (2 kot);
            //  - Determined by shear tab: tabin ust ve alt kenari seviyesinde;
            //  - Partial: yalniz ust kotta.
            //  Kiris kolon FLANSINA geliyorsa (Case A) govdenin IKI yanindaki cep
            //  (2 taraf) -> 2 kot x 2 taraf = 4 levha; govde yuzune geliyorsa (Case B)
            //  kiris kendi cebinde oldugundan yalniz KARSI cep -> 2 levha.
            const double b = sW > 0.0 ? sW : bOut;
            const double h = sH > 0.0 ? sH : hBetween;
            const double bf = beam.secFlangeThickness > 0.0 ? beam.secFlangeThickness : 0.05 * beam.secHeight;
            std::vector<double> levels;  // facePoint.z'ye gore
            if (stiffener == L"Determined by shear tab") {
                levels = {height * 0.5 - st_t * 0.5, -(height * 0.5 - st_t * 0.5)};
            } else {
                const double fl = beam.secHeight * 0.5 - bf * 0.5;  // baslik orta kotu
                levels = {fl, -fl};
            }
            if (stiffener == L"Partial") levels.resize(1);  // yalniz ust kot
            std::vector<double> sides;
            if (caseB) sides = {sigma};
            else sides = {sigma, -sigma};
            for (const double side : sides) {
                const Vec3 Os{Wd.x * side, Wd.y * side, Wd.z * side};
                for (const double lv : levels) {
                    PlateSpec stf;
                    stf.thickness = st_t;
                    stf.axis = worldUp;
                    stf.u = Os;
                    stf.v = Hd;
                    stf.width = b;
                    stf.height = h;
                    stf.center = Vec3{colC.x + Os.x * (tw * 0.5 + stiffGapWeb + b * 0.5),
                                      colC.y + Os.y * (tw * 0.5 + stiffGapWeb + b * 0.5),
                                      facePoint.z + lv};
                    if (!addStiffener(stf, false)) return plan;
                }
            }
        } else {
            // "Parallel to secondary part" (varsayilan): DUSEY levha, KIRISE PARALEL
            // duzlemde (kiris kolona dik degil, ACILI ise de kiris yonunde). Duzlem
            // kiris yonunu (yatay izdusum) icerir; kolonun karsi cebinin icine
            // KIRPILIR (dikdortgen cep ile dogru parcasi kesisimi).
            Vec3 d = lenDir;                       // yatay kiris yonu
            d.z = 0.0;
            d = vnorm(d);
            const double dw = vdot(d, Wd), dh = vdot(d, Hd);
            // Dogru uzerindeki Q noktasi (kesit koordinati): Case B'de tab konumu
            // (govde orta duzlemi), Case A'da govdenin hemen yaninda (karsi cep).
            double qw, qh;
            if (caseB) {
                qw = 0.0;
                qh = toH(facePoint) + tabSign * ecc * vdot(sec.width, Hd);
            } else {
                qw = sigma * (tw * 0.5 + stiffGapWeb + st_t * 0.5);
                qh = 0.0;
            }
            // Cep dikdortgeni (sigma tarafi): w in [w0,w1], h in [-hB/2, hB/2].
            double w0 = sigma * (tw * 0.5 + stiffGapWeb);
            double w1 = sigma * (colW * 0.5);
            if (sW > 0.0 && caseB) w1 = w0 + sigma * sW;
            const double wlo = std::min(w0, w1), whi = std::max(w0, w1);
            const double hlo = -hBetween * 0.5, hhi = hBetween * 0.5;
            // Q + s*(dw,dh) kirpma: s araligi.
            double smin = -1e9, smax = 1e9;
            const auto clip = [&](double q, double dir, double lo, double hi) {
                if (std::fabs(dir) < 1e-9) {
                    if (q < lo - 1e-9 || q > hi + 1e-9) { smin = 1; smax = 0; }
                    return;
                }
                double s1 = (lo - q) / dir, s2 = (hi - q) / dir;
                if (s1 > s2) std::swap(s1, s2);
                smin = std::max(smin, s1);
                smax = std::min(smax, s2);
            };
            clip(qw, dw, wlo, whi);
            clip(qh, dh, hlo, hhi);
            if (!(smax - smin > 1e-6)) {
                plan.ok = false;
                plan.error = L"berkitme kolon kesiti icine sigmiyor (bosluk/kesit olculerini kontrol edin)";
                return plan;
            }
            const double len = smax - smin;
            const double sm = 0.5 * (smin + smax);
            const double cw = qw + dw * sm, ch2 = qh + dh * sm;
            // u: cepte govdeden uzaga dogru (dot(u,O) >= 0).
            Vec3 u = d;
            if (vdot(u, O) < 0.0) u = Vec3{-u.x, -u.y, -u.z};
            PlateSpec stf;
            stf.thickness = st_t;
            stf.axis = vnorm(Vec3{worldUp.y * u.z - worldUp.z * u.y, worldUp.z * u.x - worldUp.x * u.z,
                                  worldUp.x * u.y - worldUp.y * u.x});
            stf.u = u;
            stf.v = worldUp;
            stf.width = (sW > 0.0 && !caseB) ? std::min(sW, len) : len;
            stf.height = vLen;
            stf.center = fromWH(cw, ch2, facePoint.z + vMid);
            if (!addStiffener(stf, !caseB)) return plan;
        }
    }

    // --- backing bar (kaynak arkalik cubugu): kiris ucu yuzunde ince serit ---
    if (backingBarT > 0.0) {
        PlateSpec bb;
        bb.axis = lenDir;
        bb.u = sec.width;
        bb.v = sec.height;
        bb.thickness = backingBarT;
        bb.width = beam.secWidth;
        bb.height = beam.secHeight;
        bb.center = Vec3{facePoint.x + lenDir.x * beamGap,
                         facePoint.y + lenDir.y * beamGap,
                         facePoint.z + lenDir.z * beamGap};
        bb.name = L"Backing Bar";
        plan.plates.push_back(bb);
        plan.plateKeys.push_back("backing_bar");
    }

    plan.ok = true;
    return plan;
}

// --- Tekla Haunch (40): initial editable native welded-plate core -------------
// The SOURCE Tekla 40 reference has multiple plates, 2x4 M20 bolt positions
// and many welds. This generator builds the SUPPORTED native core only; it
// adapts dimensions to the selected Model Maker beams/columns and does NOT
// silently claim full per-feature Tekla geometry equivalence.
ComponentType haunch40ComponentType() noexcept {
    ComponentType t;
    t.typeId = L"tekla_haunch_40";
    t.schemaVersion = 1;
    t.displayName = L"Haunch (40)";
    t.params = {
        lengthParam(L"haunchLength", L"mm", 450.0, 120.0, 2000.0,
                    L"kolondan kirise dogru haunch uzunlugu"),
        lengthParam(L"haunchDepth", L"mm", 240.0, 60.0, 1000.0,
                    L"kolon tarafinda haunch derinligi"),
        lengthParam(L"haunchTipDepth", L"mm", 25.0, 10.0, 200.0,
                    L"kiris tarafindaki haunch web derinligi"),
        lengthParam(L"haunchWebThickness", L"mm", 10.0, 4.0, 40.0,
                    L"haunch govde levhasi"),
        lengthParam(L"haunchFlangeThickness", L"mm", 10.0, 4.0, 60.0,
                    L"egimli alt baslik levhasi"),
        lengthParam(L"haunchFlangeWidth", L"mm", 0.0, 0.0, 1000.0,
                    L"0 = mevcut kiris flans genisligi"),
        lengthParam(L"endPlateThickness", L"mm", 20.0, 6.0, 80.0,
                    L"alin levhasi kalinligi"),
        lengthParam(L"endPlateWidth", L"mm", 0.0, 0.0, 1000.0,
                    L"0 = kiris kesiti ve bulon araligindan otomatik"),
        lengthParam(L"endPlateHeight", L"mm", 0.0, 0.0, 2000.0,
                    L"0 = kiris yuksekligi + haunch + kenar bosluklari"),
        lengthParam(L"gap", L"mm", 0.0, 0.0, 100.0,
                    L"kolon yuzu ve alin levhasi arasi bosluk"),
        lengthParam(L"topPlateThickness", L"mm", 10.0, 0.0, 60.0,
                    L"kolon cap levhasi et kalinligi (0 = yok)"),
        lengthParam(L"topPlateLength", L"mm", 230.0, 50.0, 1000.0,
                    L"eski uyumluluk alani; cap olcusu kolon kesitinden otomatik"),
        boolParam(L"createBeamStiffener", false,
                  L"haunch bitisinde enine kiris berkitmesi"),
        lengthParam(L"stiffenerThickness", L"mm", 10.0, 4.0, 40.0,
                    L"kiriş berkitme kalinligi"),
        lengthParam(L"boltDiameter", L"mm", 20.0, 12.0, 36.0,
                    L"bulon nominal capi"),
        lengthParam(L"holeDiameter", L"mm", 22.0, 13.0, 50.0,
                    L"yuvarlak delik capi"),
        intParam(L"boltCountX", 2, 1, 4, L"her grupta yatay bulon adedi"),
        intParam(L"boltCountY", 2, 1, 4, L"her grupta dikey bulon adedi"),
        lengthParam(L"boltPitchX", L"mm", 120.0, 40.0, 500.0,
                    L"iki bulon kolonu araligi"),
        lengthParam(L"boltPitchY", L"mm", 80.0, 40.0, 500.0,
                    L"her bulon grubu dikey araligi"),
        lengthParam(L"boltEdge", L"mm", 35.0, 20.0, 120.0,
                    L"delik kenar boslugu"),
        enumParam(L"grade", L"8.8", {L"8.8", L"10.9"},
                  L"bulon kalitesi"),
        boolParam(L"createWelds", true, L"haunch kaynaklari"),
        lengthParam(L"weldSize", L"mm", 6.0, 3.0, 30.0,
                    L"fillet kaynak bacagi"),
        stringParam(L"plateMaterial", L"S235JR", L"levha malzemesi"),
    };
    t.catalog.sourceComponentNumber = L"40";
    t.catalog.category = L"Moment connection / Haunch";
    t.catalog.primaryMemberType = L"kolon";
    t.catalog.secondaryMemberType = L"kiris";
    t.catalog.supportedFeatures = {
        L"alın levhası", L"taper haunch gövdesi", L"eğimli alt başlık levhası",
        L"kolon ust cap levhasi", L"iki grup M20 bulon", L"kaynaklar", L"opsiyonel enine berkitme"
    };
    t.catalog.coverageStatus = L"PARTIALLY_IMPLEMENTED";
    t.catalog.pickCount = 2;
    t.catalog.catalogVisible = true;
    return t;
}

GenerationPlan generateHaunch40(const ComponentInput& input) noexcept {
    GenerationPlan plan;
    // Share the production 146 contact placement: actual selected member
    // axes/sections, automatic column vs beam role recognition, local frame.
    const ShearTabPlacement p = resolveShearTabPlacement(input);
    if (!p.ok) { plan.ok = false; plan.error = p.error; return plan; }
    if (p.beam.secHeight < 80.0 || p.beam.secWidth < 60.0 ||
        p.col.secHeight < 80.0 || p.col.secWidth < 60.0 ||
        p.col.id == p.beam.id || p.col.id == kInvalidEntityId ||
        p.beam.id == kInvalidEntityId) {
        plan.ok = false;
        plan.error = L"Haunch 40: farkli ve gercek kolon/kiris kesitleri gerekli";
        return plan;
    }
    if (std::fabs(vdot(p.lenDir, Vec3{0,0,1})) > 0.65) {
        plan.ok = false;
        plan.error = L"Haunch 40: bu surum duseye yakin kiris desteklemiyor";
        return plan;
    }

    const double L = paramAsDouble(input.params,L"haunchLength",450.0);
    const double D = paramAsDouble(input.params,L"haunchDepth",240.0);
    const double tip = paramAsDouble(input.params,L"haunchTipDepth",25.0);
    const double webT = paramAsDouble(input.params,L"haunchWebThickness",10.0);
    const double flangeT = paramAsDouble(input.params,L"haunchFlangeThickness",10.0);
    const double endT = paramAsDouble(input.params,L"endPlateThickness",20.0);
    const double gap = paramAsDouble(input.params,L"gap",0.0);
    const double edge = paramAsDouble(input.params,L"boltEdge",35.0);
    const double bD = paramAsDouble(input.params,L"boltDiameter",20.0);
    const double holeD = paramAsDouble(input.params,L"holeDiameter",22.0);
    const double pitchX = paramAsDouble(input.params,L"boltPitchX",120.0);
    const double pitchY = paramAsDouble(input.params,L"boltPitchY",80.0);
    const int countX=paramAsInt(input.params,L"boltCountX",2);
    const int countY=paramAsInt(input.params,L"boltCountY",2);
    const double spanX=(countX-1)*pitchX;
    const double spanY=(countY-1)*pitchY;
    const double hBeam = p.beam.secHeight;
    const double wBeam = p.beam.secWidth;
    // Auto-sized end plate must satisfy the SAME clearances as an
    // explicitly dimensioned plate. Small edge values and larger holes
    // previously made the automatic plate width fail its own validation.
    const double plateW = std::max({wBeam+20.0, spanX+2.0*edge,
                                    spanX+holeD+15.0});
    const double plateH = hBeam+D+2.0*edge;
    const double givenW = paramAsDouble(input.params,L"endPlateWidth",0.0);
    const double givenH = paramAsDouble(input.params,L"endPlateHeight",0.0);
    const double actualW = givenW>0 ? givenW : plateW;
    const double actualH = givenH>0 ? givenH : plateH;
    const double flWparam=paramAsDouble(input.params,L"haunchFlangeWidth",0.0);
    const double flW=flWparam>0 ? flWparam : wBeam;
    if (!(L > 80.0 && D > tip+25.0 && tip > 5.0 && webT > 0.0 &&
          flangeT > 0.0 && endT > 0.0 && holeD >= bD &&
          holeD > 0.0 && pitchX > holeD && pitchY > holeD &&
          countX>=1 && countX<=4 && countY>=1 && countY<=4 &&
          flW >= webT && actualW >= spanX + holeD + 15.0 &&
          actualH >= hBeam+D+20.0)) {
        plan.ok=false;
        plan.error=L"Haunch 40: haunch/levha/bulon olculeri tutarsiz";
        return plan;
    }
    const auto add=[](Vec3 a,Vec3 b) {
        return Vec3{a.x+b.x,a.y+b.y,a.z+b.z};
    };
    const auto scale=[](Vec3 a,double k) {
        return Vec3{a.x*k,a.y*k,a.z*k};
    };
    const auto cross=[](Vec3 a,Vec3 b) {
        return Vec3{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
    };
    const auto length=[](Vec3 a) {
        return std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);
    };
    const Vec3 dir=p.lenDir, W=p.sec.width, H=p.sec.height;
    // The actual welded beam end must stop at the OUTER face of the
    // bolted end plate. Column face = p.facePoint; the plate occupies
    // [facePoint+gap, facePoint+gap+endT]. Never let the beam penetrate
    // the column or the plate. Use a persistent hidden PlaneCut on
    // the selected Document beam, just like Tekla 146.
    const Vec3 start=add(p.facePoint,scale(dir,gap+endT));
    {
        CutModifier cut;
        cut.kind=CutKind::PlaneCut;
        cut.target=p.beam.id;
        cut.planeOrigin=start;
        cut.planeNormal=dir;
        cut.keepPositive=true;  // keep the beam's outward/remote side
        cut.hidden=true;
        cut.name="Haunch 40 - Beam End at Column Face + Plate";
        plan.cuts.push_back(cut);
        plan.cutKeys.push_back("haunch_beam_end_cut");
    }

    std::wstring plateMaterial=paramAsString(input.params,L"plateMaterial",L"S235JR");
    const auto pushPlate=[&](PlateSpec plate,const char* role) {
        plate.material=plateMaterial;
        plan.plates.push_back(std::move(plate));
        plan.plateKeys.push_back(role);
    };
    // 1) Bolted end plate. The origin follows the actual column face.
    PlateSpec end;
    end.center=add(p.facePoint,add(scale(dir,gap+endT*0.5),scale(H,-D*0.5)));
    end.u=W; end.v=H; end.axis=dir;
    end.width=actualW;end.height=actualH;end.thickness=endT;
    end.name=L"40 - End Plate";
    pushPlate(end,"haunch_end_plate");

    // 2) Haunch web: rectangular stock with large bottom-outboard chamfer
    // creates a true tapered PL web (thinner at far beam end).
    PlateSpec web;
    web.center=add(start,add(scale(dir,L*0.5),
                              scale(H,-hBeam*0.5-D*0.5)));
    web.u=dir; web.v=H; web.axis=W;
    web.width=L;web.height=D;web.thickness=webT;
    web.chamfers.push_back(PlateSpec::Chamfer{1,1,L-25.0,D-tip});
    web.name=L"40 - Tapered Haunch Web";
    pushPlate(web,"haunch_web");

    // 3) Inclined compression flange: use the EXACT two endpoints of the
    // haunch web's chamfer (corner=1). The chamfer starts 25 mm after
    // the column-side lower corner, NOT at x=0. The previous flange used
    // x=0 and visibly diverged from the sloping web edge.
    const double webChamferStart=L-web.chamfers.front().h; // = 25 mm
    const double webChamferEndHeight=web.chamfers.front().v; // = D-tip
    const Vec3 lowerNear=add(add(start,scale(dir,webChamferStart)),
                              scale(H,-hBeam*0.5-D));
    const Vec3 lowerFar=add(add(start,scale(dir,L)),
                             scale(H,-hBeam*0.5-D+webChamferEndHeight));
    const Vec3 slant{lowerFar.x-lowerNear.x,lowerFar.y-lowerNear.y,
                     lowerFar.z-lowerNear.z};
    const double slantL=length(slant);
    if(slantL<=1.0) {
        plan.ok=false;plan.error=L"Haunch 40: egimli flans boyu sifir";
        return plan;
    }
    const Vec3 slope=scale(slant,1.0/slantL);
    // W x slope faces OUT of the web (below the haunch); shift half the
    // thickness so the INNER FLANGE FACE, not its mid-surface, contacts
    // the exact chamfer edge at both ends.
    const Vec3 outward=vnorm(cross(W,slope));
    if(length(outward)<0.99) {
        plan.ok=false;plan.error=L"Haunch 40: egimli flans normal ekseni bozuk";
        return plan;
    }
    PlateSpec flange;
    flange.center=add(scale(add(lowerNear,lowerFar),0.5),
                      scale(outward,flangeT*0.5));
    flange.u=W;flange.v=slope;flange.axis=outward;
    flange.width=flW;flange.height=slantL;flange.thickness=flangeT;
    flange.name=L"40 - Inclined Lower Flange";
    pushPlate(flange,"haunch_lower_flange");

    // 4) CAP PLATE belongs on the ACTUAL COLUMN TOP, never the beam top.
    // Keep existing editable parameter keys for MMW8/recompute compatibility.
    const double topT=paramAsDouble(input.params,L"topPlateThickness",10.0);
    if(topT>0.0) {
        const Vec3 up{0.0,0.0,1.0};
        const Vec3 colDelta{
            p.col.axisTo.x-p.col.axisFrom.x,
            p.col.axisTo.y-p.col.axisFrom.y,
            p.col.axisTo.z-p.col.axisFrom.z
        };
        const double colLength=length(colDelta);
        if(colLength<1.0 || std::fabs(vdot(vnorm(colDelta),up))<0.95) {
            plan.ok=false;
            plan.error=L"Haunch 40: cap levhasi icin duseye yakin kolon gerekli";
            return plan;
        }
        // Top is the physically higher endpoint regardless of axis order.
        const Vec3 columnTop=p.col.axisTo.z>=p.col.axisFrom.z
                                 ? p.col.axisTo : p.col.axisFrom;
        // For non-centred section anchors the steel section is displaced
        // transversely from the member axis (e.g. SectionAnchor::Top).
        const Vec3 columnSectionTop=add(
            columnTop,scale(p.colSec.height,p.col.sectionCenterOffset));
        // Dimension cap in the COLUMN'S OWN section frame, not the beam
        // direction. Previous use of a projected beam-aligned bounding box
        // plus 20-mm margins enlarged/misrotated the cap.
        const Vec3 projectedWidth{
            p.colSec.width.x,p.colSec.width.y,0.0
        };
        if(length(projectedWidth)<1e-8) {
            plan.ok=false;plan.error=L"Haunch 40: kolon kesit genislik yonu tanimsiz";
            return plan;
        }
        const Vec3 capU=vnorm(projectedWidth);
        const Vec3 capV=vnorm(cross(up,capU)); // right-handed, XY plane
        PlateSpec cap;
        cap.axis=up;cap.u=capU;cap.v=capV;
        // EXACT target profile bbox b x h; no allowance, no beam-based
        // projection, and no overriding old topPlateLength dimension.
        cap.width=p.col.secWidth;
        cap.height=p.col.secHeight;
        cap.thickness=topT;
        // The cap's lower surface is exactly the COLUMN TOP plane.
        cap.center=add(columnSectionTop,scale(up,topT*0.5));
        cap.name=L"40 - Column Cap Plate";
        pushPlate(cap,"haunch_top_plate"); // stable role id for old records
    }

    // 5) Optional transverse web stiffener at the far haunch termination.
    if(paramAsBool(input.params,L"createBeamStiffener",false)) {
        const double st=paramAsDouble(input.params,L"stiffenerThickness",10.0);
        if(p.beam.secWebThickness<=0.0 || p.beam.secFlangeThickness<=0.0) {
            plan.ok=false;
            plan.error=L"Haunch 40: kiriș berkitmesi icin flans/govde et kalinliklari gerekli";
            return plan;
        }
        PlateSpec rib;
        rib.center=add(start,scale(dir,L));
        rib.u=W;rib.v=H;rib.axis=dir;
        rib.width=wBeam-2.0*p.beam.secFlangeThickness;
        rib.height=hBeam-2.0*p.beam.secFlangeThickness;
        rib.thickness=st;rib.name=L"40 - Beam Stiffener";
        if(rib.width<=0.0 || rib.height<=0.0) {
            plan.ok=false;plan.error=L"Haunch 40: berkitme kesite sigmiyor";
            return plan;
        }
        pushPlate(rib,"haunch_beam_stiffener");
    }

    // Two independent semantic bolt groups; both share user-controlled X/Y
    // counts. Keep the OUTERMOST rows 20 mm from the structural beam/
    // haunch reference edges; adding rows grows each group inwards.
    // Legacy records without count params default to 2x2 (8 total).
    const double upperCenter=hBeam*0.5-0.5*spanY-20.0;
    const double lowerCenter=-(hBeam*0.5+D)+0.5*spanY+20.0;
    const double topEdge=hBeam*0.5+edge;
    const double bottomEdge=-(hBeam*0.5+D)-edge;
    if(upperCenter+0.5*spanY+holeD*0.5 >= topEdge ||
       lowerCenter-0.5*spanY-holeD*0.5 <= bottomEdge ||
       upperCenter-lowerCenter <= spanY+holeD) {
        plan.ok=false;
        plan.error=L"Haunch 40: iki bulon grubu alin levhasina sigmiyor";
        return plan;
    }
    // Real bolt stack: end plate + physical supporting column FLANGE,
    // not the entire HEB/HEA 300-mm bounding box or every haunch plate.
    // Auto-grip from all owned solids caused the enormous shanks which
    // appeared on both sides of the connection in the first Windows preview.
    if (!(p.col.secFlangeThickness > 0.0)) {
        plan.ok=false;
        plan.error=L"Haunch 40: secili kolonun gercek flans kalinligi bilinmiyor";
        return plan;
    }
    const double gripLength=endT+gap+p.col.secFlangeThickness;
    const std::wstring gradeW=paramAsString(input.params,L"grade",L"8.8");
    const std::string grade(gradeW.begin(),gradeW.end());
    const auto pushBolts=[&](const char* role,double offset) {
        BoltGroup bg;
        // Use the SAME measured plate frame for both groups. BoltFrame's
        // second in-plane vector is axis CROSS xDir = end.v (right-handed).
        // The origin is the first (lower-left) point of countX x countY
        // rectangular pattern, NOT the centre of that group.
        const Vec3 groupCentre=add(end.center,
                                  scale(end.v,offset + D*0.5));
        bg.origin=add(add(groupCentre,scale(end.u,-spanX*0.5)),
                      scale(end.v,-spanY*0.5));
        // The shank centre is in the real plate+column-flange grip stack,
        // not at the end plate midplane or column section centreline.
        bg.origin=add(bg.origin,
                      scale(end.axis,-0.5*(gap+p.col.secFlangeThickness)));
        bg.axis=end.axis;
        bg.xDir=end.u;
        bg.countX=countX;bg.countY=countY;
        bg.spacingX=pitchX;bg.spacingY=pitchY;
        bg.params.nominalDiameter=bD;
        bg.params.holeDiameter=holeD;
        bg.params.grade=grade;
        bg.params.hasHead=true;
        bg.params.hasNut=true;
        bg.params.hasWasher=true;
        // Head sits on the accessible beam/end-plate side (+end.axis);
        // nut sits behind the column flange (-end.axis).
        bg.params.direction=2;
        bg.gripLength=gripLength;
        bg.holeDepth=gripLength+2.0; // 1 mm clearance at each end
        bg.targetOwnedPlateRole="haunch_end_plate";
        bg.parts={p.col.id};
        plan.boltGroups.push_back(std::move(bg));
        plan.boltGroupKeys.push_back(role);
    };
    pushBolts("haunch_bolts_upper",upperCenter);
    pushBolts("haunch_bolts_lower",lowerCenter);

    // Identified semantic weld relationships. No automatic weld-to-column
    // for the bolted end plate; it is welded to the secondary beam.
    if(paramAsBool(input.params,L"createWelds",true)) {
        const double size=paramAsDouble(input.params,L"weldSize",6.0);
        const auto weld=[&](const char* role,const char* plateRole) {
            WeldSpec w;
            w.partA=p.beam.id;
            w.partBRole=plateRole;
            w.size=size;
            w.name=L"40 - Haunch Weld";
            plan.welds.push_back(w);
            plan.weldKeys.push_back(role);
        };
        weld("weld_haunch_end_plate","haunch_end_plate");
        weld("weld_haunch_web","haunch_web");
        weld("weld_haunch_lower_flange","haunch_lower_flange");
        if(topT>0.0) {
            WeldSpec capWeld;
            capWeld.partA=p.col.id;
            capWeld.partBRole="haunch_top_plate";
            capWeld.size=size;
            capWeld.name=L"40 - Column Cap Weld";
            plan.welds.push_back(capWeld);
            plan.weldKeys.push_back("weld_haunch_top_plate"); // stable id
        }
    }
    plan.ok=true;
    return plan;
}

} // namespace mm
