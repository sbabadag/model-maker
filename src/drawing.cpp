#include "model_maker/drawing.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <exception>

namespace mm::drawing {
namespace {

using json = nlohmann::json;
constexpr int kSchemaVersion = 3;   // v3: P2C coklu pafta seti + kullanici tercihleri; v1/v2 okunabilir

// --- enum <-> string (kalici ad; sayisal deger degil => yeniden siralamaya dayanikli) ---
const char* name(DrawingKind k) { return k == DrawingKind::Assembly ? "assembly" : "singlePart"; }
const char* name(ViewKind k) {
    switch (k) {
    case ViewKind::Front: return "front";
    case ViewKind::Top: return "top";
    case ViewKind::Side: return "side";
    case ViewKind::Detail: return "detail";
    }
    return "front";
}
const char* name(LineStyle s) { return s == LineStyle::Hidden ? "hidden" : "visible"; }
const char* name(ProjectionMethod m) {
    return m == ProjectionMethod::FirstAngle ? "firstAngle" : "thirdAngle";
}
const char* name(DimensionDirection d) {
    switch (d) {
    case DimensionDirection::Horizontal: return "horizontal";
    case DimensionDirection::Vertical: return "vertical";
    case DimensionDirection::Aligned: return "aligned";
    }
    return "horizontal";
}
const char* name(DimensionRole r) {
    switch (r) {
    case DimensionRole::MemberLength: return "memberLength";
    case DimensionRole::OverallWidth: return "overallWidth";
    case DimensionRole::OverallHeight: return "overallHeight";
    case DimensionRole::HoleSpacing: return "holeSpacing";
    case DimensionRole::PartPosition: return "partPosition";
    case DimensionRole::PlateWidth: return "plateWidth";
    case DimensionRole::PlateHeight: return "plateHeight";
    case DimensionRole::HolePitch: return "holePitch";
    case DimensionRole::HoleEdge: return "holeEdge";
    case DimensionRole::PlateOffsetX: return "plateOffsetX";
    case DimensionRole::PlateOffsetY: return "plateOffsetY";
    case DimensionRole::PlateOffsetZ: return "plateOffsetZ";
    }
    return "overallWidth";
}

bool parse(const std::string& s, DrawingKind& o) {
    if (s == "assembly") { o = DrawingKind::Assembly; return true; }
    if (s == "singlePart") { o = DrawingKind::SinglePart; return true; }
    return false;
}
bool parse(const std::string& s, ViewKind& o) {
    if (s == "front") { o = ViewKind::Front; return true; }
    if (s == "top") { o = ViewKind::Top; return true; }
    if (s == "side") { o = ViewKind::Side; return true; }
    if (s == "detail") { o = ViewKind::Detail; return true; }
    return false;
}
bool parse(const std::string& s, LineStyle& o) {
    if (s == "visible") { o = LineStyle::Visible; return true; }
    if (s == "hidden") { o = LineStyle::Hidden; return true; }
    return false;
}
const char* name(SheetRole r) {
    switch (r) {
    case SheetRole::Primary: return "primary";
    case SheetRole::Continuation: return "continuation";
    case SheetRole::DetailSheet: return "detail";
    }
    return "primary";
}
bool parseRole(const std::string& s, SheetRole& o) {
    for (SheetRole r : {SheetRole::Primary, SheetRole::Continuation, SheetRole::DetailSheet})
        if (s == name(r)) { o = r; return true; }
    return false;
}
const char* name(DrawingMode m) { return m == DrawingMode::Single ? "single" : "single"; }
bool parseMode(const std::string& s, DrawingMode& o) {
    if (s == "single") { o = DrawingMode::Single; return true; }
    return false;
}
bool parse(const std::string& s, ProjectionMethod& o) {
    if (s == "thirdAngle") { o = ProjectionMethod::ThirdAngle; return true; }
    if (s == "firstAngle") { o = ProjectionMethod::FirstAngle; return true; }
    return false;
}
bool parse(const std::string& s, DimensionDirection& o) {
    if (s == "horizontal") { o = DimensionDirection::Horizontal; return true; }
    if (s == "vertical") { o = DimensionDirection::Vertical; return true; }
    if (s == "aligned") { o = DimensionDirection::Aligned; return true; }
    return false;
}
bool parse(const std::string& s, DimensionRole& o) {
    if (s == "memberLength") { o = DimensionRole::MemberLength; return true; }
    if (s == "overallWidth") { o = DimensionRole::OverallWidth; return true; }
    if (s == "overallHeight") { o = DimensionRole::OverallHeight; return true; }
    if (s == "holeSpacing") { o = DimensionRole::HoleSpacing; return true; }
    if (s == "partPosition") { o = DimensionRole::PartPosition; return true; }
    if (s == "plateWidth") { o = DimensionRole::PlateWidth; return true; }
    if (s == "plateHeight") { o = DimensionRole::PlateHeight; return true; }
    if (s == "holePitch") { o = DimensionRole::HolePitch; return true; }
    if (s == "holeEdge") { o = DimensionRole::HoleEdge; return true; }
    if (s == "plateOffsetX") { o = DimensionRole::PlateOffsetX; return true; }
    if (s == "plateOffsetY") { o = DimensionRole::PlateOffsetY; return true; }
    if (s == "plateOffsetZ") { o = DimensionRole::PlateOffsetZ; return true; }
    return false;
}

const char* name(TextKind k) {
    switch (k) {
    case TextKind::HoleCallout: return "holeCallout";
    case TextKind::PlateNote: return "plateNote";
    case TextKind::GeneralNote: return "generalNote";
    case TextKind::Legend: return "legend";
    case TextKind::StatusStamp: return "statusStamp";
    case TextKind::ViewTitle: return "viewTitle";
    }
    return "generalNote";
}
bool parse(const std::string& s, TextKind& o) {
    for (TextKind k : {TextKind::HoleCallout, TextKind::PlateNote, TextKind::GeneralNote, TextKind::Legend,
                       TextKind::StatusStamp, TextKind::ViewTitle})
        if (s == name(k)) { o = k; return true; }
    return false;
}
const char* name(ChecklistState k) {
    switch (k) {
    case ChecklistState::Satisfied: return "satisfied";
    case ChecklistState::Unresolved: return "unresolved";
    case ChecklistState::NotApplicable: return "notApplicable";
    }
    return "unresolved";
}
bool parse(const std::string& s, ChecklistState& o) {
    for (ChecklistState k : {ChecklistState::Satisfied, ChecklistState::Unresolved, ChecklistState::NotApplicable})
        if (s == name(k)) { o = k; return true; }
    return false;
}

json v3(const Vec3& v) { return json::array({v.x, v.y, v.z}); }
Vec3 v3From(const json& j) { return Vec3{j.at(0).get<double>(), j.at(1).get<double>(), j.at(2).get<double>()}; }
bool parseSize(const std::string& s, SheetSize& o) {
    for (SheetSize z : {SheetSize::A4, SheetSize::A3, SheetSize::A2, SheetSize::A1, SheetSize::A0})
        if (s == sheetSizeName(z)) { o = z; return true; }
    return false;
}

json v2(const Vec2& v) { return json::array({v.x, v.y}); }
Vec2 v2From(const json& j) { return Vec2{j.at(0).get<double>(), j.at(1).get<double>()}; }

json boundsJson(const Bounds2& b) {
    return b.empty ? json{{"empty", true}}
                   : json{{"empty", false}, {"min", v2(b.min)}, {"max", v2(b.max)}};
}
Bounds2 boundsFrom(const json& j) {
    Bounds2 b;
    if (j.at("empty").get<bool>()) return b;
    b.empty = false;
    b.min = v2From(j.at("min"));
    b.max = v2From(j.at("max"));
    return b;
}

json titleJson(const TitleBlock& t) {
    json revs = json::array();
    for (const Revision& r : t.revisions)
        revs.push_back({{"id", r.id}, {"date", r.date}, {"description", r.description}});
    return {{"project", t.project}, {"title", t.title}, {"drawingNumber", t.drawingNumber},
            {"drawnBy", t.drawnBy}, {"date", t.date}, {"scaleText", t.scaleText},
            {"revision", t.revision}, {"revisions", revs}};
}
TitleBlock titleFrom(const json& j) {
    TitleBlock t;
    t.project = j.at("project").get<std::string>();
    t.title = j.at("title").get<std::string>();
    t.drawingNumber = j.at("drawingNumber").get<std::string>();
    t.drawnBy = j.at("drawnBy").get<std::string>();
    t.date = j.at("date").get<std::string>();
    t.scaleText = j.at("scaleText").get<std::string>();
    t.revision = j.at("revision").get<std::string>();
    for (const json& r : j.at("revisions"))
        t.revisions.push_back({r.at("id").get<std::string>(), r.at("date").get<std::string>(),
                               r.at("description").get<std::string>()});
    return t;
}

json viewJson(const DrawingView& v) {
    json segs = json::array();
    for (const Segment2& s : v.segments)
        segs.push_back({v2(s.a), v2(s.b), name(s.style)});
    json circles = json::array();
    for (const Circle2& c : v.circles)
        circles.push_back({{"c", v2(c.center)}, {"r", c.radius}, {"style", name(c.style)},
                           {"owner", c.owner}, {"label", c.label}});
    json mem = json::array();
    for (const ViewMember& m : v.members)
        mem.push_back({{"entity", m.entity}, {"bounds", boundsJson(m.bounds)}});
    return {{"id", v.id}, {"kind", name(v.kind)}, {"subject", v.subject}, {"scale", v.scale},
            {"origin", v2(v.origin)}, {"projectionOffset", v2(v.projectionOffset)},
            {"segments", segs}, {"bounds", boundsJson(v.bounds)},
            {"members", mem}, {"label", v.label}, {"circles", circles}, {"detailPlate", v.detailPlate},
            {"frame", {{"right", v3(v.frame.right)}, {"up", v3(v.frame.up)}, {"normal", v3(v.frame.normal)},
                       {"memberAligned", v.frame.memberAligned}}}};
}
bool viewFrom(const json& j, DrawingView& v) {
    v.id = j.at("id").get<ElementId>();
    if (!parse(j.at("kind").get<std::string>(), v.kind)) return false;
    v.subject = j.at("subject").get<EntityId>();
    v.scale = j.at("scale").get<double>();
    v.origin = v2From(j.at("origin"));
    v.projectionOffset = v2From(j.at("projectionOffset"));
    for (const json& s : j.at("segments")) {
        Segment2 seg;
        seg.a = v2From(s.at(0));
        seg.b = v2From(s.at(1));
        if (!parse(s.at(2).get<std::string>(), seg.style)) return false;
        v.segments.push_back(seg);
    }
    v.bounds = boundsFrom(j.at("bounds"));
    if (j.contains("circles"))  // eski kayitlarda yok
        for (const json& c : j.at("circles")) {
            Circle2 ci;
            ci.center = v2From(c.at("c"));
            ci.radius = c.at("r").get<double>();
            if (!parse(c.at("style").get<std::string>(), ci.style)) return false;
            ci.owner = c.at("owner").get<EntityId>();
            ci.label = c.at("label").get<std::string>();
            v.circles.push_back(std::move(ci));
        }
    for (const json& m : j.at("members"))
        v.members.push_back({m.at("entity").get<EntityId>(), boundsFrom(m.at("bounds"))});
    v.label = j.at("label").get<std::string>();
    v.detailPlate = j.value("detailPlate", EntityId{0});
    if (j.contains("frame")) {  // v1 kayitlarinda yok: dunya cercevesi (varsayilan)
        const json& f = j.at("frame");
        v.frame.right = v3From(f.at("right"));
        v.frame.up = v3From(f.at("up"));
        v.frame.normal = v3From(f.at("normal"));
        v.frame.memberAligned = f.at("memberAligned").get<bool>();
    }
    return true;
}

} // namespace

void Bounds2::include(const Vec2& p) noexcept {
    if (empty) {
        min = max = p;
        empty = false;
        return;
    }
    min.x = std::min(min.x, p.x);
    min.y = std::min(min.y, p.y);
    max.x = std::max(max.x, p.x);
    max.y = std::max(max.y, p.y);
}

Bounds2 DrawingView::paperBounds() const noexcept {
    Bounds2 out;
    if (bounds.empty) return out;
    out.include(toPaper(bounds.min));
    out.include(toPaper(bounds.max));
    return out;
}

double Dimension2D::measured() const noexcept {
    const double dx = p2.x - p1.x, dy = p2.y - p1.y;
    switch (direction) {
    case DimensionDirection::Horizontal: return std::fabs(dx);
    case DimensionDirection::Vertical: return std::fabs(dy);
    case DimensionDirection::Aligned: return std::sqrt(dx * dx + dy * dy);
    }
    return 0.0;
}

SheetFormat sheetFormat(SheetSize size) {
    SheetFormat f;
    f.size = size;
    switch (size) {
    case SheetSize::A4: f.width = 297; f.height = 210; f.titleWidth = 150; f.titleHeight = 40; break;
    case SheetSize::A3: f.width = 420; f.height = 297; f.titleWidth = 190; f.titleHeight = 55; break;
    case SheetSize::A2: f.width = 594; f.height = 420; f.titleWidth = 190; f.titleHeight = 55; break;
    case SheetSize::A1: f.width = 841; f.height = 594; f.titleWidth = 190; f.titleHeight = 55; break;
    case SheetSize::A0: f.width = 1189; f.height = 841; f.titleWidth = 190; f.titleHeight = 55; break;
    }
    return f;
}

const char* sheetSizeName(SheetSize size) noexcept {
    switch (size) {
    case SheetSize::A4: return "A4";
    case SheetSize::A3: return "A3";
    case SheetSize::A2: return "A2";
    case SheetSize::A1: return "A1";
    case SheetSize::A0: return "A0";
    }
    return "A3";
}

const DrawingView* DrawingSheet::findView(ElementId vid) const noexcept {
    for (const DrawingView& v : views)
        if (v.id == vid) return &v;
    return nullptr;
}

const DrawingView* DrawingSheet::viewOfKind(ViewKind k) const noexcept {
    for (const DrawingView& v : views)
        if (v.kind == k) return &v;
    return nullptr;
}

Bounds2 DrawingSheet::titleBlockRect() const noexcept {
    Bounds2 b;
    b.include({format.width - format.margin - format.titleWidth, format.margin});
    b.include({format.width - format.margin, format.margin + format.titleHeight});
    return b;
}

Bounds2 DrawingSheet::notesArea() const noexcept {
    Bounds2 b;
    b.include({format.margin + 2.0, format.margin + 2.0});
    b.include({format.width - format.margin - format.titleWidth - 2.0, format.margin + format.titleHeight - 2.0});
    return b;
}

Bounds2 DrawingSheet::drawingArea() const noexcept {
    Bounds2 b;
    b.include({format.margin, format.margin + format.titleHeight});
    b.include({format.width - format.margin, format.height - format.margin});
    return b;
}

DrawingSheet& DrawingDocument::addSheet(DrawingSheet sheet) {
    sheet.id = allocateSheetId(); // kimlik DOKUMANA aittir (cagiranin degeri yok sayilir)
    sheets_.push_back(std::move(sheet));
    return sheets_.back();
}

const DrawingSheet* DrawingDocument::primarySheet() const noexcept {
    if (setInfo_.present && setInfo_.primarySheetId != 0)
        for (const DrawingSheet& s : sheets_)
            if (s.id == setInfo_.primarySheetId) return &s;
    return sheets_.empty() ? nullptr : &sheets_.front();
}

std::string DrawingDocument::serialize() const {
    json sheets = json::array();
    for (const DrawingSheet& s : sheets_) {
        json marks = json::array();
        for (const MarkAnnotation& m : s.marks)
            marks.push_back({{"id", m.id}, {"viewId", m.viewId}, {"target", m.target},
                             {"text", m.text}, {"isAssembly", m.isAssembly},
                             {"anchor", v2(m.anchor)}, {"labelPos", v2(m.labelPos)}});
        json dims = json::array();
        for (const Dimension2D& d : s.dimensions)
            dims.push_back({{"id", d.id}, {"viewId", d.viewId}, {"role", name(d.role)},
                            {"direction", name(d.direction)}, {"p1", v2(d.p1)}, {"p2", v2(d.p2)},
                            {"value", d.value}, {"offset", d.offset}, {"textLevel", d.textLevel},
                            {"side", d.side}, {"textT", d.textT}, {"textSide", d.textSide},
                            {"chain", d.chain}, {"source", d.source}, {"featureKey", d.featureKey},
                            {"basis", d.basis}, {"documentRevision", d.documentRevision}});
        json views = json::array();
        for (const DrawingView& v : s.views) views.push_back(viewJson(v));
        json texts = json::array();
        for (const TextAnnotation& t : s.texts)
            texts.push_back({{"id", t.id}, {"viewId", t.viewId}, {"kind", name(t.kind)}, {"lines", t.lines},
                             {"height", t.height}, {"hasAnchor", t.hasAnchor}, {"anchor", v2(t.anchor)},
                             {"pos", v2(t.pos)}, {"leader", t.leader}, {"bold", t.bold},
                             {"source", t.source}, {"featureKey", t.featureKey}});
        json checklist = json::array();
        for (const ChecklistItem& c : s.fabrication.checklist)
            checklist.push_back({{"key", c.key}, {"label", c.label}, {"state", name(c.state)},
                                 {"detail", c.detail}, {"featureKey", c.featureKey}});
        json fab = {{"evaluated", s.fabrication.evaluated}, {"draft", s.fabrication.draft},
                    {"checklist", checklist}, {"unresolvedCodes", s.fabrication.unresolvedCodes}};
        const LayoutConfig& lc = s.layoutConfig;
        json lcj = {{"dimTextHeight", lc.dimTextHeight}, {"noteTextHeight", lc.noteTextHeight},
                    {"minDimText", lc.minDimText}, {"minNoteText", lc.minNoteText}, {"clearance", lc.clearance},
                    {"rowPitch", lc.rowPitch}, {"firstOffset", lc.firstOffset},
                    {"minSheet", sheetSizeName(lc.minSheet)}, {"maxSheet", sheetSizeName(lc.maxSheet)},
                    {"fixedScaleDen", lc.fixedScaleDen}, {"maxIterations", lc.maxIterations}};
        json issues = json::array();
        for (const LayoutIssue& i : s.layout.issues)
            issues.push_back({{"code", i.code}, {"detail", i.detail}, {"element", i.element}});
        json lay = {{"ran", s.layout.ran}, {"ok", s.layout.ok},
                    {"requestedSheet", sheetSizeName(s.layout.requestedSheet)},
                    {"finalSheet", sheetSizeName(s.layout.finalSheet)},
                    {"scaleDenominator", s.layout.scaleDenominator}, {"attempts", s.layout.attempts},
                    {"escalations", s.layout.escalations}, {"issues", issues}};
        json refs = json::array();
        for (const SheetReference& r : s.references)
            refs.push_back({{"targetSheetId", r.targetSheetId}, {"targetViewId", r.targetViewId},
                            {"targetNumber", r.targetNumber}, {"label", r.label}, {"sourceEntity", r.sourceEntity}});
        sheets.push_back(
            {{"id", s.id}, {"name", s.name}, {"size", sheetSizeName(s.format.size)},
             {"margin", s.format.margin}, {"kind", name(s.kind)},
             {"projection", name(s.projection)}, {"subject", s.subject}, {"scale", s.scale},
             {"scaleIsStandard", s.scaleIsStandard}, {"titleBlock", titleJson(s.titleBlock)},
             {"views", views}, {"marks", marks}, {"dimensions", dims}, {"texts", texts},
             {"fabrication", fab}, {"layoutConfig", lcj}, {"layoutReport", lay},
             {"documentRevision", s.documentRevision}, {"nextId", s.nextId},
             {"role", name(s.role)}, {"parentSheetId", s.parentSheetId},
             {"sheetNumber", s.sheetNumber}, {"sheetIndex", s.sheetIndex}, {"references", refs}});
    }
    const DrawingPreferences& pr = preferences_;
    json prefs = {{"sheetSize", pr.autoSheetSize ? "auto" : sheetSizeName(pr.sheetSize)},
                  {"drawingMode", name(pr.mode)},
                  {"autoMultiSheetFallback", pr.autoMultiSheetFallback},
                  {"autoFirst", sheetSizeName(pr.autoFirst)},
                  {"autoMax", sheetSizeName(pr.autoMax)}};
    json index = json::array();
    for (const SheetIndexEntry& e : setInfo_.index)
        index.push_back({{"sheetId", e.sheetId}, {"number", e.number}, {"title", e.title}, {"role", name(e.role)}});
    json set = {{"present", setInfo_.present}, {"id", setInfo_.id},
                {"primarySheetId", setInfo_.primarySheetId}, {"multiSheet", setInfo_.multiSheet},
                {"layoutValid", setInfo_.layoutValid}, {"ready", setInfo_.ready},
                {"issues", setInfo_.issues}, {"index", index}};
    json root = {{"version", kSchemaVersion}, {"nextSheetId", nextSheetId_},
                 {"preferences", prefs}, {"drawingSet", set}, {"sheets", sheets}};
    return root.dump(2);
}

bool DrawingDocument::deserialize(const std::string& text, DrawingDocument& out, std::string& error) {
    error.clear();
    try {
        const json root = json::parse(text);
        const int ver = root.is_object() ? root.value("version", 0) : 0;
        if (ver < 1 || ver > kSchemaVersion) {
            error = "cizim kaydi: surum tanimsiz/desteklenmiyor";
            return false;
        }
        DrawingDocument doc;
        doc.nextSheetId_ = root.at("nextSheetId").get<ElementId>();
        if (root.contains("preferences")) {
            const json& jp = root.at("preferences");
            DrawingPreferences& p = doc.preferences_;
            const std::string sz = jp.value("sheetSize", std::string("auto"));
            if (sz == "auto") p.autoSheetSize = true;
            else {
                p.autoSheetSize = false;
                if (!parseSize(sz, p.sheetSize)) {
                    error = "cizim kaydi: bilinmeyen tercih kagit boyutu";
                    return false;
                }
            }
            if (!parseMode(jp.value("drawingMode", std::string("single")), p.mode)) {
                error = "cizim kaydi: bilinmeyen cizim modu";
                return false;
            }
            p.autoMultiSheetFallback = jp.value("autoMultiSheetFallback", true);
            if (!parseSize(jp.value("autoFirst", std::string("A3")), p.autoFirst) ||
                !parseSize(jp.value("autoMax", std::string("A0")), p.autoMax)) {
                error = "cizim kaydi: bilinmeyen otomatik kagit araligi";
                return false;
            }
        }
        if (root.contains("drawingSet")) {
            const json& js = root.at("drawingSet");
            DrawingSetInfo& si = doc.setInfo_;
            si.present = js.value("present", false);
            si.id = js.value("id", std::string{});
            si.primarySheetId = js.value("primarySheetId", ElementId{0});
            si.multiSheet = js.value("multiSheet", false);
            si.layoutValid = js.value("layoutValid", false);
            si.ready = js.value("ready", false);
            si.issues = js.value("issues", std::vector<std::string>{});
            for (const json& je : js.value("index", json::array())) {
                SheetIndexEntry e;
                e.sheetId = je.at("sheetId").get<ElementId>();
                e.number = je.at("number").get<std::string>();
                e.title = je.at("title").get<std::string>();
                if (!parseRole(je.value("role", std::string("continuation")), e.role)) {
                    error = "cizim kaydi: bilinmeyen pafta rolu";
                    return false;
                }
                si.index.push_back(std::move(e));
            }
        }
        for (const json& js : root.at("sheets")) {
            DrawingSheet s;
            s.id = js.at("id").get<ElementId>();
            s.name = js.at("name").get<std::string>();
            SheetSize size;
            if (!parseSize(js.at("size").get<std::string>(), size)) {
                error = "cizim kaydi: bilinmeyen kagit boyutu";
                return false;
            }
            s.format = sheetFormat(size);
            s.format.margin = js.at("margin").get<double>();
            if (!parse(js.at("kind").get<std::string>(), s.kind) ||
                !parse(js.at("projection").get<std::string>(), s.projection)) {
                error = "cizim kaydi: bilinmeyen tur/projeksiyon";
                return false;
            }
            s.subject = js.at("subject").get<EntityId>();
            s.scale = js.at("scale").get<double>();
            s.scaleIsStandard = js.at("scaleIsStandard").get<bool>();
            s.titleBlock = titleFrom(js.at("titleBlock"));
            for (const json& jv : js.at("views")) {
                DrawingView v;
                if (!viewFrom(jv, v)) {
                    error = "cizim kaydi: gorunus okunamadi";
                    return false;
                }
                s.views.push_back(std::move(v));
            }
            for (const json& jm : js.at("marks")) {
                MarkAnnotation m;
                m.id = jm.at("id").get<ElementId>();
                m.viewId = jm.at("viewId").get<ElementId>();
                m.target = jm.at("target").get<EntityId>();
                m.text = jm.at("text").get<std::string>();
                m.isAssembly = jm.at("isAssembly").get<bool>();
                m.anchor = v2From(jm.at("anchor"));
                m.labelPos = v2From(jm.at("labelPos"));
                s.marks.push_back(std::move(m));
            }
            for (const json& jd : js.at("dimensions")) {
                Dimension2D d;
                d.id = jd.at("id").get<ElementId>();
                d.viewId = jd.at("viewId").get<ElementId>();
                if (!parse(jd.at("role").get<std::string>(), d.role) ||
                    !parse(jd.at("direction").get<std::string>(), d.direction)) {
                    error = "cizim kaydi: bilinmeyen olcu rolu/yonu";
                    return false;
                }
                d.p1 = v2From(jd.at("p1"));
                d.p2 = v2From(jd.at("p2"));
                d.value = jd.at("value").get<double>();
                d.offset = jd.at("offset").get<double>();
                d.textLevel = jd.value("textLevel", 0);
                d.side = jd.value("side", 1);
                d.textT = jd.value("textT", 0.5);
                d.textSide = jd.value("textSide", -1);
                d.chain = jd.value("chain", ElementId{0});
                d.source = jd.at("source").get<EntityId>();
                d.featureKey = jd.value("featureKey", std::string{});
                d.basis = jd.value("basis", std::string{});
                d.documentRevision = jd.value("documentRevision", std::uint64_t{0});
                s.dimensions.push_back(std::move(d));
            }
            if (js.contains("texts"))
                for (const json& jt : js.at("texts")) {
                    TextAnnotation t;
                    t.id = jt.at("id").get<ElementId>();
                    t.viewId = jt.at("viewId").get<ElementId>();
                    if (!parse(jt.at("kind").get<std::string>(), t.kind)) {
                        error = "cizim kaydi: bilinmeyen metin turu";
                        return false;
                    }
                    t.lines = jt.at("lines").get<std::vector<std::string>>();
                    t.height = jt.at("height").get<double>();
                    t.hasAnchor = jt.at("hasAnchor").get<bool>();
                    t.anchor = v2From(jt.at("anchor"));
                    t.pos = v2From(jt.at("pos"));
                    t.leader = jt.at("leader").get<bool>();
                    t.bold = jt.at("bold").get<bool>();
                    t.source = jt.at("source").get<EntityId>();
                    t.featureKey = jt.at("featureKey").get<std::string>();
                    s.texts.push_back(std::move(t));
                }
            if (js.contains("fabrication")) {
                const json& jf = js.at("fabrication");
                s.fabrication.evaluated = jf.at("evaluated").get<bool>();
                s.fabrication.draft = jf.at("draft").get<bool>();
                s.fabrication.unresolvedCodes = jf.at("unresolvedCodes").get<std::vector<std::string>>();
                for (const json& jc : jf.at("checklist")) {
                    ChecklistItem c;
                    c.key = jc.at("key").get<std::string>();
                    c.label = jc.at("label").get<std::string>();
                    if (!parse(jc.at("state").get<std::string>(), c.state)) {
                        error = "cizim kaydi: bilinmeyen kontrol listesi durumu";
                        return false;
                    }
                    c.detail = jc.at("detail").get<std::string>();
                    c.featureKey = jc.at("featureKey").get<std::string>();
                    s.fabrication.checklist.push_back(std::move(c));
                }
            }
            if (js.contains("layoutConfig")) {
                const json& jl = js.at("layoutConfig");
                LayoutConfig& lc = s.layoutConfig;
                lc.dimTextHeight = jl.at("dimTextHeight").get<double>();
                lc.noteTextHeight = jl.at("noteTextHeight").get<double>();
                lc.minDimText = jl.at("minDimText").get<double>();
                lc.minNoteText = jl.at("minNoteText").get<double>();
                lc.clearance = jl.at("clearance").get<double>();
                lc.rowPitch = jl.at("rowPitch").get<double>();
                lc.firstOffset = jl.at("firstOffset").get<double>();
                if (!parseSize(jl.at("minSheet").get<std::string>(), lc.minSheet) ||
                    !parseSize(jl.at("maxSheet").get<std::string>(), lc.maxSheet)) {
                    error = "cizim kaydi: bilinmeyen yerlesim kagit boyutu";
                    return false;
                }
                lc.fixedScaleDen = jl.at("fixedScaleDen").get<double>();
                lc.maxIterations = jl.at("maxIterations").get<int>();
            }
            if (js.contains("layoutReport")) {
                const json& jr = js.at("layoutReport");
                LayoutReport& r = s.layout;
                r.ran = jr.at("ran").get<bool>();
                r.ok = jr.at("ok").get<bool>();
                if (!parseSize(jr.at("requestedSheet").get<std::string>(), r.requestedSheet) ||
                    !parseSize(jr.at("finalSheet").get<std::string>(), r.finalSheet)) {
                    error = "cizim kaydi: bilinmeyen yerlesim raporu kagidi";
                    return false;
                }
                r.scaleDenominator = jr.at("scaleDenominator").get<double>();
                r.attempts = jr.at("attempts").get<int>();
                r.escalations = jr.at("escalations").get<int>();
                for (const json& ji : jr.at("issues"))
                    r.issues.push_back({ji.at("code").get<std::string>(), ji.at("detail").get<std::string>(),
                                        ji.at("element").get<ElementId>()});
            }
            s.documentRevision = js.value("documentRevision", std::uint64_t{0});
            s.nextId = js.at("nextId").get<ElementId>();
            if (js.contains("role")) {
                if (!parseRole(js.at("role").get<std::string>(), s.role)) {
                    error = "cizim kaydi: bilinmeyen pafta rolu";
                    return false;
                }
                s.parentSheetId = js.value("parentSheetId", ElementId{0});
                s.sheetNumber = js.value("sheetNumber", std::string{});
                s.sheetIndex = js.value("sheetIndex", 1);
                for (const json& jr : js.value("references", json::array())) {
                    SheetReference r;
                    r.targetSheetId = jr.at("targetSheetId").get<ElementId>();
                    r.targetViewId = jr.value("targetViewId", ElementId{0});
                    r.targetNumber = jr.value("targetNumber", std::string{});
                    r.label = jr.value("label", std::string{});
                    r.sourceEntity = jr.value("sourceEntity", kInvalidEntityId);
                    s.references.push_back(std::move(r));
                }
            }
            doc.sheets_.push_back(std::move(s));
        }
        out = std::move(doc);
        return true;
    } catch (const std::exception& e) {
        error = std::string("cizim kaydi okunamadi: ") + e.what();
        return false;
    }
}

} // namespace mm::drawing
