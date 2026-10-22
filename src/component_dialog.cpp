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

    // --- SEKMELER (Tekla 146 duzeni): Plates / Stiffeners / General / Notch /
    // Bolts / Beam cut. SEMATIK sol panelde KALICI durur (Tekla'daki Picture
    // sekmesinin islevi); bu yuzden ayrica "Picture" sekmesi acilmaz — ayni
    // parametreler iki sekmede iki ayri editor olur ve degerler paylasilmazdi.
    ComponentDialogGroup gPlates;
    gPlates.id = L"tab_plates";
    gPlates.title = L"Plates";
    gPlates.asTab = true;
    gPlates.paramNames = {L"plateThickness", L"plateWidth", L"plateHeight",
                          L"boltEdge", L"boltCount"};

    ComponentDialogGroup gStiff;
    gStiff.id = L"tab_stiffeners";
    gStiff.title = L"Stiffeners";
    gStiff.asTab = true;
    gStiff.paramNames = {L"stiffenerCreation", L"stiffenerGapWeb"};

    ComponentDialogGroup gGeneral;
    gGeneral.id = L"tab_general";
    gGeneral.title = L"General";
    gGeneral.asTab = true;
    gGeneral.paramNames = {L"shearTabPosition", L"tabGapSecondary", L"backingBarT"};

    ComponentDialogGroup gNotch;
    gNotch.id = L"tab_notch";
    gNotch.title = L"Notch";
    gNotch.asTab = true;
    gNotch.paramNames = {L"beamEndCut"};

    ComponentDialogGroup gBolts;
    gBolts.id = L"tab_bolts";
    gBolts.title = L"Bolts";
    gBolts.asTab = true;
    gBolts.paramNames = {L"boltDiameter", L"holeDiameter", L"grade",
                         L"boltStandard", L"tolerance", L"threadInMat",
                         L"holeType", L"rotateSlots", L"slotsIn", L"cutLength",
                         L"boltCount", L"boltCountX", L"pitchX", L"pitchY",
                         L"edgeX", L"edgeY", L"boltEdge", L"boltSpacing",
                         L"boltVerticalMeasure", L"boltVerticalPos",
                         L"deletedBolts", L"comment"};

    ComponentDialogGroup gBeamCut;
    gBeamCut.id = L"tab_beam_cut";
    gBeamCut.title = L"Beam cut";
    gBeamCut.asTab = true;
    gBeamCut.paramNames = {L"beamGap", L"beamEndCut"};

    def.schema.groups = {gPlates, gStiff, gGeneral, gNotch, gBolts, gBeamCut};
    def.schema.useTabs = true;
    return def;
}

} // namespace mm
