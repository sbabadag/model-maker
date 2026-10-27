#include "model_maker/drawing_builder.hpp"

#include "model_maker/utf8.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace mm::drawing {
namespace {

const char* labelOf(ViewKind k) {
    switch (k) {
    case ViewKind::Front: return "FRONT";
    case ViewKind::Top: return "TOP";
    case ViewKind::Side: return "SIDE";
    }
    return "";
}

double dist3(const Vec3& a, const Vec3& b) {
    const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
Vec3 bboxCentre(const WireframeModel& m) {
    if (m.vertices().empty()) return {};
    Vec3 lo = m.vertices()[0], hi = lo;
    for (const Vec3& v : m.vertices()) {
        lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)};
        hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)};
    }
    return {(lo.x + hi.x) * 0.5, (lo.y + hi.y) * 0.5, (lo.z + hi.z) * 0.5};
}

// Eksen (properties'ten); ikisi de sifirsa/cakisiksa yok.
bool axisOf(const WireframeModel& m, Vec3& a, Vec3& b) {
    const EntityProperties& p = m.properties();
    a = {p.axisFromX, p.axisFromY, p.axisFromZ};
    b = {p.axisToX, p.axisToY, p.axisToZ};
    return dist3(a, b) > 1e-6;
}

// --- hidden-line (deneysel: arka-yuz testi, oklüzyon yok) ---
Vec3 faceNormal(const std::vector<Vec3>& v, const Face& f) {
    Vec3 n{};
    for (std::size_t i = 0; i < f.size(); ++i) {
        const Vec3& a = v[f[i]];
        const Vec3& b = v[f[(i + 1) % f.size()]];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    const double l = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    if (l < 1e-12) return {};
    return {n.x / l, n.y / l, n.z / l};
}

std::set<std::pair<std::size_t, std::size_t>> hiddenEdges(const WireframeModel& m, ViewKind kind) {
    std::set<std::pair<std::size_t, std::size_t>> hidden;
    const auto& v = m.vertices();
    if (m.faces().empty() || v.empty()) return hidden;
    Vec3 toViewer{0, 0, 1};
    switch (kind) {
    case ViewKind::Front: toViewer = {0, -1, 0}; break;
    case ViewKind::Top: toViewer = {0, 0, 1}; break;
    case ViewKind::Side: toViewer = {1, 0, 0}; break;
    }
    const Vec3 c = bboxCentre(m);
    std::map<std::pair<std::size_t, std::size_t>, std::vector<double>> dots;
    for (const Face& f : m.faces()) {
        if (f.size() < 3) continue;
        Vec3 n = faceNormal(v, f);
        Vec3 fc{};
        for (std::size_t i : f) fc = {fc.x + v[i].x, fc.y + v[i].y, fc.z + v[i].z};
        fc = {fc.x / f.size(), fc.y / f.size(), fc.z / f.size()};
        if (n.x * (fc.x - c.x) + n.y * (fc.y - c.y) + n.z * (fc.z - c.z) < 0) n = {-n.x, -n.y, -n.z};
        const double d = n.x * toViewer.x + n.y * toViewer.y + n.z * toViewer.z;
        for (std::size_t i = 0; i < f.size(); ++i) {
            std::size_t a = f[i], b = f[(i + 1) % f.size()];
            if (a > b) std::swap(a, b);
            dots[{a, b}].push_back(d);
        }
    }
    for (const auto& [edge, ds] : dots)
        if (std::all_of(ds.begin(), ds.end(), [](double d) { return d < -1e-6; })) hidden.insert(edge);
    return hidden;
}

Vec3 viewNormal(ViewKind k) {
    switch (k) {
    case ViewKind::Front: return {0, 1, 0};
    case ViewKind::Top: return {0, 0, 1};
    case ViewKind::Side: return {1, 0, 0};
    }
    return {0, 1, 0};
}

std::string fmtNum(double v) {
    char buf[32];
    const double r = std::round(v);
    if (std::fabs(v - r) < 0.05) std::snprintf(buf, sizeof(buf), "%.0f", r);
    else std::snprintf(buf, sizeof(buf), "%.1f", v);
    return buf;
}

// Sirali, `tol` icinde birlesen benzersiz degerler (kume basinin degeri).
std::vector<double> uniqueSorted(std::vector<double> xs, double tol) {
    std::sort(xs.begin(), xs.end());
    std::vector<double> out;
    for (const double x : xs)
        if (out.empty() || x - out.back() > tol) out.push_back(x);
    return out;
}

std::string deriveName(const WireframeModel& m) {
    const EntityProperties& p = m.properties();
    if (!p.name.empty()) return p.name;
    if (!p.profileName.empty()) return p.profileName;
    return {};
}

} // namespace

