#include "model_maker/paper_editing.hpp"

#include "model_maker/drawing_layout.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace mm::drawing {
namespace {

double dot(const Vec2& a, const Vec2& b) noexcept { return a.x * b.x + a.y * b.y; }
Vec2 sub(const Vec2& a, const Vec2& b) noexcept { return {a.x - b.x, a.y - b.y}; }
Vec2 add(const Vec2& a, const Vec2& b) noexcept { return {a.x + b.x, a.y + b.y}; }
double len(const Vec2& v) noexcept { return std::hypot(v.x, v.y); }

double distPointSegment(const Vec2& p, const Vec2& a, const Vec2& b) noexcept {
    const Vec2 ab = sub(b, a);
    const double l2 = dot(ab, ab);
    if (l2 < 1e-12) return len(sub(p, a));
    double t = dot(sub(p, a), ab) / l2;
    t = std::clamp(t, 0.0, 1.0);
    return len(sub(p, {a.x + ab.x * t, a.y + ab.y * t}));
}

bool pointInObb(const Obb& o, const Vec2& p, double tol) noexcept {
    const double a = o.angleDeg * 3.14159265358979323846 / 180.0;
    const double c = std::cos(a), s = std::sin(a);
    const double dx = p.x - o.c.x, dy = p.y - o.c.y;
    const double lx = dx * c + dy * s;
    const double ly = -dx * s + dy * c;
    return std::fabs(lx) <= o.hx + tol && std::fabs(ly) <= o.hy + tol;
}

bool boundsHit(const Bounds2& b, const Vec2& p, double tol) noexcept {
    if (b.empty) return false;
    return p.x >= b.min.x - tol && p.x <= b.max.x + tol && p.y >= b.min.y - tol && p.y <= b.max.y + tol;
}

Dimension2D* findDim(DrawingSheet& s, ElementId id) {
    for (Dimension2D& d : s.dimensions)
        if (d.id == id) return &d;
    return nullptr;
}

std::string viewKey(const DrawingView& v) {
    return std::to_string(static_cast<int>(v.kind)) + "|" + std::to_string(v.subject) + "|" +
           std::to_string(v.detailPlate);
}
std::string viewKeyOf(const DrawingSheet& s, ElementId viewId) {
    if (viewId == 0) return "page";
    const DrawingView* v = s.findView(viewId);
    return v ? viewKey(*v) : "missing";
}
std::string dimKey(const DrawingSheet& s, const Dimension2D& d) {
    return viewKeyOf(s, d.viewId) + "|" + std::to_string(static_cast<int>(d.role)) + "|" + d.featureKey + "|" +
           std::to_string(d.chain) + "|" + std::to_string(static_cast<int>(d.direction));
}
std::string markKey(const DrawingSheet& s, const MarkAnnotation& m) {
    return viewKeyOf(s, m.viewId) + "|" + std::to_string(m.target) + "|" + (m.isAssembly ? "A" : "P");
}
std::string textKey(const DrawingSheet& s, const TextAnnotation& t) {
    return viewKeyOf(s, t.viewId) + "|" + std::to_string(static_cast<int>(t.kind)) + "|" + t.featureKey + "|" +
           (t.lines.empty() ? std::string() : t.lines.front());
}

} // namespace

PaperHit hitTestPaper(const DrawingSheet& s, Vec2 p, double tolMm) {
    PaperHit hit;
    hit.paper = p;
    // 1) kullanici 2B sekilleri (en ust katman)
    for (const PaperShape& sh : s.shapes) {
        if (!sh.visible) continue;
        double d = 1e300;
        if (sh.kind == ShapeKind::Circle) d = std::fabs(len(sub(p, sh.center)) - sh.radius);
        else d = distPointSegment(p, sh.a, sh.b);
        if (d <= tolMm) {
            hit.kind = PaperHitKind::Shape;
            hit.id = sh.id;
            return hit;
        }
    }
    // 2) metinler
    for (const TextAnnotation& t : s.texts) {
        if (!t.visible) continue;
        const TextGeometry g = resolveText(s, t);
        if (pointInObb(g.box, p, tolMm)) {
            hit.kind = PaperHitKind::Text;
            hit.id = t.id;
            return hit;
        }
    }
    // 3) mark etiketleri
    for (const MarkAnnotation& m : s.marks) {
        if (!m.visible) continue;
        if (boundsHit(markBox(m), p, tolMm)) {
            hit.kind = PaperHitKind::Mark;
            hit.id = m.id;
            return hit;
        }
    }
    // 4) olculer (metin kutusu veya olcu/uzatma cizgileri)
    for (const Dimension2D& d : s.dimensions) {
        if (!d.visible) continue;
        const DimensionGeometry g = resolveDimension(s, d);
        if (!g.valid) continue;
        bool in = pointInObb(g.textBox, p, tolMm);
        for (const LineSeg& l : g.lines)
            if (!in && distPointSegment(p, l.a, l.b) <= tolMm) in = true;
        if (in) {
            hit.kind = PaperHitKind::Dimension;
            hit.id = d.id;
            return hit;
        }
    }
    // 5) gorunus (en alt katman)
    for (const DrawingView& v : s.views) {
        if (boundsHit(v.paperBounds(), p, tolMm)) {
            hit.kind = PaperHitKind::View;
            hit.id = v.id;
            return hit;
        }
    }
    return hit;
}

