#include "model_maker/drawing.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <exception>

namespace mm::drawing {
namespace {

using json = nlohmann::json;
constexpr int kSchemaVersion = 1;

// --- enum <-> string (kalici ad; sayisal deger degil => yeniden siralamaya dayanikli) ---
const char* name(DrawingKind k) { return k == DrawingKind::Assembly ? "assembly" : "singlePart"; }
const char* name(ViewKind k) {
    switch (k) {
    case ViewKind::Front: return "front";
    case ViewKind::Top: return "top";
    case ViewKind::Side: return "side";
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
    return false;
}
bool parse(const std::string& s, LineStyle& o) {
    if (s == "visible") { o = LineStyle::Visible; return true; }
    if (s == "hidden") { o = LineStyle::Hidden; return true; }
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
    return false;
}
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
    json mem = json::array();
    for (const ViewMember& m : v.members)
        mem.push_back({{"entity", m.entity}, {"bounds", boundsJson(m.bounds)}});
    return {{"id", v.id}, {"kind", name(v.kind)}, {"subject", v.subject}, {"scale", v.scale},
            {"origin", v2(v.origin)}, {"projectionOffset", v2(v.projectionOffset)},
            {"segments", segs}, {"bounds", boundsJson(v.bounds)},
            {"members", mem}, {"label", v.label}};
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
    for (const json& m : j.at("members"))
        v.members.push_back({m.at("entity").get<EntityId>(), boundsFrom(m.at("bounds"))});
    v.label = j.at("label").get<std::string>();
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
                            {"value", d.value}, {"offset", d.offset}, {"source", d.source}});
        json views = json::array();
        for (const DrawingView& v : s.views) views.push_back(viewJson(v));
        sheets.push_back(
            {{"id", s.id}, {"name", s.name}, {"size", sheetSizeName(s.format.size)},
             {"margin", s.format.margin}, {"kind", name(s.kind)},
             {"projection", name(s.projection)}, {"subject", s.subject}, {"scale", s.scale},
             {"scaleIsStandard", s.scaleIsStandard}, {"titleBlock", titleJson(s.titleBlock)},
             {"views", views}, {"marks", marks}, {"dimensions", dims}, {"nextId", s.nextId}});
    }
    json root = {{"version", kSchemaVersion}, {"nextSheetId", nextSheetId_}, {"sheets", sheets}};
    return root.dump(2);
}

bool DrawingDocument::deserialize(const std::string& text, DrawingDocument& out, std::string& error) {
    error.clear();
    try {
        const json root = json::parse(text);
        if (!root.is_object() || root.value("version", 0) != kSchemaVersion) {
            error = "cizim kaydi: surum tanimsiz/desteklenmiyor";
            return false;
        }
        DrawingDocument doc;
        doc.nextSheetId_ = root.at("nextSheetId").get<ElementId>();
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
                d.source = jd.at("source").get<EntityId>();
                s.dimensions.push_back(std::move(d));
            }
            s.nextId = js.at("nextId").get<ElementId>();
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