Vec2 projectToView(const Vec3& p, ViewKind kind) noexcept {
    switch (kind) {
    case ViewKind::Front: return {p.x, p.z};
    case ViewKind::Top: return {p.x, p.y};
    case ViewKind::Side: return {p.y, p.z};
    }
    return {p.x, p.z};
}

Bounds2 markLabelBox(const MarkAnnotation& m) noexcept {
    const double h = m.isAssembly ? 5.0 : 3.5;
    const double w = 3.0 + h * 0.75 * static_cast<double>(std::max<std::size_t>(m.text.size(), 2));
    Bounds2 b;
    b.include(m.labelPos);
    b.include({m.labelPos.x + w, m.labelPos.y + h + 2.0});
    return b;
}

bool isDrawablePart(const WireframeModel& m) noexcept {
    return !m.isPointEntity() && m.constructionKind() == ConstructionKind::None &&
           !m.edges().empty() && m.vertices().size() >= 2;
}

DocumentMarkSummary markDocument(const Document& doc, MarkRegistry& reg) {
    DocumentMarkSummary s;
    const auto& models = doc.models();
    for (std::size_t i = 0; i < models.size(); ++i) {
        if (!isDrawablePart(models[i])) continue;
        reg.markPart(doc.modelId(i), partSignatureOf(models[i]));
        ++s.parts;
    }
    for (const AssemblyRecord& a : doc.assemblies()) {
        AssemblySignature sig;
        std::vector<Vec3> centres;
        if (const auto mk = reg.markOf(a.mainPart)) sig.mainPartMark = mk->number;
        for (const EntityId p : a.parts) {
            const auto idx = doc.indexOfEntity(p);
            if (!idx) continue;
            if (const auto mk = reg.markOf(p)) sig.memberPartMarks.push_back(mk->number);
            centres.push_back(bboxCentre(models[*idx]));
        }
        sig.layoutHash = layoutHashOf(centres);
        reg.markAssembly(a.id, sig);
        ++s.assemblies;
    }
    return s;
}

std::vector<EntityId> subjectParts(const Document& doc, EntityId subject, DrawingKind& kind,
                                   std::string& error) {
    error.clear();
    if (const auto ai = doc.indexOfAssembly(subject)) {
        kind = DrawingKind::Assembly;
        std::vector<EntityId> parts;
        for (const EntityId p : doc.assemblies()[*ai].parts) {
            const auto idx = doc.indexOfEntity(p);
            if (idx && isDrawablePart(doc.models()[*idx])) parts.push_back(p);
        }
        if (parts.empty()) error = "assembly'de cizilebilir parca yok";
        return parts;
    }
    kind = DrawingKind::SinglePart;
    const auto idx = doc.indexOfEntity(subject);
    if (!idx) {
        error = "secilen eleman bulunamadi";
        return {};
    }
    if (!isDrawablePart(doc.models()[*idx])) {
        error = "secilen eleman cizilebilir bir parca degil";
        return {};
    }
    return {subject};
}