bool movePaperElement(DrawingSheet& s, const PaperHit& hit, Vec2 deltaMm) {
    if (!hit.valid()) return false;
    if (std::fabs(deltaMm.x) < 1e-12 && std::fabs(deltaMm.y) < 1e-12) return false;
    switch (hit.kind) {
    case PaperHitKind::View:
        for (DrawingView& v : s.views)
            if (v.id == hit.id) {
                v.origin = add(v.origin, deltaMm);
                return true;
            }
        return false;
    case PaperHitKind::Mark:
        for (MarkAnnotation& m : s.marks)
            if (m.id == hit.id) {
                m.labelPos = add(m.labelPos, deltaMm);
                return true;
            }
        return false;
    case PaperHitKind::Text:
        for (TextAnnotation& t : s.texts)
            if (t.id == hit.id) {
                t.pos = add(t.pos, deltaMm);
                return true;
            }
        return false;
    case PaperHitKind::Shape:
        for (PaperShape& sh : s.shapes)
            if (sh.id == hit.id) {
                sh.a = add(sh.a, deltaMm);
                sh.b = add(sh.b, deltaMm);
                sh.center = add(sh.center, deltaMm);
                return true;
            }
        return false;
    case PaperHitKind::Dimension: {
        Dimension2D* d = findDim(s, hit.id);
        if (!d) return false;
        const DrawingView* v = s.findView(d->viewId);
        if (!v) return false;
        const Vec2 a = v->toPaper(d->p1);
        const Vec2 b = v->toPaper(d->p2);
        const Vec2 ab = sub(b, a);
        const double l = len(ab);
        Vec2 outward{0.0, 0.0};
        if (d->direction == DimensionDirection::Horizontal) {
            outward = {0.0, d->side >= 0 ? -1.0 : 1.0};
        } else if (d->direction == DimensionDirection::Vertical) {
            outward = {d->side >= 0 ? 1.0 : -1.0, 0.0};
        } else if (l > 1e-9) {
            Vec2 n{-ab.y / l, ab.x / l};
            const Vec2 mid{(a.x + b.x) / 2.0, (a.y + b.y) / 2.0};
            const Vec2 c = v->paperBounds().centre();
            const double sgn = (n.x * (mid.x - c.x) + n.y * (mid.y - c.y)) >= 0.0 ? 1.0 : -1.0;
            n = {n.x * sgn, n.y * sgn};
            outward = n;
        }
        d->offset = std::max(0.5, d->offset + dot(deltaMm, outward));
        if (l > 1e-9) d->textT += dot(deltaMm, {ab.x / l, ab.y / l}) / l;
        return true;
    }
    case PaperHitKind::None:
        break;
    }
    return false;
}

ElementId addPaperShape(DrawingSheet& s, ShapeKind kind, Vec2 a, Vec2 b, double radius, EntityId source) {
    PaperShape sh;
    sh.id = s.allocateId();
    sh.kind = kind;
    sh.a = a;
    sh.b = b;
    sh.center = a;
    sh.radius = radius > 0.0 ? radius : len(sub(b, a));
    sh.style = LineStyle::Visible;
    sh.visible = true;
    sh.source = source;
    sh.featureKey = kind == ShapeKind::Circle ? "user:circle" : (kind == ShapeKind::Centerline ? "user:centerline" : "user:line");
    s.shapes.push_back(std::move(sh));
    return s.shapes.back().id;
}

