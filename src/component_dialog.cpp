#include "model_maker/component_dialog.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace mm {

// --- Yardimcilar (anonim) ---------------------------------------------------

namespace {

// Parametre semasi kurulum yardimcilari (dialog'a ozgu varsayilanlar).
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

// Araliksiniz Length (sentinel/override alanlari: -1e9, -1, 0=otomatik vb.).
CompParamSpec lengthParamOpen(const std::wstring& name, const std::wstring& unit,
                              double def, const std::wstring& desc = {}) noexcept {
    CompParamSpec s;
    s.name = name;
    s.type = CompParamType::Length;
    s.unit = unit;
    s.defDouble = def;
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

CompParamSpec stringParam(const std::wstring& name, std::wstring def,
                          const std::wstring& desc = {}) noexcept {
    CompParamSpec s;
    s.name = name;
    s.type = CompParamType::String;
    s.defString = std::move(def);
    s.description = desc;
    return s;
}

// Dar -> genis / genis -> dar string.
std::wstring widen(const std::string& s) noexcept {
    return std::wstring(s.begin(), s.end());
}
std::string narrow(const std::wstring& s) noexcept {
    return std::string(s.begin(), s.end());
}

// choices icinde value'nun indeksini bul (yoksa -1).
int enumIndex(const std::vector<std::wstring>& choices, const std::wstring& value) noexcept {
    for (std::size_t i = 0; i < choices.size(); ++i)
        if (choices[i] == value) return static_cast<int>(i);
    return -1;
}

// --- Cizim ilkel yardimcilari ------------------------------------------------

DiagramPrimitive rectP(double x0, double y0, double x1, double y1,
                       bool filled = false) noexcept {
    DiagramPrimitive p;
    p.kind = DiagramPrimitiveKind::Rect;
    p.a = {x0, y0};
    p.b = {x1, y1};
    p.filled = filled;
    return p;
}

DiagramPrimitive lineP(double x0, double y0, double x1, double y1,
                       bool dashed = false) noexcept {
    DiagramPrimitive p;
    p.kind = DiagramPrimitiveKind::Line;
    p.a = {x0, y0};
    p.b = {x1, y1};
    p.dashed = dashed;
    return p;
}

DiagramPrimitive circleP(double cx, double cy, double r) noexcept {
    DiagramPrimitive p;
    p.kind = DiagramPrimitiveKind::Circle;
    p.a = {cx, cy};
    p.r = r;
    return p;
}

DiagramPrimitive textP(double x, double y, std::wstring t) noexcept {
    DiagramPrimitive p;
    p.kind = DiagramPrimitiveKind::Text;
    p.a = {x, y};
    p.text = std::move(t);
    return p;
}

DiagramHotspot hotspot(std::wstring id, double x, double y, double radius,
                       std::optional<std::wstring> paramName,
                       std::optional<std::wstring> label) noexcept {
    DiagramHotspot h;
    h.id = std::move(id);
    h.position = {x, y};
    h.radius = radius;
    h.paramName = std::move(paramName);
    h.label = std::move(label);
    return h;
}

// Enum secenekleri (Tekla isaret sozlesmesiyle uyumlu int<->etiket).
const std::vector<std::wstring>& alignHChoices() noexcept {
    static const std::vector<std::wstring> c{L"Sol", L"Orta", L"Sag"};
    return c;
}
const std::vector<std::wstring>& alignVChoices() noexcept {
    static const std::vector<std::wstring> c{L"Ust", L"Orta", L"Alt"};
    return c;
}
const std::vector<std::wstring>& gradeChoices() noexcept {
    static const std::vector<std::wstring> c{L"8.8", L"10.9"};
    return c;
}

int clampI(int v, int lo, int hi) noexcept {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

} // namespace

// --- Dogrulama --------------------------------------------------------------

DialogSchemaValidation validateDialogSchema(const ComponentDialogSchema& schema,
                                            const std::vector<CompParamSpec>& specs) noexcept {
    DialogSchemaValidation result;
    result.ok = true;

    if (schema.typeId.empty()) {
        result.ok = false;
        result.problems.push_back(L"typeId bos");
    }

    std::unordered_set<std::wstring> specNames;
    for (const auto& s : specs) specNames.insert(s.name);

    // Item id'leri: bos/tekil.
    std::unordered_set<std::wstring> itemIds;
    for (const auto& item : schema.items) {
        if (item.id.empty()) {
            result.ok = false;
            result.problems.push_back(L"item id bos");
        } else if (!itemIds.insert(item.id).second) {
            result.ok = false;
            result.problems.push_back(L"yinelenen item id: " + item.id);
        }
    }

    // Hotspot id'leri + parametre baglari.
    std::unordered_set<std::wstring> hotspotIds;
    for (const auto& h : schema.hotspots) {
        if (h.id.empty()) {
            result.ok = false;
            result.problems.push_back(L"hotspot id bos");
        } else if (!hotspotIds.insert(h.id).second) {
            result.ok = false;
            result.problems.push_back(L"yinelenen hotspot id: " + h.id);
        }
        if (h.paramName.has_value() && specNames.find(*h.paramName) == specNames.end()) {
            result.ok = false;
            result.problems.push_back(L"hotspot " + h.id + L" bilinmeyen parametreye bagli: " +
                                      *h.paramName);
        }
        // Kaynak cagrisi: satir indeksi kaynak tablosunda VAR olmali.
        if (h.weldRow.has_value() &&
            (*h.weldRow < 0 || static_cast<std::size_t>(*h.weldRow) >= schema.weldRows.size())) {
            result.ok = false;
            result.problems.push_back(L"hotspot " + h.id + L" gecersiz kaynak satirina bagli");
        }
        if (h.weldRow.has_value() && h.paramName.has_value()) {
            result.ok = false;
            result.problems.push_back(L"hotspot " + h.id +
                                      L" hem parametreye hem kaynak satirina bagli (biri olmali)");
        }
    }

    // Kaynak tablosu: numaralar 1..N tekil ve sirali olmali (cagri numaralari).
    for (std::size_t i = 0; i < schema.weldRows.size(); ++i) {
        if (schema.weldRows[i].number != static_cast<int>(i) + 1) {
            result.ok = false;
            result.problems.push_back(L"kaynak satiri numarasi sirali degil");
            break;
        }
        if (schema.weldRows[i].size <= 0.0) {
            result.ok = false;
            result.problems.push_back(L"kaynak satiri olcusu <= 0");
        }
    }

    // Grup id'leri + parametre referanslari.
    std::unordered_set<std::wstring> groupIds;
    for (const auto& g : schema.groups) {
        if (g.id.empty()) {
            result.ok = false;
            result.problems.push_back(L"grup id bos");
        } else if (!groupIds.insert(g.id).second) {
            result.ok = false;
            result.problems.push_back(L"yinelenen grup id: " + g.id);
        }
        for (const auto& entry : g.paramNames) {
            std::vector<std::wstring> parts;
            const auto bar = entry.find(L'|');
            if (bar == std::wstring::npos) parts.push_back(entry);
            else { parts.push_back(entry.substr(0, bar)); parts.push_back(entry.substr(bar + 1)); }
            for (const auto& pn : parts) {
                if (specNames.find(pn) == specNames.end()) {
                    result.ok = false;
                    result.problems.push_back(L"grup " + g.id + L" bilinmeyen parametreyi isaret ediyor: " +
                                              pn);
                }
            }
        }
        for (const auto& pr : g.partRows) {
            for (const std::wstring* pn : {&pr.t, &pr.b, &pr.h, &pr.profile, &pr.posPrefix,
                                           &pr.posStart, &pr.asmPrefix, &pr.asmStart,
                                           &pr.material, &pr.name, &pr.finish}) {
                if (!pn->empty() && specNames.find(*pn) == specNames.end()) {
                    result.ok = false;
                    result.problems.push_back(L"grup " + g.id + L" parca satiri '" + pr.label +
                                              L"' bilinmeyen parametreyi isaret ediyor: " + *pn);
                }
            }
        }
    }

    return result;
}

std::optional<std::wstring> hitTestDiagramHotspot(const ComponentDialogSchema& schema,
                                                  DiagramPoint p) noexcept {
    std::optional<std::wstring> best;
    double bestDist = 0.0;
    bool found = false;
    for (const auto& h : schema.hotspots) {
        const double dx = p.x - h.position.x;
        const double dy = p.y - h.position.y;
        const double d = std::hypot(dx, dy);
        if (d <= h.radius && (!found || d < bestDist)) {
            best = h.id;
            bestDist = d;
            found = true;
        }
    }
    return best;
}

const DiagramHotspot* findHotspot(const ComponentDialogSchema& schema,
                                  const std::wstring& id) noexcept {
    for (const auto& h : schema.hotspots)
        if (h.id == id) return &h;
    return nullptr;
}

// --- End Plate parametre adaptoru -------------------------------------------

std::vector<CompParamSpec> endPlateDialogSpecs() noexcept {
    return {
        // Levha / geometri
        lengthParam(L"plateThickness", L"mm", 12.0, 4.0, 100.0, L"levha kalinligi"),
        lengthParam(L"plateExtra", L"mm", 20.0, 0.0, 200.0, L"yatay kenar payi"),
        lengthParam(L"topExtra", L"mm", -1.0, -1.0, 500.0, L"ust pay (legacy; -1 = plateExtra)"),
        lengthParam(L"bottomExtra", L"mm", -1.0, -1.0, 500.0, L"alt pay (legacy; -1 = plateExtra)"),
        lengthParamOpen(L"upperEdgeDistance", L"mm", -1e9, L"ust kenar mesafesi (Tekla; < -1e8 = tanimsiz)"),
        lengthParamOpen(L"lowerEdgeDistance", L"mm", -1e9, L"alt kenar mesafesi (Tekla; < -1e8 = tanimsiz)"),
        lengthParamOpen(L"plateWidthOverride", L"mm", 0.0, L"acik genislik (0 = otomatik)"),
        lengthParamOpen(L"plateHeightOverride", L"mm", 0.0, L"acik yukseklik (0 = otomatik)"),
        stringParam(L"plateMaterial", L"", L"levha malzemesi"),
        stringParam(L"plateName", L"", L"levha adi"),
        stringParam(L"plateFinish", L"", L"levha yuzey islemi"),
        lengthParam(L"gap", L"mm", 0.0, 0.0, 100.0, L"kolon-levha boslugu"),
        // Bulonlar
        lengthParam(L"boltDiameter", L"mm", 20.0, 12.0, 30.0, L"bulon capi"),
        lengthParam(L"holeDiameter", L"mm", 22.0, 12.0, 34.0, L"delik capi"),
        enumParam(L"grade", L"8.8", {L"8.8", L"10.9"}, L"bulon sinifi"),
        intParam(L"boltCountX", 2, 1, 8, L"genislik yonunde bulon adedi"),
        intParam(L"boltCountY", 2, 1, 8, L"yukseklik yonunde bulon adedi"),
        lengthParam(L"boltEdge", L"mm", 30.0, 10.0, 200.0, L"kenar mesafesi"),
        enumParam(L"boltAlignH", L"Orta", {L"Sol", L"Orta", L"Sag"}, L"yatay hiza"),
        enumParam(L"boltAlignV", L"Alt", {L"Ust", L"Orta", L"Alt"}, L"dikey hiza"),
        lengthParamOpen(L"boltOffsetH", L"mm", 0.0, L"yatay konum olcusu"),
        lengthParamOpen(L"boltOffsetV", L"mm", -1.0, L"dikey konum olcusu (<0 = boltEdge)"),
        lengthParamOpen(L"boltSpacingX", L"mm", 0.0, L"yatay aralik (0 = otomatik)"),
        lengthParamOpen(L"boltSpacingY", L"mm", 0.0, L"dikey aralik (0 = otomatik)"),
        boolParam(L"hasHead", true, L"bas"),
        boolParam(L"hasNut", true, L"somun"),
        boolParam(L"hasWasher", true, L"pul"),
    };
}

std::vector<CompParam> endPlateToParams(const EndPlateParams& p) noexcept {
    std::vector<CompParam> params;
    const auto& ah = alignHChoices();
    const auto& av = alignVChoices();

    params.push_back({L"plateThickness", p.plateThickness});
    params.push_back({L"plateExtra", p.plateExtra});
    params.push_back({L"topExtra", p.topExtra});
    params.push_back({L"bottomExtra", p.bottomExtra});
    params.push_back({L"upperEdgeDistance", p.upperEdgeDistance});
    params.push_back({L"lowerEdgeDistance", p.lowerEdgeDistance});
    params.push_back({L"plateWidthOverride", p.plateWidthOverride});
    params.push_back({L"plateHeightOverride", p.plateHeightOverride});
    params.push_back({L"plateMaterial", widen(p.plateMaterial)});
    params.push_back({L"plateName", widen(p.plateName)});
    params.push_back({L"plateFinish", widen(p.plateFinish)});
    params.push_back({L"gap", p.gap});
    params.push_back({L"boltDiameter", p.boltDiameter});
    params.push_back({L"holeDiameter", p.holeDiameter});
    params.push_back({L"grade", widen(p.boltGrade)});
    params.push_back({L"boltCountX", p.boltCountX});
    params.push_back({L"boltCountY", p.boltCountY});
    params.push_back({L"boltEdge", p.boltEdge});
    params.push_back({L"boltAlignH", ah[static_cast<std::size_t>(clampI(p.boltAlignH, 0, 2))]});
    params.push_back({L"boltAlignV", av[static_cast<std::size_t>(clampI(p.boltAlignV, 0, 2))]});
    params.push_back({L"boltOffsetH", p.boltOffsetH});
    params.push_back({L"boltOffsetV", p.boltOffsetV});
    params.push_back({L"boltSpacingX", p.boltSpacingX});
    params.push_back({L"boltSpacingY", p.boltSpacingY});
    params.push_back({L"hasHead", p.hasHead});
    params.push_back({L"hasNut", p.hasNut});
    params.push_back({L"hasWasher", p.hasWasher});
    return params;
}

EndPlateParams endPlateFromParams(const std::vector<CompParam>& params) noexcept {
    EndPlateParams p;
    p.plateThickness = paramAsDouble(params, L"plateThickness", p.plateThickness);
    p.plateExtra = paramAsDouble(params, L"plateExtra", p.plateExtra);
    p.topExtra = paramAsDouble(params, L"topExtra", p.topExtra);
    p.bottomExtra = paramAsDouble(params, L"bottomExtra", p.bottomExtra);
    p.upperEdgeDistance = paramAsDouble(params, L"upperEdgeDistance", p.upperEdgeDistance);
    p.lowerEdgeDistance = paramAsDouble(params, L"lowerEdgeDistance", p.lowerEdgeDistance);
    p.plateWidthOverride = paramAsDouble(params, L"plateWidthOverride", p.plateWidthOverride);
    p.plateHeightOverride = paramAsDouble(params, L"plateHeightOverride", p.plateHeightOverride);
    p.plateMaterial = narrow(paramAsString(params, L"plateMaterial", L""));
    p.plateName = narrow(paramAsString(params, L"plateName", L""));
    p.plateFinish = narrow(paramAsString(params, L"plateFinish", L""));
    p.gap = paramAsDouble(params, L"gap", p.gap);
    p.boltDiameter = paramAsDouble(params, L"boltDiameter", p.boltDiameter);
    p.holeDiameter = paramAsDouble(params, L"holeDiameter", p.holeDiameter);
    {
        std::wstring g = paramAsString(params, L"grade", L"8.8");
        if (enumIndex(gradeChoices(), g) < 0) g = L"8.8";  // gecersiz sinif -> varsayilan
        p.boltGrade = narrow(g);
    }
    p.boltCountX = paramAsInt(params, L"boltCountX", p.boltCountX);
    p.boltCountY = paramAsInt(params, L"boltCountY", p.boltCountY);
    p.boltEdge = paramAsDouble(params, L"boltEdge", p.boltEdge);
    {
        const int h = enumIndex(alignHChoices(), paramAsString(params, L"boltAlignH", L"Orta"));
        p.boltAlignH = (h < 0) ? 1 : h;
    }
    {
        const int v = enumIndex(alignVChoices(), paramAsString(params, L"boltAlignV", L"Alt"));
        p.boltAlignV = (v < 0) ? 2 : v;
    }
    p.boltOffsetH = paramAsDouble(params, L"boltOffsetH", p.boltOffsetH);
    p.boltOffsetV = paramAsDouble(params, L"boltOffsetV", p.boltOffsetV);
    p.boltSpacingX = paramAsDouble(params, L"boltSpacingX", p.boltSpacingX);
    p.boltSpacingY = paramAsDouble(params, L"boltSpacingY", p.boltSpacingY);
    p.hasHead = paramAsBool(params, L"hasHead", p.hasHead);
    p.hasNut = paramAsBool(params, L"hasNut", p.hasNut);
    p.hasWasher = paramAsBool(params, L"hasWasher", p.hasWasher);
    return p;
}

// --- Registry ----------------------------------------------------------------

ComponentDialogRegistry& ComponentDialogRegistry::instance() noexcept {
    static ComponentDialogRegistry reg;
    return reg;
}

bool ComponentDialogRegistry::registerDialog(ComponentDialogDefinition def) noexcept {
    if (def.typeId.empty() || def.specs.empty()) return false;
    for (const auto& d : defs_)
        if (d.typeId == def.typeId) return false;
    defs_.push_back(std::move(def));
    return true;
}

const ComponentDialogDefinition* ComponentDialogRegistry::find(const std::wstring& typeId) const noexcept {
    for (const auto& d : defs_)
        if (d.typeId == typeId) return &d;
    return nullptr;
}

// --- Yerlesik dialog tanimlari ----------------------------------------------

ComponentDialogDefinition endPlateDialogDefinition() noexcept {
    ComponentDialogDefinition def;
    def.typeId = L"beam_to_column_end_plate";
    def.schema.typeId = def.typeId;
    def.specs = endPlateDialogSpecs();

    // Sematik (yan gorunus): kolon solda, kiris sagda, alin levhasi arada.
    ComponentDiagramItem column;
    column.id = L"column";
    column.primitives.push_back(rectP(20.0, 20.0, 48.0, 220.0, true));

    ComponentDiagramItem plate;
    plate.id = L"plate";
    plate.primitives.push_back(rectP(60.0, 48.0, 88.0, 212.0, true));

    ComponentDiagramItem beam;
    beam.id = L"beam";
    beam.primitives.push_back(rectP(88.0, 104.0, 340.0, 156.0, true));

    ComponentDiagramItem bolts;
    bolts.id = L"bolts";
    bolts.primitives.push_back(circleP(67.0, 70.0, 4.0));
    bolts.primitives.push_back(circleP(81.0, 70.0, 4.0));
    bolts.primitives.push_back(circleP(67.0, 190.0, 4.0));
    bolts.primitives.push_back(circleP(81.0, 190.0, 4.0));

    ComponentDiagramItem dimT;
    dimT.id = L"dim_thickness";
    dimT.primitives.push_back(lineP(60.0, 224.0, 88.0, 224.0, true));
    dimT.primitives.push_back(textP(70.0, 228.0, L"t"));

    ComponentDiagramItem dimG;
    dimG.id = L"dim_gap";
    dimG.primitives.push_back(lineP(48.0, 224.0, 60.0, 224.0, true));
    dimG.primitives.push_back(textP(49.0, 228.0, L"gap"));

    ComponentDiagramItem dimUpper;
    dimUpper.id = L"dim_upper";
    dimUpper.primitives.push_back(lineP(110.0, 156.0, 110.0, 212.0, true));
    dimUpper.primitives.push_back(textP(114.0, 184.0, L"up"));

    ComponentDiagramItem dimLower;
    dimLower.id = L"dim_lower";
    dimLower.primitives.push_back(lineP(110.0, 104.0, 110.0, 48.0, true));
    dimLower.primitives.push_back(textP(114.0, 72.0, L"dn"));

    def.schema.items = {column, plate, beam, bolts, dimT, dimG, dimUpper, dimLower};

    def.schema.hotspots = {
        hotspot(L"hs_thickness", 74.0, 224.0, 8.0, std::wstring(L"plateThickness"), std::wstring(L"1")),
        hotspot(L"hs_gap", 54.0, 224.0, 8.0, std::wstring(L"gap"), std::wstring(L"2")),
        hotspot(L"hs_upper", 120.0, 184.0, 8.0, std::wstring(L"upperEdgeDistance"), std::wstring(L"3")),
        hotspot(L"hs_lower", 120.0, 76.0, 8.0, std::wstring(L"lowerEdgeDistance"), std::wstring(L"4")),
        hotspot(L"hs_bolt_edge", 44.0, 130.0, 8.0, std::wstring(L"boltEdge"), std::wstring(L"5")),
        hotspot(L"hs_bolt_count_v", 74.0, 130.0, 8.0, std::wstring(L"boltCountY"), std::wstring(L"6")),
        hotspot(L"hs_bolt_count_h", 108.0, 130.0, 8.0, std::wstring(L"boltCountX"), std::wstring(L"7")),
        hotspot(L"hs_bolt_align_v", 74.0, 30.0, 8.0, std::wstring(L"boltAlignV"), std::wstring(L"8")),
    };

    ComponentDialogGroup gPlates;
    gPlates.id = L"plates";
    gPlates.title = L"Levha";
    gPlates.paramNames = {L"plateThickness", L"plateExtra", L"topExtra", L"bottomExtra",
                          L"upperEdgeDistance", L"lowerEdgeDistance", L"plateWidthOverride",
                          L"plateHeightOverride", L"plateMaterial", L"plateName",
                          L"plateFinish", L"gap"};

    ComponentDialogGroup gBolts;
    gBolts.id = L"bolts";
    gBolts.title = L"Bulonlar";
    gBolts.paramNames = {L"boltDiameter", L"holeDiameter", L"grade", L"boltCountX",
                         L"boltCountY", L"boltEdge", L"boltAlignH", L"boltAlignV",
                         L"boltOffsetH", L"boltOffsetV", L"boltSpacingX", L"boltSpacingY",
                         L"hasHead", L"hasNut", L"hasWasher"};

    def.schema.groups = {gPlates, gBolts};
    return def;
}

ComponentDialogDefinition finPlateDialogDefinition() noexcept {
    ComponentDialogDefinition def;
    def.typeId = L"fin_plate";
    def.schema.typeId = def.typeId;
    // Engine semasi TEK KAYNAK (finPlateComponentType) — dialog ondan turer.
    def.specs = finPlateComponentType().params;

    ComponentDiagramItem column;
    column.id = L"column";
    column.primitives.push_back(rectP(20.0, 20.0, 48.0, 220.0, true));

    ComponentDiagramItem plate;
    plate.id = L"plate";
    plate.primitives.push_back(rectP(48.0, 80.0, 78.0, 180.0, true));

    ComponentDiagramItem beam;
    beam.id = L"beam";
    beam.primitives.push_back(rectP(78.0, 110.0, 340.0, 150.0, true));

    ComponentDiagramItem bolts;
    bolts.id = L"bolts";
    bolts.primitives.push_back(circleP(63.0, 105.0, 4.0));
    bolts.primitives.push_back(circleP(63.0, 130.0, 4.0));
    bolts.primitives.push_back(circleP(63.0, 155.0, 4.0));

    def.schema.items = {column, plate, beam, bolts};

    def.schema.hotspots = {
        hotspot(L"hs_thickness", 63.0, 192.0, 8.0, std::wstring(L"plateThickness"), std::wstring(L"1")),
        hotspot(L"hs_height", 63.0, 72.0, 8.0, std::wstring(L"plateHeight"), std::wstring(L"2")),
        hotspot(L"hs_width", 100.0, 130.0, 8.0, std::wstring(L"plateWidth"), std::wstring(L"3")),
        hotspot(L"hs_bolt_edge", 44.0, 105.0, 8.0, std::wstring(L"boltEdge"), std::wstring(L"4")),
        hotspot(L"hs_bolt_count", 63.0, 130.0, 8.0, std::wstring(L"boltCount"), std::wstring(L"5")),
    };

    ComponentDialogGroup gPlate;
    gPlate.id = L"plate";
    gPlate.title = L"Levha";
    gPlate.paramNames = {L"plateThickness", L"plateWidth", L"plateHeight"};

    ComponentDialogGroup gBolts;
    gBolts.id = L"bolts";
    gBolts.title = L"Bulonlar";
    gBolts.paramNames = {L"boltDiameter", L"holeDiameter", L"boltCount", L"boltEdge", L"grade"};

    def.schema.groups = {gPlate, gBolts};
    return def;
}

void registerBuiltInDialogs(ComponentDialogRegistry& registry) noexcept {
    registry.registerDialog(endPlateDialogDefinition());
    registry.registerDialog(finPlateDialogDefinition());
    registry.registerDialog(shearPlate146DialogDefinition());
}

/* -------------------------------------------------------------------------
   Shear plate simple (component 146) – modern Tekla‑style dialog
   ------------------------------------------------------------------------- */
ComponentDialogDefinition shearPlate146DialogDefinition() noexcept {
    ComponentDialogDefinition def;
    def.typeId = L"tekla_shear_plate_146";
    def.schema.typeId = def.typeId;
    // Engine semasi TEK KAYNAK (shearPlate146ComponentType); 146 core = fin plate
    // core. Fazla Tekla 146 parametreleri blueprint'te documented_only olarak
    // durur; dialog YALNIZ UYGULANAN alanlari gosterir (unsupported gizlenmez,
    // bu dilimde dialogda acilmaz — coverage'ta raporlanir).
    def.specs = shearPlate146ComponentType().params;
    // Tekla diyalogunda bu satirlarin solunda "Default" onay kutusu vardir:
    // isaretsiz birakilirsa motor varsayilani kullanilir (parametre yazilmaz).
    {
        const std::wstring withDefaultBox[] = {
            L"boltStandard", L"tolerance", L"threadInMat", L"holeType",
            L"rotateSlots", L"slotsIn", L"cutLength", L"comment"};
        for (auto& s : def.specs)
            for (const auto& n : withDefaultBox)
                if (s.name == n) s.optional = true;
    }

    // --- TEKLA 146 ALANLARI, GEOMETRISI YOK (documentedOnly) -----------------
    // Kaynak: support.tekla.com .../conn_146_help. Diyalog Tekla duzenine
    // benzesin diye gosterilir; alanlar GRI'dir ve sonuca yazilmaz (motor
    // okumaz). Geometri yazildikca ilgili alan documentedOnly=false yapilir ve
    // engine ComponentType'a tasinir.
    const auto docLen = [&](const wchar_t* n, const wchar_t* label, double d) {
        CompParamSpec s = lengthParamOpen(n, L"mm", d, label);
        s.documentedOnly = true;
        def.specs.push_back(s);
    };
    const auto docEnum = [&](const wchar_t* n, const wchar_t* label,
                             std::vector<std::wstring> choices) {
        CompParamSpec s = enumParam(n, choices.front(), choices, label);
        s.documentedOnly = true;
        def.specs.push_back(s);
    };
    const auto docStr = [&](const wchar_t* n, const wchar_t* label) {
        CompParamSpec s = stringParam(n, L"", label);
        s.documentedOnly = true;
        def.specs.push_back(s);
    };
    const std::vector<std::wstring> chamferTypes = {
        L"Default", L"No chamfer", L"Line chamfer", L"Convex arc", L"Concave arc"};

    // Picture sekmesi (kiris kesimleri)
    docLen(L"flangeStripSize", L"Flange strip size", 20.0);
    docEnum(L"beamWebCut", L"Beam web cut", {L"Default", L"Bevel", L"Square"});
    docEnum(L"beamFlangeCut", L"Beam flange cut", {L"Default", L"Bevel", L"Square"});
    docEnum(L"beamBottomFlangeCut", L"Beam bottom flange cut",
            {L"Default", L"Notch", L"Flange cut"});
    docLen(L"bottomNotchRadius", L"Bottom notch radius", 0.0);
    docLen(L"bottomNotchHeight", L"Bottom notch height", 0.0);

    // Plates sekmesi
    docStr(L"tabMaterial", L"Tab plate material");
    docStr(L"tabName", L"Tab plate name");
    docStr(L"tabFinish", L"Tab plate finish");
    docEnum(L"tabPositionSloped", L"Shear tab position (sloped)",
            {L"Default", L"Same level in main part", L"Same length"});
    docLen(L"tabEdgeDistance", L"Shear tab edge distance", 0.0);
    docEnum(L"tabEndCut", L"Shear tab end cut", {L"Default", L"Square", L"Bevel"});
    docEnum(L"tabChamferType", L"Shear tab chamfer type", chamferTypes);
    docLen(L"tabChamferH", L"Shear tab chamfer (horizontal)", 0.0);
    docLen(L"tabChamferV", L"Shear tab chamfer (vertical)", 0.0);
    docEnum(L"tabOrientation", L"Shear tab orientation",
            {L"Default", L"Automatic", L"Sloped", L"Square", L"Modified sloped"});
    docEnum(L"plateWasherType", L"Plate washer",
            {L"Default", L"No plate washer", L"One plate washer",
             L"Square washers (each bolt)", L"Round washers (each bolt)"});
    docEnum(L"plateWasherSide", L"Plate washer side", {L"Default", L"One tab", L"Both tabs"});
    docLen(L"plateWasherT", L"Plate washer thickness", 0.0);
    docLen(L"plateWasherW", L"Plate washer width", 0.0);
    docLen(L"plateWasherH", L"Plate washer height", 0.0);

    // Stiffeners sekmesi
    docLen(L"stiffenerT", L"Opposite web stiffener thickness", 0.0);
    docLen(L"stiffenerW", L"Opposite web stiffener width", 0.0);
    docLen(L"stiffenerH", L"Opposite web stiffener height", 0.0);
    docLen(L"stiffenerGapFlange", L"Stiffener gap (to flanges)", 0.0);
    docEnum(L"stiffenerOrientation", L"Stiffener orientation",
            {L"Default", L"Perpendicular to main part", L"Parallel to secondary part"});
    docEnum(L"stiffenerChamferType", L"Stiffener chamfer type", chamferTypes);
    docLen(L"stiffenerChamferH", L"Stiffener chamfer (horizontal)", 0.0);
    docLen(L"stiffenerChamferV", L"Stiffener chamfer (vertical)", 0.0);

    // Haunch sekmesi
    docEnum(L"haunchCreation", L"Haunch plate creation",
            {L"Default", L"Automatic", L"Top and bottom", L"No haunch plates"});
    docLen(L"haunchTopT", L"Top haunch plate thickness", 0.0);
    docLen(L"haunchTopW", L"Top haunch plate width", 0.0);
    docLen(L"haunchTopH", L"Top haunch plate height", 0.0);
    docLen(L"haunchBottomT", L"Bottom haunch plate thickness", 0.0);
    docLen(L"haunchBottomW", L"Bottom haunch plate width", 0.0);
    docLen(L"haunchBottomH", L"Bottom haunch plate height", 0.0);
    docLen(L"haunchTopChamferW", L"Top haunch chamfer width", 0.0);
    docLen(L"haunchTopChamferH", L"Top haunch chamfer height", 0.0);
    docLen(L"haunchBottomChamferH", L"Bottom haunch chamfer height", 0.0);
    docLen(L"haunchBottomChamferW", L"Bottom haunch chamfer width", 0.0);

    // Notch sekmesi - otomatik
    docEnum(L"notchShape", L"Notch shape (automatic)",
            {L"Default", L"Square to main web", L"Square to secondary web",
             L"Mixed", L"No automatic notch", L"Both sides"});
    docEnum(L"notchSizeRef", L"Notch size measured from",
            {L"Default", L"Flange edge / under top flange", L"Center line / top flange",
             L"Flange edge / outer top flange"});
    docEnum(L"notchFlangeCutShape", L"Flange cut shape",
            {L"Default", L"Parallel to main beam", L"Square"});
    docEnum(L"notchRounding", L"Notch dimension rounding",
            {L"Default", L"Not rounded", L"Rounded"});
    docLen(L"notchRoundH", L"Rounding (horizontal)", 0.0);
    docLen(L"notchRoundV", L"Rounding (vertical)", 0.0);
    docEnum(L"notchPosition", L"Notch position",
            {L"Default", L"Below main flange", L"Above main flange"});
    docEnum(L"notchChamfer", L"Notch chamfer", {L"Default", L"No chamfer", L"Line", L"Radius"});
    docLen(L"notchChamferRadius", L"Notch chamfer radius", 0.0);
    // Notch sekmesi - manuel
    // Manuel notch: Tekla'da ust ve alt flans icin AYRI alan seti vardir.
    for (const wchar_t* sfx : {L"Top", L"Bottom"}) {
        const std::wstring S = sfx;
        const std::wstring side = (S == L"Top") ? L" (top flange)" : L" (bottom flange)";
        docEnum((L"flangeNotchSide" + S).c_str(), (L"Side of flange notch" + side).c_str(),
                {L"Default", L"Automatic", L"Both sides", L"Near side", L"Far side"});
        docEnum((L"flangeNotchShape" + S).c_str(), (L"Flange notch shape" + side).c_str(),
                {L"Default", L"Automatic", L"Chamfer", L"Default values", L"No cut",
                 L"Flush with web", L"Fields 1 and 2"});
        docEnum((L"flangeNotchDepthRef" + S).c_str(), (L"Flange notch depth" + side).c_str(),
                {L"Default", L"Depth", L"From web center line"});
        docLen((L"flangeNotchDepth" + S).c_str(), (L"Flange notch depth value" + side).c_str(), 0.0);
        docLen((L"notchCutH" + S).c_str(), (L"Cut dimension 1 (horizontal)" + side).c_str(), 10.0);
        docLen((L"notchCutV" + S).c_str(), (L"Cut dimension 2 (vertical)" + side).c_str(), 0.0);
        docLen((L"notchWebToFlange" + S).c_str(), (L"Dimension from web to flange cut" + side).c_str(), 0.0);
    }
    docEnum(L"notchBcsa", L"BCSA notch definition", {L"Default", L"Yes", L"No"});
    docLen(L"notchBcsaValue", L"BCSA notch value", 0.0);
    docLen(L"notchWebClearance", L"Cut clearance from beam web", 0.0);

    // Bolts sekmesi
    docEnum(L"boltStagger", L"Staggering of bolts",
            {L"Default", L"Not staggered", L"Staggered type 1", L"Staggered type 2"});
    docEnum(L"boltHorizRef", L"Horizontal dimension measured to",
            {L"Default", L"Secondary part", L"Main part web"});
    docEnum(L"boltVertRef", L"Vertical dimension measured to",
            {L"Default", L"Secondary part (sloped)", L"Secondary part",
             L"Main part", L"Main part center line"});

    // --- Tekla ekran goruntuleri + yardim PDF'i ile tamamlanan alanlar ---------
    // (conn_146_help PDF'i, 23.10.2026 + kullanicinin gercek diyalog ekran
    // goruntuleri). Hepsi GRI: motor okumaz, sonuca yazilmaz.
    const auto docBool = [&](const wchar_t* n, const wchar_t* label, bool d) {
        CompParamSpec s = boolParam(n, d, label);
        s.documentedOnly = true;
        def.specs.push_back(s);
    };
    const auto docInt = [&](const wchar_t* n, const wchar_t* label, int d) {
        CompParamSpec s = intParam(n, d, 0, 1000, label);
        s.documentedOnly = true;
        def.specs.push_back(s);
    };
    // Parca satiri (Tekla: t b h | Pos_No | Material | Name | Finish).
    // withAsm: Pos_No'nun ikinci satiri (montaj/assembly numarasi).
    const auto docPart = [&](const std::wstring& p, const std::wstring& title,
                             bool withAsm, bool withMaterial) {
        docStr((p + L"PosPrefix").c_str(), (title + L" Pos_No prefix").c_str());
        docInt((p + L"PosStart").c_str(), (title + L" Pos_No start number").c_str(), 1);
        if (withAsm) {
            docStr((p + L"AsmPrefix").c_str(), (title + L" assembly Pos_No prefix").c_str());
            docInt((p + L"AsmStart").c_str(), (title + L" assembly Pos_No start number").c_str(), 1);
        }
        if (withMaterial) {
            docStr((p + L"Material").c_str(), (title + L" material").c_str());
            docStr((p + L"Name").c_str(), (title + L" name").c_str());
            docStr((p + L"Finish").c_str(), (title + L" finish").c_str());
        }
    };
    const std::vector<std::wstring> siteWorkshop = {L"Default", L"Site", L"Workshop"};

    // Picture: beam gap olcum yonu + kiris ucu hazirligi (x kutulari Beam cut'ta)
    docEnum(L"beamGapMeasure", L"Beam gap measured",
            {L"Default", L"Square to main part", L"Along secondary part"});
    // Plates: tab + plate washer parca satirlari (tab malzeme/ad/yuzey yukarida tanimli)
    docPart(L"tab", L"Tab plate", true, false);
    docPart(L"washer", L"Plate washer", true, true);
    // Stiffeners
    docPart(L"stiffener", L"Web stiffener", false, true);
    // Haunch: ust + alt plaka ayri satirlar
    docPart(L"haunchTop", L"Top plate", false, true);
    docPart(L"haunchBottom", L"Bottom plate", false, true);

    // General sekmesi (ekran goruntusu). Dropdown'lardaki diger secenekler
    // goruntude gorunmedigi icin YAZILMADI (uydurulmaz): yalniz gorunen deger.
    docEnum(L"upDirection", L"Up direction", {L"auto"});
    docLen(L"upDirection2", L"Up direction (2)", 0.0);
    docLen(L"upDirection3", L"Up direction (3)", 0.0);
    docEnum(L"locked", L"Locked", {L"No", L"Yes"});
    docInt(L"classNo", L"Class", 99);
    docStr(L"connectionCode", L"Connection code");
    docEnum(L"autoDefaultsGroup", L"AutoDefaults rule group", {L"None"});
    docEnum(L"autoConnectionGroup", L"AutoConnection rule group", {L"None"});

    // Design type sekmesi (ekran goruntusu; dropdown'larda yalniz \"Default\" gorunur)
    docEnum(L"externalDesign", L"External design", {L"Default"});
    docEnum(L"checkConnection", L"Check connection", {L"Default"});
    docEnum(L"designCode", L"Design code", {L"Default"});
    docEnum(L"loadDefinition", L"Load definition", {L"Default"});
    docStr(L"udlPercent", L"UDL %");
    docStr(L"loadShear", L"Loads from secondary: Shear, V");
    docStr(L"loadTension", L"Loads from secondary: Tension, T");
    docStr(L"loadMoment", L"Loads from secondary: Moment, M");

    // Bolts: eksik secenekler
    docEnum(L"boltStaggerExtra", L"Staggering (type 3/4)",
            {L"Default", L"Staggered type 3", L"Staggered type 4"});
    docEnum(L"boltGroupOrientation", L"Bolt group orientation",
            {L"Default", L"Automatic", L"Staggered", L"Square", L"Sloped"});
    docEnum(L"boltingDirection", L"Bolting direction",
            {L"Default", L"Bolting direction 1", L"Bolting direction 2"});
    docEnum(L"boltSiteWorkshop", L"Site/Workshop", siteWorkshop);
    docLen(L"slotV", L"Slotted hole 1 (vertical)", 0.0);
    docLen(L"slotH", L"Slotted hole 2 (horizontal / oversize)", 0.0);
    // Bolt assembly: ekranda 6 onay kutusu var, etiketi yok (simgeler). Varsayilanlar
    // goruntudeki isaret durumundan okundu: 1 ve 5-6 isaretli.
    {
        const bool defs[6] = {true, false, false, false, true, true};
        for (int i = 0; i < 6; ++i) {
            const std::wstring n = L"asm" + std::to_wstring(i + 1);
            const std::wstring l = L"Bolt assembly part " + std::to_wstring(i + 1);
            docBool(n.c_str(), l.c_str(), defs[i]);
        }
    }
    docBool(L"asmEffectInModify", L"Effect in modify", true);
    docLen(L"boltLengthIncrease", L"Bolt length increase", 0.0);

    // Beam cut sekmesi (weld backing bar, weld access hole, beam end preparation)
    docLen(L"backingBarW", L"Backing bar b", 0.0);
    docPart(L"backingBar", L"Backing bar", true, true);
    docLen(L"wahGapTop", L"1 Gap: secondary top flange - main part", 0.0);
    docLen(L"wahVertical", L"2 Weld access hole vertical", 0.0);
    docLen(L"wahHorizontal", L"3 Weld access hole horizontal", 0.0);
    docLen(L"wahGapWeb", L"4 Gap: secondary web - main part (+Picture gap)", 0.0);
    docLen(L"wahGapBottom", L"5 Gap: secondary bottom flange - main part", 0.0);
    docEnum(L"weldAccessHoleType", L"Weld access holes",
            {L"Default", L"Round", L"Square", L"Diagonal", L"Round (radius)",
             L"Extended cone", L"Cone"});
    docLen(L"wahRadiusR", L"R", 35.0);
    docLen(L"wahRadiusSmall", L"r", 10.0);
    docLen(L"beamPrepTopX", L"Top Prep x", 0.0);
    docLen(L"beamPrepBottomX", L"Bottom Prep x", 0.0);
    docEnum(L"beamEndPreparation", L"Beam end preparation",
            {L"Default", L"Automatic", L"Not prepared", L"Top and bottom flange",
             L"Top flange", L"Bottom flange"});
    docEnum(L"flangeCutTop", L"Flange cut (top)", {L"Default", L"Not cut", L"Cut"});
    docEnum(L"flangeCutBottom", L"Flange cut (bottom)", {L"Default", L"Not cut", L"Cut"});
    docEnum(L"backingBars", L"Weld backing bars",
            {L"Default", L"None", L"Inside flanges", L"Outside flanges"});
    docEnum(L"backingBarLengthType", L"Weld backing bar length",
            {L"Default", L"Absolute length", L"Extension beyond flange edge"});
    docLen(L"backingBarLength", L"Weld backing bar length value", 0.0);
    docLen(L"backingBarPosition", L"Weld backing bar position (+/-)", 0.0);
    docEnum(L"backingBarAssembly", L"Assembly type", siteWorkshop);

    // Angle box sekmesi (seat angle): Stiffeners / Top angle / Bottom angle satirlari
    docLen(L"seatStiffT", L"Seat stiffener t", 0.0);
    docLen(L"seatStiffW", L"Seat stiffener b", 0.0);
    docLen(L"seatStiffH", L"Seat stiffener h", 0.0);
    docPart(L"seatStiff", L"Seat stiffener", false, true);
    docStr(L"seatTopProfile", L"Top angle profile");
    docPart(L"seatTop", L"Top angle", false, true);
    docStr(L"seatBottomProfile", L"Bottom angle profile");
    docPart(L"seatBottom", L"Bottom angle", false, true);
    docEnum(L"seatPosition", L"Seat angle position",
            {L"Default", L"No seat angle", L"Top", L"Bottom", L"Both"});
    const std::vector<std::wstring> seatAttach = {
        L"Default", L"Bolted", L"Welded-bolted", L"Bolted-welded", L"Welded"};
    docEnum(L"seatAttachTop", L"Seat angle attachment (top)", seatAttach);
    docEnum(L"seatAttachBottom", L"Seat angle attachment (bottom)", seatAttach);
    docEnum(L"seatStiffType", L"Stiffener type",
            {L"Default", L"Rectangular", L"Triangular", L"Leg-end line"});
    docLen(L"seatStiffOffsetH", L"Stiffener offset 1 (horizontal)", 0.0);
    docLen(L"seatStiffOffsetV", L"Stiffener offset 2 (vertical)", 0.0);
    docEnum(L"seatRotation", L"Seat angle rotation",
            {L"Default", L"Not rotated", L"Rotated 90"});
    docEnum(L"seatOrientation", L"Seat angle orientation",
            {L"Default", L"Long leg to secondary", L"Long leg to main", L"Automatic"});
    docEnum(L"seatSideStiff", L"Side stiffener position",
            {L"Default", L"None", L"Near side", L"Far side", L"Near and far"});
    docEnum(L"seatMiddleStiff", L"Middle stiffener position",
            {L"Default", L"None", L"Middle stiffeners", L"According to bolts"});
    docInt(L"seatMiddleStiffCount", L"Number of middle stiffeners", 0);
    docLen(L"seatOffset", L"Seat angle offset from main part center line", 0.0);
    docLen(L"seatGap", L"Gap (top/bottom) seat angle - secondary part", 0.0);
    docEnum(L"seatChamferType", L"Seat angle chamfer type", chamferTypes);
    docLen(L"seatChamferV", L"Seat angle chamfer 1 (vertical)", 0.0);
    docLen(L"seatChamferH", L"Seat angle chamfer 2 (horizontal)", 0.0);

    // BoxPBolts / BoxSBolts (seat angle -> main / secondary): gercek Tekla ekran
    // goruntusu. Sol kutu: Bolt size/standard/Tolerance/Thread in mat/Top/Bottom +
    // Comment; Top ve Bottom kutulari: oval delik 2 olcu + Hole type/Rotate Slots/
    // Slots in + bulon boyu artisi; ortada 5 bulon grubu olcusu (PDF'ten).
    // Goruntude acilir listelerde yalniz "Default" gorundugu icin Default disinda
    // secenek YAZILMADI (Hole type secenekleri PDF'ten).
    const auto docBoxBolts = [&](const std::wstring& p, bool toMain) {
        const auto name = [&](const wchar_t* sfx) { return (p + sfx); };
        docEnum(name(L"Size").c_str(), L"Bolt size", {L"Default"});
        docEnum(name(L"Standard").c_str(), L"Bolt standard", {L"Default"});
        docStr(name(L"Tolerance").c_str(), L"Tolerance");
        docEnum(name(L"ThreadInMat").c_str(), L"Thread in mat", {L"Default"});
        docEnum(name(L"SiteTop").c_str(), L"Top", {L"Default"});
        docEnum(name(L"SiteBottom").c_str(), L"Bottom", {L"Default"});
        docStr(name(L"Comment").c_str(), L"Comment");
        for (const wchar_t* side : {L"Top", L"Bottom"}) {
            const std::wstring S = side;
            docLen((p + S + L"SlotV").c_str(), L"Slotted hole 1 (vertical)", 0.0);
            docLen((p + S + L"SlotH").c_str(), L"Slotted hole 2 (horizontal)", 0.0);
            docEnum((p + S + L"HoleType").c_str(), L"Hole type",
                    {L"Default", L"Slotted", L"Oversized", L"No hole", L"Tapped"});
            docEnum((p + S + L"RotateSlots").c_str(), L"Rotate Slots:", {L"Default"});
            docEnum((p + S + L"SlotsIn").c_str(), L"Slots in", {L"Default"});
            docLen((p + S + L"LengthIncrease").c_str(), L"Bolt length increase", 0.0);
        }
        docLen(name(L"Pos1").c_str(),
               toMain ? L"1 Horizontal position (from secondary middle line)"
                      : L"1 Vertical position (from secondary middle line)", 0.0);
        docLen(name(L"Edge").c_str(), L"2 Bolt edge distance", 0.0);
        docInt(name(L"Count").c_str(), L"3 Number of bolts", 0);
        docStr(name(L"Spacing").c_str(), L"4 Bolt spacing");
        docLen(name(L"Pos5").c_str(),
               toMain ? L"5 Vertical position (from secondary bottom)"
                      : L"5 Horizontal position (from secondary bottom)", 0.0);
    };
    docBoxBolts(L"boxP", true);
    docBoxBolts(L"boxS", false);

    // Analysis sekmesi (gercek Tekla ekran goruntusu). Acilir listelerde goruntude
    // yalniz gorunen deger yazildi; "Restrain combination" resimli listedir ve
    // secili ogesi bir KALEM simgesidir (metni yok) -> simge karakteriyle gosterilir.
    docEnum(L"useAnalysisRestraints", L"Use analysis restraints", {L"Yes"});
    docEnum(L"memberSelection", L"Member selection", {L"Primary"});
    docEnum(L"restrainCombination", L"Restrain combination", {L"\u270E"});
    docEnum(L"supportCondition", L"Support condition", {L"Connected"});
    for (const wchar_t* ax : {L"Ux", L"Uy", L"Uz"}) {
        const std::wstring a = ax;
        docEnum((L"support" + a).c_str(), a.c_str(), {L"Free"});
        docLen((L"support" + a + L"Value").c_str(), (a + L" value").c_str(), 0.0);
    }
    for (const wchar_t* ax : {L"Rx", L"Ry", L"Rz"}) {
        const std::wstring a = ax;
        docEnum((L"support" + a).c_str(), a.c_str(), {L"Pinned"});
        docLen((L"support" + a + L"Value").c_str(), (a + L" value").c_str(), 0.0);
    }
    docLen(L"longitudinalMemberOffset", L"Longitudinal member offset", 0.0);
    docStr(L"analysisProfile", L"Analysis profile");
    docLen(L"analysisProfileLength", L"Analysis profile length", 0.0);

    // Ekran goruntusunde BoxP/BoxS ve Analysis'teki HER satirin solunda "Default"
    // onay kutusu var: bu alanlari optional yap (gri alanin kutusu da gri).
    for (auto& sp : def.specs) {
        const std::wstring& n = sp.name;
        const bool isBox = n.rfind(L"boxP", 0) == 0 || n.rfind(L"boxS", 0) == 0;
        const bool isAnalysis =
            n == L"useAnalysisRestraints" || n == L"memberSelection" ||
            n == L"restrainCombination" || n == L"supportCondition" ||
            n.rfind(L"supportU", 0) == 0 || n.rfind(L"supportR", 0) == 0 ||
            n == L"longitudinalMemberOffset" || n == L"analysisProfile" ||
            n == L"analysisProfileLength";
        if ((isBox || isAnalysis) && n.find(L"Value") == std::wstring::npos) sp.optional = true;
    }

    // --- SEMATIK (Picture sekmesi): Tekla 146 resmi -------------------------
    // Kolon (dikey) + turuncu kayma levhasi (2x2 bulon deligi) + kiris (yatay).
    ComponentDiagramItem main;
    main.id = L"main";
    main.primitives.push_back(rectP(18.0, 18.0, 52.0, 222.0, true));   // kolon

    ComponentDiagramItem plate;
    plate.id = L"plate";
    plate.primitives.push_back(rectP(52.0, 62.0, 84.0, 178.0, true));  // levha

    ComponentDiagramItem beam;
    beam.id = L"beam";
    beam.primitives.push_back(rectP(84.0, 92.0, 320.0, 148.0, true));  // kiris

    ComponentDiagramItem bolts;
    bolts.id = L"bolts";
    bolts.primitives.push_back(circleP(62.0, 100.0, 4.0));
    bolts.primitives.push_back(circleP(74.0, 100.0, 4.0));
    bolts.primitives.push_back(circleP(62.0, 140.0, 4.0));
    bolts.primitives.push_back(circleP(74.0, 140.0, 4.0));

    // Olcu cizgileri (Tekla resmindeki olcu oklarinin karsiligi).
    ComponentDiagramItem dims;
    dims.id = L"dims";
    dims.primitives.push_back(lineP(52.0, 186.0, 84.0, 186.0, true));   // genislik olcusu
    dims.primitives.push_back(lineP(52.0, 180.0, 52.0, 192.0));         // olcu basi (sol)
    dims.primitives.push_back(lineP(84.0, 180.0, 84.0, 192.0));         // olcu basi (sag)
    dims.primitives.push_back(lineP(92.0, 62.0, 92.0, 178.0, true));    // yukseklik olcusu
    dims.primitives.push_back(lineP(88.0, 62.0, 96.0, 62.0));
    dims.primitives.push_back(lineP(88.0, 178.0, 96.0, 178.0));
    dims.primitives.push_back(lineP(46.0, 100.0, 46.0, 140.0, true));   // bulon araligi

    def.schema.items = {main, plate, beam, bolts, dims};

    def.schema.hotspots = {
        hotspot(L"hs_thickness", 46.0, 120.0, 8.0, std::wstring(L"plateThickness"), std::wstring(L"1")),
        hotspot(L"hs_height", 92.0, 120.0, 8.0, std::wstring(L"plateHeight"), std::wstring(L"2")),
        hotspot(L"hs_width", 68.0, 186.0, 8.0, std::wstring(L"plateWidth"), std::wstring(L"3")),
        hotspot(L"hs_bolt_edge", 46.0, 100.0, 8.0, std::wstring(L"boltEdge"), std::wstring(L"4")),
        hotspot(L"hs_bolt_count", 62.0, 120.0, 8.0, std::wstring(L"boltCount"), std::wstring(L"5")),
    };

    // --- KAYNAK TABLOSU + CAGRILAR (Tekla "Welds") ---------------------------
    // Tekla 146 kaynak diyalogu: 10 satirlik tablo ve semada 1..10 numarali
    // cagrilar. Burada varsayilan dikisler: levha-kolon (dikey iki kenar),
    // levha-kiris (ust/alt), kiris govde levhasi. Kullanici satiri kapatabilir.
    struct CalloutPos {
        double x, y;
    };
    // 1..10 cagri noktalari (levha cevresi + kiris temaslari).
    const CalloutPos callouts[] = {
        {52.0, 166.0},  // 1 levha-kolon ust dikey dikis
        {52.0, 74.0},   // 2 levha-kolon alt dikey dikis
        {84.0, 170.0},  // 3 levha-kiris ust kose
        {84.0, 70.0},   // 4 levha-kiris alt kose
        {96.0, 148.0},  // 5 kiris alt flans - levha
        {96.0, 92.0},   // 6 kiris ust flans - levha
        {300.0, 120.0}, // 7 kiris uzak ucu (berkitme)
        {46.0, 58.0},   // 8 levha alt kenar - kolon
        {46.0, 182.0},  // 9 levha ust kenar - kolon
        {68.0, 152.0},  // 10 bulon bolgesi cevresi
    };
    for (int i = 0; i < 10; ++i) {
        std::wstring id = L"ws";
        id += static_cast<wchar_t>(L'0' + (i + 1) / 10);
        id += static_cast<wchar_t>(L'0' + (i + 1) % 10);
        DiagramHotspot h = hotspot(id, callouts[i].x, callouts[i].y, 7.0, std::nullopt,
                                   std::to_wstring(i + 1));
        h.weldRow = i;  // 0-tabanli satir indeksi
        def.schema.hotspots.push_back(h);
    }

    // 10 kaynak satiri: hepsi fillet a=5, etkin bogaz 3.5 mm (0.7*a), Auto/None.
    def.schema.weldRows.clear();
    for (int i = 0; i < 10; ++i) {
        WeldTableRow r;
        r.number = i + 1;
        r.size = 5.0;
        r.type = WeldRowType::Fillet;
        r.angle = 45.0;
        r.effThroat = 3.5;
        r.rootOpening = 0.0;
        r.placement = L"Auto";
        r.prep = L"None";
        r.length = 0.0;
        r.pitch = 0.0;
        def.schema.weldRows.push_back(r);
    }

    // Tekla 146 sekme SIRASI (gercek diyalog): Picture / Plates / Stiffeners /
    // General / Haunch / Notch / Bolts / Design type / Beam cut / Angle box /
    // BoxPBolts / BoxSBolts / Analysis. \"Welds\" sekme DEGIL, ust cubuktaki
    // \"Welds...\" dugmesidir. Parca satirlari (t b h | Pos_No | Material | Name |
    // Finish) partRows ile sekmenin ustunde cizilir.
    ComponentDialogGroup gPicture;
    gPicture.id = L"tab_picture";
    gPicture.title = L"Picture";
    gPicture.asTab = true;
    gPicture.paramNames = {L"flangeStripSize", L"beamGap", L"beamGapMeasure", L"beamEndCut",
                           L"beamWebCut", L"beamFlangeCut", L"beamBottomFlangeCut",
                           L"bottomNotchRadius", L"bottomNotchHeight"};

    ComponentDialogGroup gPlates;
    gPlates.id = L"tab_plates";
    gPlates.title = L"Plates";
    gPlates.asTab = true;
    gPlates.partRows = {
        {L"Tab plate", L"plateThickness", L"plateWidth", L"plateHeight", L"",
         L"tabPosPrefix", L"tabPosStart", L"tabAsmPrefix", L"tabAsmStart",
         L"tabMaterial", L"tabName", L"tabFinish"},
        {L"Plate washer", L"plateWasherT", L"plateWasherW", L"plateWasherH", L"",
         L"washerPosPrefix", L"washerPosStart", L"washerAsmPrefix", L"washerAsmStart",
         L"washerMaterial", L"washerName", L"washerFinish"}};
    gPlates.paramNames = {L"shearTabPosition", L"tabGapSecondary", L"tabPositionSloped",
                          L"tabEdgeDistance", L"tabEndCut",
                          L"tabChamferType", L"tabChamferH", L"tabChamferV",
                          L"tabOrientation", L"plateWasherType", L"plateWasherSide"};

    ComponentDialogGroup gStiff;
    gStiff.id = L"tab_stiffeners";
    gStiff.title = L"Stiffeners";
    gStiff.asTab = true;
    gStiff.partRows = {
        {L"Web stiffener", L"stiffenerT", L"stiffenerW", L"stiffenerH", L"",
         L"stiffenerPosPrefix", L"stiffenerPosStart", L"", L"",
         L"stiffenerMaterial", L"stiffenerName", L"stiffenerFinish"}};
    gStiff.paramNames = {L"stiffenerCreation", L"stiffenerGapFlange", L"stiffenerGapWeb",
                         L"stiffenerOrientation",
                         L"stiffenerChamferType", L"stiffenerChamferH", L"stiffenerChamferV"};

    ComponentDialogGroup gGeneral;
    gGeneral.id = L"tab_general";
    gGeneral.title = L"General";
    gGeneral.asTab = true;
    gGeneral.paramNames = {L"upDirection", L"upDirection2", L"upDirection3", L"locked",
                           L"classNo", L"connectionCode", L"autoDefaultsGroup",
                           L"autoConnectionGroup"};

    ComponentDialogGroup gHaunch;
    gHaunch.id = L"tab_haunch";
    gHaunch.title = L"Haunch";
    gHaunch.asTab = true;
    gHaunch.partRows = {
        {L"Top plate", L"haunchTopT", L"haunchTopW", L"haunchTopH", L"",
         L"haunchTopPosPrefix", L"haunchTopPosStart", L"", L"",
         L"haunchTopMaterial", L"haunchTopName", L"haunchTopFinish"},
        {L"Bottom plate", L"haunchBottomT", L"haunchBottomW", L"haunchBottomH", L"",
         L"haunchBottomPosPrefix", L"haunchBottomPosStart", L"", L"",
         L"haunchBottomMaterial", L"haunchBottomName", L"haunchBottomFinish"}};
    gHaunch.paramNames = {L"haunchCreation",
                          L"haunchTopChamferW", L"haunchTopChamferH",
                          L"haunchBottomChamferH", L"haunchBottomChamferW"};

    ComponentDialogGroup gNotch;
    gNotch.id = L"tab_notch";
    gNotch.title = L"Notch";
    gNotch.asTab = true;
    gNotch.paramNames = {L"notchShape", L"notchSizeRef", L"notchFlangeCutShape",
                         L"notchRounding", L"notchRoundH", L"notchRoundV",
                         L"notchPosition", L"notchChamfer", L"notchChamferRadius",
                         // Manuel (Manual notch control): ust ve alt flans ayri
                         L"flangeNotchSideTop", L"flangeNotchShapeTop",
                         L"flangeNotchDepthRefTop", L"flangeNotchDepthTop",
                         L"notchCutHTop", L"notchCutVTop", L"notchWebToFlangeTop",
                         L"flangeNotchSideBottom", L"flangeNotchShapeBottom",
                         L"flangeNotchDepthRefBottom", L"flangeNotchDepthBottom",
                         L"notchCutHBottom", L"notchCutVBottom", L"notchWebToFlangeBottom",
                         L"notchBcsa", L"notchBcsaValue", L"notchWebClearance"};

    ComponentDialogGroup gBolts;
    gBolts.id = L"tab_bolts";
    gBolts.title = L"Bolts";
    gBolts.asTab = true;
    gBolts.paramNames = {L"boltHorizRef", L"boltEdge", L"boltCount", L"boltSpacing",
                         L"boltVertRef", L"boltVerticalMeasure", L"boltVerticalPos",
                         L"deletedBolts", L"boltStagger", L"boltStaggerExtra",
                         L"boltGroupOrientation", L"boltingDirection",
                         L"boltDiameter", L"holeDiameter", L"grade",
                         L"boltStandard", L"tolerance", L"threadInMat", L"boltSiteWorkshop",
                         L"cutLength", L"comment",
                         L"slotV", L"slotH", L"holeType", L"rotateSlots", L"slotsIn",
                         L"asm1", L"asm2", L"asm3", L"asm4", L"asm5", L"asm6",
                         L"asmEffectInModify", L"boltLengthIncrease",
                         L"boltCountX", L"pitchX", L"pitchY",
                         L"edgeX", L"edgeY"};

    ComponentDialogGroup gDesign;
    gDesign.id = L"tab_design_type";
    gDesign.title = L"Design type";
    gDesign.asTab = true;
    gDesign.paramNames = {L"externalDesign", L"checkConnection", L"designCode",
                          L"loadDefinition", L"udlPercent",
                          L"loadShear", L"loadTension", L"loadMoment"};

    ComponentDialogGroup gBeamCut;
    gBeamCut.id = L"tab_beam_cut";
    gBeamCut.title = L"Beam cut";
    gBeamCut.asTab = true;
    gBeamCut.partRows = {
        {L"Backing bar", L"backingBarT", L"backingBarW", L"", L"",
         L"backingBarPosPrefix", L"backingBarPosStart", L"backingBarAsmPrefix", L"backingBarAsmStart",
         L"backingBarMaterial", L"backingBarName", L"backingBarFinish"}};
    gBeamCut.paramNames = {L"wahGapTop", L"wahVertical", L"wahHorizontal",
                           L"wahGapWeb", L"wahGapBottom",
                           L"weldAccessHoleType", L"wahRadiusR", L"wahRadiusSmall",
                           L"beamPrepTopX", L"beamPrepBottomX",
                           L"beamEndPreparation", L"flangeCutTop", L"flangeCutBottom",
                           L"backingBars", L"backingBarLengthType", L"backingBarLength",
                           L"backingBarPosition", L"backingBarAssembly"};

    ComponentDialogGroup gAngleBox;
    gAngleBox.id = L"tab_angle_box";
    gAngleBox.title = L"Angle box";
    gAngleBox.asTab = true;
    gAngleBox.partRows = {
        {L"Stiffeners", L"seatStiffT", L"seatStiffW", L"seatStiffH", L"",
         L"seatStiffPosPrefix", L"seatStiffPosStart", L"", L"",
         L"seatStiffMaterial", L"seatStiffName", L"seatStiffFinish"},
        {L"Top angle", L"", L"", L"", L"seatTopProfile",
         L"seatTopPosPrefix", L"seatTopPosStart", L"", L"",
         L"seatTopMaterial", L"seatTopName", L"seatTopFinish"},
        {L"Bottom angle", L"", L"", L"", L"seatBottomProfile",
         L"seatBottomPosPrefix", L"seatBottomPosStart", L"", L"",
         L"seatBottomMaterial", L"seatBottomName", L"seatBottomFinish"}};
    gAngleBox.paramNames = {L"seatPosition", L"seatAttachTop", L"seatAttachBottom",
                            L"seatStiffType", L"seatStiffOffsetH", L"seatStiffOffsetV",
                            L"seatRotation", L"seatOrientation",
                            L"seatSideStiff", L"seatMiddleStiff", L"seatMiddleStiffCount",
                            L"seatOffset", L"seatGap",
                            L"seatChamferType", L"seatChamferV", L"seatChamferH"};

    const auto boxBoltsTab = [](const wchar_t* id, const wchar_t* title, const std::wstring& p) {
        ComponentDialogGroup g;
        g.id = id;
        g.title = title;
        g.asTab = true;
        g.layout = L"boxbolts";
        g.prefix = p;
        for (const wchar_t* sfx : {L"Size", L"Standard", L"Tolerance", L"ThreadInMat",
                                   L"SiteTop", L"SiteBottom", L"Comment"})
            g.paramNames.push_back(p + sfx);
        for (const wchar_t* side : {L"Top", L"Bottom"})
            for (const wchar_t* sfx : {L"SlotV", L"SlotH", L"HoleType", L"RotateSlots",
                                       L"SlotsIn", L"LengthIncrease"})
                g.paramNames.push_back(p + side + sfx);
        for (const wchar_t* sfx : {L"Pos1", L"Edge", L"Count", L"Spacing", L"Pos5"})
            g.paramNames.push_back(p + sfx);
        return g;
    };
    ComponentDialogGroup gBoxP = boxBoltsTab(L"tab_boxpbolts", L"BoxPBolts", L"boxP");
    ComponentDialogGroup gBoxS = boxBoltsTab(L"tab_boxsbolts", L"BoxSBolts", L"boxS");

    ComponentDialogGroup gAnalysis;
    gAnalysis.id = L"tab_analysis";
    gAnalysis.title = L"Analysis";
    gAnalysis.asTab = true;
    gAnalysis.paramNames = {L"useAnalysisRestraints", L"memberSelection", L"restrainCombination",
                            L"supportCondition",
                            L"supportUx|supportUxValue", L"supportUy|supportUyValue",
                            L"supportUz|supportUzValue", L"supportRx|supportRxValue",
                            L"supportRy|supportRyValue", L"supportRz|supportRzValue",
                            L"longitudinalMemberOffset", L"analysisProfile",
                            L"analysisProfileLength"};

    // Tekla yardim sayfasindaki sekme basina olcu/ayrinti sekilleri
    // (resources/tekla146.qrc). Yalniz bilgi amaclidir, parametre degildir.
    const auto fig = [](const wchar_t* file, const wchar_t* caption) {
        return DialogFigure{std::wstring(L":/tekla146/") + file, caption};
    };
    gPicture.figures = {
        fig(L"picture_dimensions.png", L"Dimensions: 1 flange strip, 2 beam gap"),
        fig(L"use_beam.png", L"Simple shear tab connected to a beam"),
        fig(L"part_key.png", L"Part identification key: 1 shear tab, 2 haunch plate")};
    gPlates.figures = {
        fig(L"tab_gap.png", L"Gap between shear tab and secondary part"),
        fig(L"tab_edge_distance.png", L"Shear tab edge distance"),
        fig(L"tab_chamfers.png", L"Shear tab chamfers: 1 horizontal, 2 vertical")};
    gStiff.figures = {
        fig(L"stiffener_gap.png", L"Stiffener gap: 1 to flanges, 2 to web"),
        fig(L"tab_chamfers.png", L"Stiffener chamfers: 1 horizontal, 2 vertical")};
    gHaunch.figures = {
        fig(L"haunch_chamfers.png", L"Haunch plate chamfers 1-4")};
    gNotch.figures = {
        fig(L"notch_size.png", L"Notch size"),
        fig(L"notch_rounding.png", L"Notch dimension rounding"),
        fig(L"notch_chamfer.png", L"Notch chamfer radius"),
        fig(L"notch_cut_dimensions.png", L"Cut dimensions: 1 horizontal, 2 vertical"),
        fig(L"notch_web_to_flange.png", L"Dimension from web to flange cut")};
    gBolts.figures = {
        fig(L"bolt_group_dimensions.png", L"Bolt group dimensions 1-7"),
        fig(L"slotted_holes.png", L"Slotted holes: 1 vertical, 2 horizontal"),
        fig(L"bolt_length_increase.png", L"Bolt length increase")};
    gBeamCut.figures = {
        fig(L"weld_access_hole_dimensions.png", L"Weld access hole dimensions 1-5")};
    gAngleBox.figures = {
        fig(L"seat_stiffener_offset.png", L"Stiffener offset: 1 horizontal, 2 vertical"),
        fig(L"seat_gap.png", L"Gap: 1 top/bottom seat angle - secondary part"),
        fig(L"seat_chamfers.png", L"Seat angle chamfers: 1 vertical, 2 horizontal")};
    gBoxP.figures = {fig(L"boxp_bolt_group.png", L"Seat angle bolt group dimensions 1-5")};
    gBoxS.figures = {fig(L"boxs_bolt_group.png", L"Seat angle bolt group dimensions 1-5")};

    def.schema.groups = {gPicture, gPlates,  gStiff,  gGeneral,  gHaunch,  gNotch, gBolts,
                         gDesign,  gBeamCut, gAngleBox, gBoxP,   gBoxS,    gAnalysis};
    def.schema.useTabs = true;
    return def;
}

} // namespace mm