DrawingView generateView(const Document& doc, const std::vector<EntityId>& parts, EntityId subject,
                         ViewKind kind, bool hiddenLines) {
    DrawingView view;
    view.kind = kind;
    view.subject = subject;
    view.label = labelOf(kind);

    struct Raw { Vec2 a, b; LineStyle style; };
    std::vector<Raw> raw;
    std::vector<std::pair<EntityId, Bounds2>> memberRaw;
    Bounds2 rawBounds;

    for (const EntityId id : parts) {
        const auto idx = doc.indexOfEntity(id);
        if (!idx) continue;
        const WireframeModel& m = doc.models()[*idx];
        const auto& v = m.vertices();
        std::set<std::pair<std::size_t, std::size_t>> hid;
        if (hiddenLines) hid = hiddenEdges(m, kind);
        Bounds2 mb;
        for (const Vec3& p : v) mb.include(projectToView(p, kind));
        for (const Edge& e : m.edges()) {
            if (e.from >= v.size() || e.to >= v.size()) continue;
            const Vec2 a = projectToView(v[e.from], kind);
            const Vec2 b = projectToView(v[e.to], kind);
            if (std::hypot(b.x - a.x, b.y - a.y) < 1e-6) continue; // goruntude nokta
            const auto key = std::minmax(e.from, e.to);
            raw.push_back({a, b, hid.count({key.first, key.second}) ? LineStyle::Hidden
                                                                       : LineStyle::Visible});
            rawBounds.include(a);
            rawBounds.include(b);
        }
        if (!mb.empty) memberRaw.emplace_back(id, mb);
    }
    if (rawBounds.empty) return view;

    // Yinelenenleri at (yon bagimsiz, 0.01 mm); gorunur gizliyi yener.
    const auto q = [](double v) { return std::llround(v * 100.0); };
    using Key = std::array<long long, 4>;
    const auto keyOf = [&](const Vec2& a, const Vec2& b) {
        Key k1{q(a.x), q(a.y), q(b.x), q(b.y)};
        Key k2{q(b.x), q(b.y), q(a.x), q(a.y)};
        return std::min(k1, k2);
    };
    std::map<Key, std::size_t> seen;
    std::vector<Raw> unique;
    for (const Raw& r : raw) {
        const Key k = keyOf(r.a, r.b);
        const auto it = seen.find(k);
        if (it == seen.end()) {
            seen[k] = unique.size();
            unique.push_back(r);
        } else if (r.style == LineStyle::Visible) {
            unique[it->second].style = LineStyle::Visible;
        }
    }

    const Vec2 lo = rawBounds.min;
    view.projectionOffset = lo;
    const auto local = [&](const Vec2& p) { return Vec2{p.x - lo.x, p.y - lo.y}; };
    for (const Raw& r : unique) {
        view.segments.push_back({local(r.a), local(r.b), r.style});
        view.bounds.include(local(r.a));
        view.bounds.include(local(r.b));
    }
    for (const auto& [id, mb] : memberRaw) {
        ViewMember vm;
        vm.entity = id;
        vm.bounds.include(local(mb.min));
        vm.bounds.include(local(mb.max));
        view.members.push_back(vm);
    }

    // --- delikler (CutKind::Hole): eksen gorunuse dik -> daire; eksen gorunus duzleminde ve
    //     hiddenLines acik -> kesik cizgi (deligin parca icindeki iki kenari). Egik eksen atlanir.
    //     Delik boyu = parcanin delik ekseni boyunca uzanimi (sinirli derinlik/yuva dikkate alinmaz).
    {
        const Vec3 n = viewNormal(kind);
        struct Group { EntityId owner; double dia; std::size_t first; int count; };
        std::set<Key> holeSeen;
        std::vector<Group> groups;
        for (const EntityId id : parts) {
            const auto pidx = doc.indexOfEntity(id);
            if (!pidx) continue;
            const WireframeModel& pm = doc.models()[*pidx];
            for (const std::size_t mi : doc.cutModifiersForTarget(id)) {
                const CutModifier& hm = doc.cutModifiers()[mi];
                if (hm.kind != CutKind::Hole || !hm.enabled || hm.diameter <= 0.0) continue;
                const double al = std::sqrt(hm.planeNormal.x * hm.planeNormal.x + hm.planeNormal.y * hm.planeNormal.y +
                                            hm.planeNormal.z * hm.planeNormal.z);
                if (al < 1e-9) continue;
                const Vec3 ax{hm.planeNormal.x / al, hm.planeNormal.y / al, hm.planeNormal.z / al};
                const double d = std::fabs(ax.x * n.x + ax.y * n.y + ax.z * n.z);
                const Vec2 c = local(projectToView(hm.planeOrigin, kind));
                if (d >= 0.999) {
                    Circle2 ci;
                    ci.center = c;
                    ci.radius = hm.diameter / 2.0;
                    ci.owner = id;
                    bool found = false;
                    for (Group& g : groups)
                        if (g.owner == id && std::fabs(g.dia - hm.diameter) < 1e-6) { ++g.count; found = true; break; }
                    if (!found) groups.push_back({id, hm.diameter, view.circles.size(), 1});
                    view.circles.push_back(std::move(ci));
                } else if (d <= 0.001 && hiddenLines && !pm.vertices().empty()) {
                    double tmin = 1e300, tmax = -1e300;
                    for (const Vec3& vtx : pm.vertices()) {
                        const double t = (vtx.x - hm.planeOrigin.x) * ax.x + (vtx.y - hm.planeOrigin.y) * ax.y +
                                         (vtx.z - hm.planeOrigin.z) * ax.z;
                        tmin = std::min(tmin, t);
                        tmax = std::max(tmax, t);
                    }
                    const auto at = [&](double t) {
                        return local(projectToView({hm.planeOrigin.x + ax.x * t, hm.planeOrigin.y + ax.y * t,
                                                    hm.planeOrigin.z + ax.z * t}, kind));
                    };
                    const Vec2 a = at(tmin), b = at(tmax);
                    const double len = std::hypot(b.x - a.x, b.y - a.y);
                    if (len < 1e-6) continue;
                    const double r = hm.diameter / 2.0;
                    const Vec2 pp{-(b.y - a.y) / len * r, (b.x - a.x) / len * r};
                    const Vec2 s1a{a.x + pp.x, a.y + pp.y}, s1b{b.x + pp.x, b.y + pp.y};
                    const Vec2 s2a{a.x - pp.x, a.y - pp.y}, s2b{b.x - pp.x, b.y - pp.y};
                    for (const auto& sg : {std::pair{s1a, s1b}, std::pair{s2a, s2b}})
                        if (holeSeen.insert(keyOf(sg.first, sg.second)).second)  // ayni izdusum tek kez
                            view.segments.push_back({sg.first, sg.second, LineStyle::Hidden});
                }
            }
        }
        for (const Group& g : groups)
            view.circles[g.first].label =
                (g.count > 1 ? std::to_string(g.count) + " x " : std::string()) + "\xC3\x98" + fmtNum(g.dia);
    }
    return view;
}