bool setPaperElementVisible(DrawingSheet& s, const PaperHit& hit, bool visible) {
    switch (hit.kind) {
    case PaperHitKind::Dimension:
        if (Dimension2D* d = findDim(s, hit.id)) {
            d->visible = visible;
            return true;
        }
        return false;
    case PaperHitKind::Text:
        for (TextAnnotation& t : s.texts)
            if (t.id == hit.id) {
                t.visible = visible;
                return true;
            }
        return false;
    case PaperHitKind::Mark:
        for (MarkAnnotation& m : s.marks)
            if (m.id == hit.id) {
                m.visible = visible;
                return true;
            }
        return false;
    case PaperHitKind::Shape:
        for (PaperShape& sh : s.shapes)
            if (sh.id == hit.id) {
                sh.visible = visible;
                return true;
            }
        return false;
    case PaperHitKind::View:   // gorunus kagit duzenlemesiyle gizlenmez
    case PaperHitKind::None:
        return false;
    }
    return false;
}

void evaluateAnnotationStatus(DrawingSheet& s) {
    int hiddenFabDims = 0;
    for (const Dimension2D& d : s.dimensions)
        if (!d.featureKey.empty() && !d.visible) ++hiddenFabDims;
    int hiddenNotes = 0;
    for (const TextAnnotation& t : s.texts)
        if (!t.visible && (t.kind == TextKind::HoleCallout || t.kind == TextKind::PlateNote ||
                           t.kind == TextKind::StatusStamp || t.kind == TextKind::Legend))
            ++hiddenNotes;
    const bool missing = hiddenFabDims > 0 || hiddenNotes > 0;

    ChecklistItem* item = nullptr;
    for (ChecklistItem& c : s.fabrication.checklist)
        if (c.key == "ANNOTATIONS") item = &c;
    if (!s.fabrication.checklist.empty()) {
        if (!item) {
            ChecklistItem c;
            c.key = "ANNOTATIONS";
            c.label = "Zorunlu imalat ölçü/notları görünür";
            c.featureKey = "annotations";
            s.fabrication.checklist.push_back(std::move(c));
            item = &s.fabrication.checklist.back();
        }
        item->state = missing ? ChecklistState::Unresolved : ChecklistState::Satisfied;
        item->detail = missing ? ("gizlenen zorunlu öğe: " + std::to_string(hiddenFabDims) + " ölçü, " +
                                  std::to_string(hiddenNotes) + " not")
                               : std::string();
        bool draft = false;
        for (const ChecklistItem& c : s.fabrication.checklist)
            if (c.state == ChecklistState::Unresolved) draft = true;
        s.fabrication.draft = draft;
    } else if (missing) {
        s.fabrication.draft = true;
    }
    std::vector<std::string>& codes = s.fabrication.unresolvedCodes;
    codes.erase(std::remove(codes.begin(), codes.end(), std::string("ANNOTATION_INCOMPLETE")), codes.end());
    if (missing) codes.push_back("ANNOTATION_INCOMPLETE");
}

void preservePaperEdits(const DrawingSheet& edited, DrawingSheet& regenerated) {
    std::map<std::string, const DrawingView*> editedViews;
    for (const DrawingView& v : edited.views) editedViews.emplace(viewKey(v), &v);
    for (DrawingView& v : regenerated.views) {
        const auto it = editedViews.find(viewKey(v));
        if (it != editedViews.end()) v.origin = it->second->origin;
    }

    std::map<std::string, const Dimension2D*> editedDims;
    for (const Dimension2D& d : edited.dimensions) editedDims.emplace(dimKey(edited, d), &d);
    for (Dimension2D& d : regenerated.dimensions) {
        const auto it = editedDims.find(dimKey(regenerated, d));
        if (it == editedDims.end()) continue;
        d.offset = it->second->offset;
        d.side = it->second->side;
        d.textT = it->second->textT;
        d.textSide = it->second->textSide;
        d.textLevel = it->second->textLevel;
        d.visible = it->second->visible;   // gizleme de kullanici tercihidir (INCOMPLETE isaretlenir)
    }

    std::map<std::string, const MarkAnnotation*> editedMarks;
    for (const MarkAnnotation& m : edited.marks) editedMarks.emplace(markKey(edited, m), &m);
    for (MarkAnnotation& m : regenerated.marks) {
        const auto it = editedMarks.find(markKey(regenerated, m));
        if (it != editedMarks.end()) {
            m.labelPos = it->second->labelPos;
            m.visible = it->second->visible;
        }
    }

    std::map<std::string, const TextAnnotation*> editedTexts;
    for (const TextAnnotation& t : edited.texts) editedTexts.emplace(textKey(edited, t), &t);
    for (TextAnnotation& t : regenerated.texts) {
        const auto it = editedTexts.find(textKey(regenerated, t));
        if (it != editedTexts.end()) {
            t.pos = it->second->pos;
            t.visible = it->second->visible;
        }
    }

    regenerated.shapes = edited.shapes;   // kullanici 2B cizimleri: kimlik (ElementId) korunur
    ElementId next = regenerated.nextId;
    for (const PaperShape& sh : regenerated.shapes) next = std::max(next, sh.id + 1);
    regenerated.nextId = next;

    evaluateAnnotationStatus(regenerated);
}

