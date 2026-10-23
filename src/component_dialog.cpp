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
        for (const auto& pn : g.paramNames) {
            if (specNames.find(pn) == specNames.end()) {
                result.ok = false;
                result.problems.push_back(L"grup " + g.id + L" bilinmeyen parametreyi isaret ediyor: " +
                                          pn);
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
    docEnum(L"flangeNotchSide", L"Side of flange notch",
            {L"Default", L"Automatic", L"Both sides", L"Near side", L"Far side"});
    docEnum(L"flangeNotchShape", L"Flange notch shape",
            {L"Default", L"Automatic", L"Chamfer", L"Default values", L"No cut",
             L"Flush with web", L"Fields 1 and 2"});
    docEnum(L"flangeNotchDepthRef", L"Flange notch depth",
            {L"Default", L"Depth", L"From web center line"});
    docLen(L"flangeNotchDepth", L"Flange notch depth value", 0.0);
    docLen(L"notchCutH", L"Cut dimension (horizontal)", 10.0);
    docLen(L"notchCutV", L"Cut dimension (vertical)", 0.0);
    docLen(L"notchWebToFlange", L"Dimension from web to flange cut", 0.0);
    docEnum(L"notchBcsa", L"BCSA notch definition", {L"Default", L"Yes", L"No"});
    docLen(L"notchWebClearance", L"Cut clearance from beam web", 0.0);

    // Bolts sekmesi
    docEnum(L"boltStagger", L"Staggering of bolts",
            {L"Default", L"Not staggered", L"Staggered type 1", L"Staggered type 2"});
    docEnum(L"boltHorizRef", L"Horizontal dimension measured to",
            {L"Default", L"Secondary part", L"Main part web"});
    docEnum(L"boltVertRef", L"Vertical dimension measured to",
            {L"Default", L"Secondary part (sloped)", L"Secondary part",
             L"Main part", L"Main part center line"});

    // --- PDF (conn_146_help, 23.10.2026) ile tamamlanan alanlar -------------
    const auto docBool = [&](const wchar_t* n, const wchar_t* label, bool d) {
        CompParamSpec s = boolParam(n, d, label);
        s.documentedOnly = true;
        def.specs.push_back(s);
    };
    const auto docInt = [&](const wchar_t* n, const wchar_t* label, int d) {
        CompParamSpec s = intParam(n, d, 0, 100, label);
        s.documentedOnly = true;
        def.specs.push_back(s);
    };
    // Parca satiri (Pos_No onek + baslangic no, Material, Name, Finish).
    const auto docPart = [&](const std::wstring& p, const std::wstring& title,
                             bool withMaterial) {
        docStr((p + L"PosPrefix").c_str(), (title + L" Pos_No prefix").c_str());
        docInt((p + L"PosStart").c_str(), (title + L" Pos_No start number").c_str(), 1);
        if (withMaterial) {
            docStr((p + L"Material").c_str(), (title + L" material").c_str());
            docStr((p + L"Name").c_str(), (title + L" name").c_str());
            docStr((p + L"Finish").c_str(), (title + L" finish").c_str());
        }
    };
    const std::vector<std::wstring> siteWorkshop = {L"Default", L"Site", L"Workshop"};

    // Picture: beam gap olcum yonu
    docEnum(L"beamGapMeasure", L"Beam gap measured",
            {L"Default", L"Square to main part", L"Along secondary part"});
    // Plates: Pos_No + plate washer parca satiri
    docPart(L"tab", L"Tab plate", false);
    docPart(L"washer", L"Plate washer", true);
    // Stiffeners / Haunch: parca satirlari
    docPart(L"stiffener", L"Stiffener", true);
    docPart(L"haunch", L"Haunch plate", true);
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
    docBool(L"asmBolt", L"Bolt assembly: bolt", true);
    docBool(L"asmWashers", L"Bolt assembly: washers", true);
    docBool(L"asmNuts", L"Bolt assembly: nuts", true);
    docBool(L"asmEffectInModify", L"Effect in modify", false);
    docLen(L"boltLengthIncrease", L"Bolt length increase", 0.0);

    // Beam cut sekmesi (weld backing bar, weld access hole, beam end prep)
    docLen(L"backingBarW", L"Weld backing bar width", 0.0);
    docPart(L"backingBar", L"Weld backing bar", true);
    docLen(L"wahGapTop", L"1 Gap: secondary top flange - main part", 0.0);
    docLen(L"wahVertical", L"2 Weld access hole vertical", 0.0);
    docLen(L"wahHorizontal", L"3 Weld access hole horizontal", 0.0);
    docLen(L"wahGapWeb", L"4 Gap: secondary web - main part (+Picture gap)", 0.0);
    docLen(L"wahGapBottom", L"5 Gap: secondary bottom flange - main part", 0.0);
    docEnum(L"weldAccessHoleType", L"Weld access holes",
            {L"Default", L"Round", L"Square", L"Diagonal", L"Round (radius)",
             L"Extended cone", L"Cone"});
    docLen(L"wahRadiusR", L"Weld access hole R (large radius)", 35.0);
    docLen(L"wahRadiusSmall", L"Weld access hole r (small radius)", 10.0);
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

    // Angle box sekmesi (seat angle)
    docLen(L"seatStiffT", L"Seat stiffener thickness", 0.0);
    docLen(L"seatStiffW", L"Seat stiffener width", 0.0);
    docLen(L"seatStiffH", L"Seat stiffener height", 0.0);
    docStr(L"seatProfile", L"Seat angle profile");
    docPart(L"seat", L"Seat angle", true);
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

    // BoxPBolts / BoxSBolts (seat angle -> main / secondary)
    const auto docBoxBolts = [&](const std::wstring& p, bool toMain) {
        docLen((p + L"Pos1").c_str(),
               toMain ? L"1 Horizontal position (from secondary middle line)"
                      : L"1 Vertical position (from secondary middle line)", 0.0);
        docLen((p + L"Edge").c_str(), L"2 Bolt edge distance", 0.0);
        docInt((p + L"Count").c_str(), L"3 Number of bolts", 0);
        docStr((p + L"Spacing").c_str(), L"4 Bolt spacing");
        docLen((p + L"Pos5").c_str(),
               toMain ? L"5 Vertical position (from secondary bottom)"
                      : L"5 Horizontal position (from secondary bottom)", 0.0);
        docLen((p + L"Size").c_str(), L"Bolt size", 0.0);
        docStr((p + L"Standard").c_str(), L"Bolt standard");
        docLen((p + L"Tolerance").c_str(), L"Tolerance", 0.0);
        docBool((p + L"ThreadInMat").c_str(), L"Thread in mat", true);
        docEnum((p + L"SiteWorkshop").c_str(), L"Site/Workshop", siteWorkshop);
        docStr((p + L"Comment").c_str(), L"Comment");
        docLen((p + L"SlotV").c_str(), L"Slotted hole 1 (vertical)", 0.0);
        docLen((p + L"SlotH").c_str(), L"Slotted hole 2 (horizontal)", 0.0);
        docEnum((p + L"HoleType").c_str(), L"Hole type",
                {L"Default", L"Slotted", L"Oversized", L"No hole", L"Tapped"});
        docEnum((p + L"RotateSlots").c_str(), L"Rotate slots", {L"Default", L"No", L"Yes"});
        docStr((p + L"SlotsIn").c_str(), L"Slots in");
        docLen((p + L"LengthIncrease").c_str(), L"Bolt length increase", 0.0);
    };
    docBoxBolts(L"boxP", true);
    docBoxBolts(L"boxS", false);

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

    // --- SEKMELER (Tekla 146 duzeni): Picture / Plates / Stiffeners / Haunch /
    // Notch / Bolts (+ Welds tablosu). SEMATIK sol panelde KALICI durur; Tekla'daki
    // "Picture" sekmesi burada yalniz KIRIS KESIMI alanlarini tasir (sematik
    // ayri bir editor degil, parametreler tek sekmede tutulur).
    // Gri (documentedOnly) alanlar Tekla'da olup henuz geometrisi olmayanlardir.
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
    gPlates.paramNames = {L"plateThickness", L"plateWidth", L"plateHeight",
                          L"tabPosPrefix", L"tabPosStart",
                          L"tabMaterial", L"tabName", L"tabFinish",
                          L"washerPosPrefix", L"washerPosStart",
                          L"washerMaterial", L"washerName", L"washerFinish",
                          L"shearTabPosition", L"tabGapSecondary", L"tabPositionSloped",
                          L"tabEdgeDistance", L"tabEndCut",
                          L"tabChamferType", L"tabChamferH", L"tabChamferV",
                          L"tabOrientation",
                          L"plateWasherType", L"plateWasherSide",
                          L"plateWasherT", L"plateWasherW", L"plateWasherH"};

    ComponentDialogGroup gStiff;
    gStiff.id = L"tab_stiffeners";
    gStiff.title = L"Stiffeners";
    gStiff.asTab = true;
    gStiff.paramNames = {L"stiffenerT", L"stiffenerW", L"stiffenerH",
                         L"stiffenerPosPrefix", L"stiffenerPosStart", L"stiffenerMaterial",
                         L"stiffenerName", L"stiffenerFinish",
                         L"stiffenerCreation", L"stiffenerGapFlange", L"stiffenerGapWeb",
                         L"stiffenerOrientation",
                         L"stiffenerChamferType", L"stiffenerChamferH", L"stiffenerChamferV"};

    ComponentDialogGroup gHaunch;
    gHaunch.id = L"tab_haunch";
    gHaunch.title = L"Haunch";
    gHaunch.asTab = true;
    gHaunch.paramNames = {L"haunchCreation",
                          L"haunchTopT", L"haunchTopW", L"haunchTopH",
                          L"haunchBottomT", L"haunchBottomW", L"haunchBottomH",
                          L"haunchPosPrefix", L"haunchPosStart", L"haunchMaterial",
                          L"haunchName", L"haunchFinish",
                          L"haunchTopChamferW", L"haunchTopChamferH",
                          L"haunchBottomChamferH", L"haunchBottomChamferW"};

    ComponentDialogGroup gNotch;
    gNotch.id = L"tab_notch";
    gNotch.title = L"Notch";
    gNotch.asTab = true;
    gNotch.paramNames = {L"notchShape", L"notchSizeRef", L"notchFlangeCutShape",
                         L"notchRounding", L"notchRoundH", L"notchRoundV",
                         L"notchPosition", L"notchChamfer", L"notchChamferRadius",
                         L"flangeNotchSide", L"flangeNotchShape",
                         L"flangeNotchDepthRef", L"flangeNotchDepth",
                         L"notchCutH", L"notchCutV", L"notchWebToFlange",
                         L"notchBcsa", L"notchWebClearance"};

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
                         L"asmBolt", L"asmWashers", L"asmNuts", L"asmEffectInModify",
                         L"boltLengthIncrease",
                         L"boltCountX", L"pitchX", L"pitchY",
                         L"edgeX", L"edgeY"};

    ComponentDialogGroup gBeamCut;
    gBeamCut.id = L"tab_beam_cut";
    gBeamCut.title = L"Beam cut";
    gBeamCut.asTab = true;
    gBeamCut.paramNames = {L"backingBarT", L"backingBarW",
                           L"backingBarPosPrefix", L"backingBarPosStart",
                           L"backingBarMaterial", L"backingBarName", L"backingBarFinish",
                           L"wahGapTop", L"wahVertical", L"wahHorizontal",
                           L"wahGapWeb", L"wahGapBottom",
                           L"weldAccessHoleType", L"wahRadiusR", L"wahRadiusSmall",
                           L"beamEndPreparation", L"flangeCutTop", L"flangeCutBottom",
                           L"backingBars", L"backingBarLengthType", L"backingBarLength",
                           L"backingBarPosition", L"backingBarAssembly"};

    ComponentDialogGroup gAngleBox;
    gAngleBox.id = L"tab_angle_box";
    gAngleBox.title = L"Angle box";
    gAngleBox.asTab = true;
    gAngleBox.paramNames = {L"seatStiffT", L"seatStiffW", L"seatStiffH", L"seatProfile",
                            L"seatPosPrefix", L"seatPosStart", L"seatMaterial",
                            L"seatName", L"seatFinish",
                            L"seatPosition", L"seatAttachTop", L"seatAttachBottom",
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
        for (const wchar_t* suffix :
             {L"Pos1", L"Edge", L"Count", L"Spacing", L"Pos5", L"Size", L"Standard",
              L"Tolerance", L"ThreadInMat", L"SiteWorkshop", L"Comment", L"SlotV", L"SlotH",
              L"HoleType", L"RotateSlots", L"SlotsIn", L"LengthIncrease"})
            g.paramNames.push_back(p + suffix);
        return g;
    };
    ComponentDialogGroup gBoxP = boxBoltsTab(L"tab_boxpbolts", L"BoxPBolts", L"boxP");
    ComponentDialogGroup gBoxS = boxBoltsTab(L"tab_boxsbolts", L"BoxSBolts", L"boxS");

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

    def.schema.groups = {gPicture, gPlates,   gStiff,    gHaunch, gNotch,
                         gBolts,   gBeamCut,  gAngleBox, gBoxP,   gBoxS};
    def.schema.useTabs = true;
    return def;
}

} // namespace mm