bool placeViews(DrawingSheet& sheet, const DrawingOptions& opt) {
    const Bounds2 area = sheet.drawingArea();
    const double availW = area.width() - 2.0 * opt.padding;
    const double availH = area.height() - 2.0 * opt.padding;
    if (availW <= 0.0 || availH <= 0.0 || sheet.views.empty()) return false;

    // Hucre atamasi: (kolon, satir). Satir 0 = altta.
    const bool third = sheet.projection == ProjectionMethod::ThirdAngle;
    const auto cell = [&](ViewKind k) -> std::pair<int, int> {
        switch (k) {
        case ViewKind::Front: return third ? std::pair{0, 0} : std::pair{1, 1};
        case ViewKind::Top: return third ? std::pair{0, 1} : std::pair{1, 0};
        case ViewKind::Side: return third ? std::pair{1, 0} : std::pair{0, 1};
        }
        return {0, 0};
    };
    // Kullanilan kolon/satirlari sikistir.
    std::set<int> cols, rows;
    for (const DrawingView& v : sheet.views) {
        cols.insert(cell(v.kind).first);
        rows.insert(cell(v.kind).second);
    }
    const auto rank = [](const std::set<int>& s, int v) {
        return static_cast<int>(std::distance(s.begin(), s.find(v)));
    };
    const int nc = static_cast<int>(cols.size()), nr = static_cast<int>(rows.size());
    std::vector<double> colW(nc, 0.0), rowH(nr, 0.0); // model mm
    for (const DrawingView& v : sheet.views) {
        const auto [c, r] = cell(v.kind);
        colW[rank(cols, c)] = std::max(colW[rank(cols, c)], v.bounds.width());
        rowH[rank(rows, r)] = std::max(rowH[rank(rows, r)], v.bounds.height());
    }
    double sumW = 0.0, sumH = 0.0;
    for (double w : colW) sumW += w;
    for (double h : rowH) sumH += h;
    const double gapW = opt.viewGap * std::max(0, nc - 1);
    const double gapH = opt.viewGap * std::max(0, nr - 1);

    static const double kDen[] = {1, 2, 5, 10, 20, 25, 50, 75, 100, 125, 150, 200, 250, 500, 1000};
    double scale = 0.0;
    bool standard = false;
    double denChosen = 0.0;
    for (const double den : kDen) {
        const double s = 1.0 / den;
        if (sumW * s + gapW <= availW && sumH * s + gapH <= availH) {
            scale = s;
            standard = true;
            denChosen = den;
            break;
        }
    }
    if (!standard) {
        const double sw = sumW > 0 ? (availW - gapW) / sumW : 1.0;
        const double sh = sumH > 0 ? (availH - gapH) / sumH : 1.0;
        scale = std::min(sw, sh);
        if (scale <= 0.0) return false;
        denChosen = 1.0 / scale;
    }
    sheet.scale = scale;
    sheet.scaleIsStandard = standard;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "1:%.*f", standard ? 0 : 1, denChosen);
    sheet.titleBlock.scaleText = buf;

    // Blogu cizim alaninda ortala.
    const double totalW = sumW * scale + gapW, totalH = sumH * scale + gapH;
    const double x0 = area.min.x + opt.padding + (availW - totalW) * 0.5;
    const double y0 = area.min.y + opt.padding + (availH - totalH) * 0.5;
    std::vector<double> colX(nc, 0.0), rowY(nr, 0.0);
    double x = x0;
    for (int c = 0; c < nc; ++c) { colX[c] = x; x += colW[c] * scale + opt.viewGap; }
    double y = y0;
    for (int r = 0; r < nr; ++r) { rowY[r] = y; y += rowH[r] * scale + opt.viewGap; }

    for (DrawingView& v : sheet.views) {
        const auto [c, r] = cell(v.kind);
        const int ci = rank(cols, c), ri = rank(rows, r);
        v.scale = scale;
        // Hucre icinde ortala (Front/Top ayni X, Front/Side ayni Z genisligi paylasir).
        v.origin = {colX[ci] + (colW[ci] - v.bounds.width()) * 0.5 * scale,
                    rowY[ri] + (rowH[ri] - v.bounds.height()) * 0.5 * scale};
    }
    return true;
}