PaperEditMerge mergePaperEdits(const std::vector<DrawingSheet>& edited, DrawingSheet& regenerated,
                               bool withShapes) {
    PaperEditMerge out;
    for (const DrawingSheet& e : edited) {
        preservePaperEdits(e, regenerated);   // eslesen gorunus/olcu/mark/metin yerlesimi
        ++out.sheets;
    }
    if (withShapes) {
        // preservePaperEdits sekilleri her cagrida EZER: tum duzenlenmis paftalarin sekillerini birlestir.
        std::vector<PaperShape> shapes;
        for (const DrawingSheet& e : edited) shapes.insert(shapes.end(), e.shapes.begin(), e.shapes.end());
        regenerated.shapes.clear();
        appendPaperShapes(regenerated, shapes);
    }
    out.shapes = regenerated.shapes.size();
    evaluateAnnotationStatus(regenerated);
    return out;
}

std::size_t appendPaperShapes(DrawingSheet& s, const std::vector<PaperShape>& shapes) {
    const auto sameShape = [](const PaperShape& a, const PaperShape& b) {
        if (a.kind != b.kind || a.featureKey != b.featureKey) return false;
        const auto close = [](double x, double y) { return std::fabs(x - y) < 1e-9; };
        return close(a.a.x, b.a.x) && close(a.a.y, b.a.y) && close(a.b.x, b.b.x) && close(a.b.y, b.b.y) &&
               close(a.center.x, b.center.x) && close(a.center.y, b.center.y) && close(a.radius, b.radius);
    };
    std::size_t added = 0;
    for (const PaperShape& src : shapes) {
        bool duplicate = false;
        for (const PaperShape& u : s.shapes) duplicate = duplicate || sameShape(u, src);
        if (duplicate) continue;
        PaperShape c = src;
        bool idTaken = false;
        for (const PaperShape& u : s.shapes) idTaken = idTaken || u.id == c.id;
        if (c.id == 0 || idTaken) c.id = s.allocateId();
        s.shapes.push_back(c);
        ++added;
    }
    return added;
}

PaperEditHistory::PaperEditHistory(DrawingSheet initial, std::size_t limit)
    : sheet_(std::move(initial)), limit_(limit ? limit : 1) {}

void PaperEditHistory::begin() {
    pending_ = sheet_;
    open_ = true;
}

void PaperEditHistory::commit() {
    if (!open_) return;
    open_ = false;
    DrawingDocument a;
    DrawingDocument b;
    a.addSheet(pending_);
    b.addSheet(sheet_);
    if (a.serialize() == b.serialize()) return;
    undo_.push_back(std::move(pending_));
    if (undo_.size() > limit_) undo_.erase(undo_.begin());
    redo_.clear();
}

void PaperEditHistory::abort() {
    open_ = false;
}

bool PaperEditHistory::undo() {
    if (undo_.empty()) return false;
    redo_.push_back(sheet_);
    sheet_ = std::move(undo_.back());
    undo_.pop_back();
    return true;
}

bool PaperEditHistory::redo() {
    if (redo_.empty()) return false;
    undo_.push_back(sheet_);
    sheet_ = std::move(redo_.back());
    redo_.pop_back();
    return true;
}

void PaperEditHistory::reset(DrawingSheet sheet) {
    sheet_ = std::move(sheet);
    undo_.clear();
    redo_.clear();
    open_ = false;
}

} // namespace mm::drawing
