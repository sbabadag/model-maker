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
        lengthParam(L"holeDiameter", L"mm", 22.0, 12.0, 34.0, L"delik capi"),
        intParam(L"boltCount", 4, 1, 12, L"bulon adedi (sira; yukseklik yonu)"),
        intParam(L"boltCountX", 1, 1, 12, L"bulon kolon adedi (genislik yonu)"),
        lengthParam(L"pitchX", L"mm", 0.0, 0.0, 2000.0, L"kolon araligi (0 = auto)"),
        lengthParam(L"pitchY", L"mm", 0.0, 0.0, 2000.0, L"sira araligi (0 = boltSpacing/auto)"),
        lengthParam(L"edgeX", L"mm", 0.0, 0.0, 2000.0, L"genislik kenar mesafesi (0 = boltEdge)"),
        lengthParam(L"edgeY", L"mm", 0.0, 0.0, 2000.0, L"yukseklik kenar mesafesi (0 = boltEdge)"),
        lengthParam(L"boltEdge", L"mm", 40.0, 10.0, 200.0, L"kenar mesafesi"),
        enumParam(L"grade", L"8.8", {L"8.8", L"10.9"}, L"bulon sinifi"),
        stringParam(L"boltSpacing", L"auto", L"bulon araliklari (mm, boslukla; 'auto' = turet)"),
        enumParam(L"boltVerticalMeasure", L"middle", {L"top", L"middle", L"below"},
                  L"bulon grubunun dusey konum referansi"),
        lengthParam(L"boltVerticalPos", L"mm", 0.0, -1000.0, 1000.0,
                    L"bulon grubunun dusey kaymasi"),
        stringParam(L"deletedBolts", L"", L"silinecek bulon numaralari (1-based, boslukla)"),
        // --- Tekla 146 "Bolt properties" (resimli diyalogdaki alanlar) -------
        // Tekla diyalogunda her satirin solunda "Default" onay kutusu vardir;
        // isaretsiz birakilan alan varsayilani kullanir (optional=true).
        enumParam(L"boltStandard", L"Default",
                  {L"Default", L"ISO 4014", L"ISO 4032", L"ISO 7412"}, L"bulon standardi"),
        lengthParam(L"tolerance", L"mm", 0.0, 0.0, 10.0, L"delik toleransi"),
        boolParam(L"threadInMat", true, L"dis bulonlu parcada (thread in mat)"),
        enumParam(L"holeType", L"Default",
                  {L"Default", L"Oversized", L"Slotted", L"Threaded"}, L"delik tipi"),
        enumParam(L"rotateSlots", L"Default", {L"Default", L"No", L"Yes"}, L"yuvalari dondur"),
        enumParam(L"slotsIn", L"Default", {L"Default", L"Plate", L"Beam"},
                  L"yuvalar hangi parcada"),
        enumParam(L"cutLength", L"Default", {L"Default", L"No", L"Yes"}, L"boyuna kesme"),
        stringParam(L"comment", L"", L"aciklama"),
        // --- Stiffeners tab ---
        enumParam(L"stiffenerCreation", L"none", {L"none", L"full"}, L"govde berkitmesi"),
        lengthParam(L"stiffenerGapWeb", L"mm", 10.0, 0.0, 200.0, L"berkitme-govde boslugu"),
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
    const double width = paramAsDouble(input.params, L"plateWidth", 120.0);
    const double height = paramAsDouble(input.params, L"plateHeight", 240.0);
    const double boltD = paramAsDouble(input.params, L"boltDiameter", 20.0);
    const double holeD = paramAsDouble(input.params, L"holeDiameter", 22.0);
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
    const std::wstring grade = paramAsString(input.params, L"grade", L"8.8");

    const std::wstring tabPos = paramAsString(input.params, L"shearTabPosition", L"near_side");
    const double tabGap = paramAsDouble(input.params, L"tabGapSecondary", 0.0);
    const double beamGap = paramAsDouble(input.params, L"beamGap", 20.0);
    const std::wstring endCut = paramAsString(input.params, L"beamEndCut", L"square");
    const std::wstring stiffener = paramAsString(input.params, L"stiffenerCreation", L"none");
    const double stiffGapWeb = paramAsDouble(input.params, L"stiffenerGapWeb", 10.0);
    const std::wstring boltSpacing = paramAsString(input.params, L"boltSpacing", L"auto");
    const std::wstring boltMeasure = paramAsString(input.params, L"boltVerticalMeasure", L"middle");
    const double boltVPos = paramAsDouble(input.params, L"boltVerticalPos", 0.0);
    const std::wstring delBolts = paramAsString(input.params, L"deletedBolts", L"");
    const double backingBarT = paramAsDouble(input.params, L"backingBarT", 0.0);

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
            spanX = std::max(0.0, width - 2.0 * effEdgeX);
            spacingX = spanX / static_cast<double>(countX - 1);
        }
    }
    b.spacingX = spacingX;

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
    // Dusey konum: measure top/middle/below + boltVerticalPos. Referans nokta
    // tab merkezi (govde merkezi = tabBase); sec.height yonu = dusey.
    const double hh = height * 0.5;
    double originOffset = -spanY * 0.5;  // middle (varsayilan)
    if (boltMeasure == L"top") {
        originOffset = (hh - effEdgeY) - spanY;   // ust bulon tabTop - edge'de
    } else if (boltMeasure == L"below") {
        originOffset = -(hh - effEdgeY);          // alt bulon tabBottom + edge'de
    }
    originOffset += boltVPos;
    // Yatay konum: kolon grubu genislikte MERKEZLENIR (origin = sol-alt bulon).
    const double xOffset = -spanX * 0.5;
    // BULON EKSEN-YONU KONUMU + GRIP (display): bulonlar levhayi + KIRIS GOVDESINI
    // (web) deler. Yigin govde uzak yuzu (-w/2) .. levha dis yuzu (ecc + t/2)
    // arasinda uzanir; merkezi (ecc+t/2-w/2)/2'dedir. Bulon gorseli bu merkeze
    // oturtulur ki bas/somun yiginin iki dis yuzune otursun. (Onceki hata: origin
    // tabBase'de kaldigi icin civata yarim kalinlik kayiyordu.) Eksen-yonu kayma
    // YALNIZ display'i etkiler; delikler through-cut (holeDepth=0) oldugundan
    // konumlari degismez. bbox yontemi (Document::boltGripLength) kiris flans
    // genisligini (300 mm) grip'e sokup civatayi gercek disi uzatirdi; yerine
    // yigin boyu kullanilir. secWebThickness bilinmiyorsa (0) eski davranis
    // korunur (origin tabBase'de, gripLength 0 -> commit'te bbox fallback).
    double boltAxisShift = 0.0;
    if (beam.secWebThickness > 0.0) {
        const double t2 = thickness * 0.5;
        const double w2 = beam.secWebThickness * 0.5;
        if (nearTab && farTab) {
            // Cift tab: yigin simetrik -> bulon tabBase merkezinde kalir.
            b.gripLength = 2.0 * (ecc + t2);
        } else {
            const double tabDir = nearTab ? 1.0 : -1.0;  // far tab: -sec.width tarafi
            const double plateOuter = ecc + t2;           // levha dis yuzu (merkezden)
            const double webFar = -w2;                    // govde uzak yuzu (merkezden)
            b.gripLength = plateOuter - webFar;           // yigin toplam boyu
            boltAxisShift = tabDir * (plateOuter + webFar) * 0.5;
        }
    }
    b.origin = Vec3{tabBase.x + sec.width.x * boltAxisShift + lenDir.x * xOffset + sec.height.x * originOffset,
                    tabBase.y + sec.width.y * boltAxisShift + lenDir.y * xOffset + sec.height.y * originOffset,
                    tabBase.z + sec.width.z * boltAxisShift + lenDir.z * xOffset + sec.height.z * originOffset};
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
    b.parts = {beam.id};
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

    // --- stiffener (govde berkitmesi): dusey levha, kolon ekseninde, kiris
    //     kotunda, lenDir'e DIK. Kalinlik lenDir boyunca; genislik kolon kesit
    //     boyutu (lenDir'e dik) - 2*gapWeb; yukseklik tab yuksekligi (baglanti
    //     boyu; Tekla "flanslar arasi" kesin boyu MemberGeometry'de flans
    //     kalinligi olmadigi icin tab yuksekligine esit — yaklasik, dokumante). ---
    if (stiffener == L"full") {
        PlateSpec st;
        st.axis = lenDir;
        st.u = sec.width;
        st.v = worldUp;
        st.thickness = thickness;
        st.width = std::max(0.0, pl.col.secWidth - 2.0 * stiffGapWeb);
        st.height = std::max(0.0, height);
        st.center = Vec3{colMid.x, colMid.y, facePoint.z};
        st.name = L"Stiffener";
        plan.plates.push_back(st);
        plan.plateKeys.push_back("stiffener");
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

} // namespace mm