BuildResult buildSheet(const Document& doc, MarkRegistry& reg, EntityId subject,
                       const DrawingOptions& opt) {
    BuildResult res;
    markDocument(doc, reg);

    DrawingSheet sheet;
    std::string err;
    const std::vector<EntityId> parts = subjectParts(doc, subject, sheet.kind, err);
    if (parts.empty()) {
        res.error = err.empty() ? "cizilecek parca yok" : err;
        return res;
    }
    const auto subjectMark = reg.markOf(subject);
    if (!subjectMark) {
        res.error = "konu icin mark atanamadi";
        return res;
    }
    sheet.subject = subject;
    sheet.format = sheetFormat(opt.sheet);
    sheet.projection = opt.projection;
    sheet.id = 1;
    sheet.name = text::toUtf8(subjectMark->text());

    for (const ViewKind k : opt.views) {
        DrawingView v = generateView(doc, parts, subject, k, opt.hiddenLines);
        if (v.bounds.empty) continue;
        v.id = sheet.allocateId();
        sheet.views.push_back(std::move(v));
    }
    if (sheet.views.empty()) {
        res.error = "gorunus uretilemedi (geometri bos)";
        return res;
    }
    if (!placeViews(sheet, opt)) {
        res.error = "gorunusler kagit alanina yerlestirilemedi";
        return res;
    }

    // Ana parca: assembly ise mainPart, degilse konu.
    EntityId mainPart = parts.front();
    if (const auto ai = doc.indexOfAssembly(subject)) {
        const EntityId mp = doc.assemblies()[*ai].mainPart;
        if (std::find(parts.begin(), parts.end(), mp) != parts.end()) mainPart = mp;
    }
    const WireframeModel& mainModel = doc.models()[*doc.indexOfEntity(mainPart)];
    Vec3 axA{}, axB{};
    const bool hasAxis = axisOf(mainModel, axA, axB);
    const double axisLen = hasAxis ? dist3(axA, axB) : 0.0;

    for (DrawingView& v : sheet.views) {
        const Bounds2 pb = v.paperBounds();

        // --- mark etiketleri ---
        {
            MarkAnnotation m;
            m.id = sheet.allocateId();
            m.viewId = v.id;
            m.target = subject;
            m.text = text::toUtf8(subjectMark->text());
            m.isAssembly = subjectMark->kind == MarkKind::Assembly;
            m.anchor = v.bounds.centre();
            m.labelPos = {pb.min.x, pb.max.y + 8.0};
            sheet.marks.push_back(std::move(m));
        }
        if (sheet.kind == DrawingKind::Assembly) {
            std::set<int> doneParts;
            for (const ViewMember& vm : v.members) {
                const auto pm = reg.markOf(vm.entity);
                if (!pm || !doneParts.insert(pm->number).second) continue;
                MarkAnnotation m;
                m.id = sheet.allocateId();
                m.viewId = v.id;
                m.target = vm.entity;
                m.text = text::toUtf8(pm->text());
                m.isAssembly = false;
                m.anchor = vm.bounds.centre();
                const Vec2 tr = v.toPaper(vm.bounds.max);
                m.labelPos = {tr.x + 4.0, tr.y + 4.0};
                sheet.marks.push_back(std::move(m));
            }
        }

        // Etiketler birbirine binmesin: sirayla, cakisan etiketi yukari kaydir (deterministik).
        {
            std::vector<std::size_t> mine;
            for (std::size_t i = 0; i < sheet.marks.size(); ++i)
                if (sheet.marks[i].viewId == v.id) mine.push_back(i);
            for (std::size_t k = 1; k < mine.size(); ++k) {
                MarkAnnotation& cur = sheet.marks[mine[k]];
                for (int guard = 0; guard < 50; ++guard) {
                    const Bounds2 cb = markLabelBox(cur);
                    bool hit = false;
                    for (std::size_t j = 0; j < k && !hit; ++j) {
                        const Bounds2 ob = markLabelBox(sheet.marks[mine[j]]);
                        hit = cb.min.x < ob.max.x + 1.0 && ob.min.x < cb.max.x + 1.0 &&
                              cb.min.y < ob.max.y + 1.0 && ob.min.y < cb.max.y + 1.0;
                    }
                    if (!hit) break;
                    cur.labelPos.y += 3.0;
                }
            }
        }

        // --- olculer: gercek geometriye bagli ---
        const Vec2 rawMin = v.projectionOffset; // yerel = projectToView(P) - projectionOffset
        struct Pending { Dimension2D d; int group; };
        std::vector<Pending> pend;
        const auto newDim = [&](DimensionRole role, DimensionDirection dir, Vec2 p1, Vec2 p2, double value,
                                EntityId src, int group, double offset = 0.0) {
            Dimension2D d;
            d.id = sheet.allocateId();
            d.viewId = v.id;
            d.role = role;
            d.direction = dir;
            d.p1 = p1;
            d.p2 = p2;
            d.value = value;
            d.offset = offset;
            d.source = src;
            pend.push_back({d, group});
        };
        constexpr double kMinGap = 0.5;
        constexpr int kLengthGroup = 100, kOverallGroup = 101;

        // (1) Montaj uyelerinin kenar konumlari zinciri (yalniz assembly, >1 uye).
        if (sheet.kind == DrawingKind::Assembly && v.members.size() > 1) {
            std::vector<double> xs{v.bounds.min.x, v.bounds.max.x}, ys{v.bounds.min.y, v.bounds.max.y};
            for (const ViewMember& vm : v.members) {
                xs.push_back(vm.bounds.min.x); xs.push_back(vm.bounds.max.x);
                ys.push_back(vm.bounds.min.y); ys.push_back(vm.bounds.max.y);
            }
            xs = uniqueSorted(xs, kMinGap);
            ys = uniqueSorted(ys, kMinGap);
            if (xs.size() > 2)
                for (std::size_t i = 0; i + 1 < xs.size(); ++i)
                    newDim(DimensionRole::PartPosition, DimensionDirection::Horizontal, {xs[i], v.bounds.min.y},
                           {xs[i + 1], v.bounds.min.y}, xs[i + 1] - xs[i], subject, 0);
            if (ys.size() > 2)
                for (std::size_t i = 0; i + 1 < ys.size(); ++i)
                    newDim(DimensionRole::PartPosition, DimensionDirection::Vertical, {v.bounds.max.x, ys[i]},
                           {v.bounds.max.x, ys[i + 1]}, ys[i + 1] - ys[i], subject, 0);
        }

        // (2) Delik zincirleri (parca basina): kenar -> delik -> delik -> kenar, merkezler arasi.
        {
            std::vector<EntityId> owners;
            for (const Circle2& c : v.circles)
                if (std::find(owners.begin(), owners.end(), c.owner) == owners.end()) owners.push_back(c.owner);
            int group = 1;
            for (const EntityId owner : owners) {
                const ViewMember* pm = nullptr;
                for (const ViewMember& vm : v.members)
                    if (vm.entity == owner) pm = &vm;
                if (!pm) continue;
                std::vector<Vec2> centres;
                for (const Circle2& c : v.circles)
                    if (c.owner == owner) centres.push_back(c.center);
                std::vector<double> cx, cy;
                for (const Vec2& c : centres) { cx.push_back(c.x); cy.push_back(c.y); }
                cx = uniqueSorted(cx, 0.05);
                cy = uniqueSorted(cy, 0.05);
                const Bounds2& pb = pm->bounds;
                // Nokta + bagli oldugu ikinci koordinat (uzatma cizgisi nereden baslar).
                std::vector<std::pair<double, double>> hx, hy;
                hx.push_back({pb.min.x, pb.min.y});
                for (const double x : cx) {
                    double y = 1e300;
                    for (const Vec2& c : centres) if (std::fabs(c.x - x) <= 0.05) y = std::min(y, c.y);
                    hx.push_back({x, y});
                }
                hx.push_back({pb.max.x, pb.min.y});
                hy.push_back({pb.min.y, pb.max.x});
                for (const double y : cy) {
                    double x = -1e300;
                    for (const Vec2& c : centres) if (std::fabs(c.y - y) <= 0.05) x = std::max(x, c.x);
                    hy.push_back({y, x});
                }
                hy.push_back({pb.max.y, pb.max.x});
                for (std::size_t i = 0; i + 1 < hx.size(); ++i) {
                    const double gap = hx[i + 1].first - hx[i].first;
                    if (gap >= kMinGap)
                        newDim(DimensionRole::HoleSpacing, DimensionDirection::Horizontal, {hx[i].first, hx[i].second},
                               {hx[i + 1].first, hx[i + 1].second}, gap, owner, group);
                }
                for (std::size_t i = 0; i + 1 < hy.size(); ++i) {
                    const double gap = hy[i + 1].first - hy[i].first;
                    if (gap >= kMinGap)
                        newDim(DimensionRole::HoleSpacing, DimensionDirection::Vertical, {hy[i].second, hy[i].first},
                               {hy[i + 1].second, hy[i + 1].first}, gap, owner, group);
                }
                ++group;
            }
        }

        // (3) Uye boyu (ana parca ekseni, gorunus duzleminde ise) + genel olculer.
        bool haveLength = false;
        DimensionDirection lengthDir = DimensionDirection::Horizontal;
        if (hasAxis) {
            const Vec2 pa = projectToView(axA, v.kind), pb2 = projectToView(axB, v.kind);
            const double projLen = std::hypot(pb2.x - pa.x, pb2.y - pa.y);
            if (projLen >= axisLen * 0.999) { // eksen gorunus duzleminde: gercek boy olculur
                const double adx = std::fabs(pb2.x - pa.x), ady = std::fabs(pb2.y - pa.y);
                lengthDir = ady < projLen * 1e-3   ? DimensionDirection::Horizontal
                            : adx < projLen * 1e-3 ? DimensionDirection::Vertical
                                                   : DimensionDirection::Aligned;
                newDim(DimensionRole::MemberLength, lengthDir, {pa.x - rawMin.x, pa.y - rawMin.y},
                       {pb2.x - rawMin.x, pb2.y - rawMin.y}, axisLen, mainPart, kLengthGroup);
                haveLength = true;
            }
        }
        const auto sameAsLength = [&](DimensionDirection dir, double value) {
            return haveLength && lengthDir == dir && std::fabs(value - axisLen) < 0.5;
        };
        const double w = v.bounds.width(), h = v.bounds.height();
        if (w >= 0.5 && !sameAsLength(DimensionDirection::Horizontal, w))
            newDim(DimensionRole::OverallWidth, DimensionDirection::Horizontal, {v.bounds.min.x, v.bounds.min.y},
                   {v.bounds.max.x, v.bounds.min.y}, w, subject, kOverallGroup);
        if (h >= 0.5 && !sameAsLength(DimensionDirection::Vertical, h))
            newDim(DimensionRole::OverallHeight, DimensionDirection::Vertical, {v.bounds.max.x, v.bounds.min.y},
                   {v.bounds.max.x, v.bounds.max.y}, h, subject, kOverallGroup);

        // (4) Satir (offset) atamasi: ayni yondeki gruplar yakindan uzaga; metin kademesi.
        {
            int rowsH = 0, rowsV = 0;
            for (const auto dir : {DimensionDirection::Horizontal, DimensionDirection::Vertical}) {
                std::vector<int> groups;
                for (const Pending& p : pend)
                    if (p.d.direction == dir) groups.push_back(p.group);
                std::sort(groups.begin(), groups.end());
                groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
                (dir == DimensionDirection::Horizontal ? rowsH : rowsV) = static_cast<int>(groups.size());
                for (Pending& p : pend)
                    if (p.d.direction == dir) {
                        const auto row = std::find(groups.begin(), groups.end(), p.group) - groups.begin();
                        p.d.offset = 10.0 + 8.0 * static_cast<double>(row);
                    }
                // Dar aralikli zincir parcalarinda metni kademeli kaydir (ust uste binmesin).
                for (const int g : groups) {
                    std::vector<Pending*> chain;
                    for (Pending& p : pend)
                        if (p.d.direction == dir && p.group == g &&
                            (p.d.role == DimensionRole::HoleSpacing || p.d.role == DimensionRole::PartPosition))
                            chain.push_back(&p);
                    std::sort(chain.begin(), chain.end(), [&](const Pending* a, const Pending* b) {
                        return dir == DimensionDirection::Horizontal ? a->d.p1.x < b->d.p1.x : a->d.p1.y < b->d.p1.y;
                    });
                    int k = 0;
                    for (Pending* p : chain) {
                        const double textW = 2.0 * static_cast<double>(fmtNum(p->d.value).size()) + 1.5;
                        if (p->d.value * sheet.scale < textW) p->d.textLevel = 1 + (k++ % 2);
                        else k = 0;
                    }
                }
            }
            for (Pending& p : pend)
                if (p.d.direction == DimensionDirection::Aligned)
                    p.d.offset = 10.0 + 8.0 * static_cast<double>(std::max(rowsH, rowsV));
            std::stable_sort(pend.begin(), pend.end(), [](const Pending& a, const Pending& b) {
                return a.group < b.group;
            });
            for (Pending& p : pend) sheet.dimensions.push_back(std::move(p.d));
        }
    }

    // --- title block ---
    TitleBlock& tb = sheet.titleBlock;
    tb.project = opt.project;
    tb.drawnBy = opt.drawnBy;
    tb.date = opt.date;
    tb.drawingNumber = sheet.name;
    tb.title = std::string(sheet.kind == DrawingKind::Assembly ? "Assembly " : "Part ") + sheet.name;
    const std::string nm = deriveName(mainModel);
    if (!nm.empty()) tb.title += " - " + nm;

    res.sheet = std::move(sheet);
    res.ok = true;
    return res;
}

} // namespace mm::drawing
