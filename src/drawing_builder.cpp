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
        bool haveLength = false;
        DimensionDirection lengthDir = DimensionDirection::Horizontal;
        if (hasAxis) {
            const Vec2 pa = projectToView(axA, v.kind), pb2 = projectToView(axB, v.kind);
            const double projLen = std::hypot(pb2.x - pa.x, pb2.y - pa.y);
            if (projLen >= axisLen * 0.999) { // eksen gorunus duzleminde: gercek boy olculur
                Dimension2D d;
                d.id = sheet.allocateId();
                d.viewId = v.id;
                d.role = DimensionRole::MemberLength;
                const double adx = std::fabs(pb2.x - pa.x), ady = std::fabs(pb2.y - pa.y);
                d.direction = ady < projLen * 1e-3   ? DimensionDirection::Horizontal
                              : adx < projLen * 1e-3 ? DimensionDirection::Vertical
                                                     : DimensionDirection::Aligned;
                d.p1 = {pa.x - rawMin.x, pa.y - rawMin.y};
                d.p2 = {pb2.x - rawMin.x, pb2.y - rawMin.y};
                d.value = axisLen;
                d.offset = 24.0;
                d.source = mainPart;
                lengthDir = d.direction;
                haveLength = true;
                sheet.dimensions.push_back(std::move(d));
            }
        }
        const auto sameAsLength = [&](DimensionDirection dir, double value) {
            return haveLength && lengthDir == dir && std::fabs(value - axisLen) < 0.5;
        };
        const double w = v.bounds.width(), h = v.bounds.height();
        if (w >= 0.5 && !sameAsLength(DimensionDirection::Horizontal, w)) {
            Dimension2D d;
            d.id = sheet.allocateId();
            d.viewId = v.id;
            d.role = DimensionRole::OverallWidth;
            d.direction = DimensionDirection::Horizontal;
            d.p1 = {v.bounds.min.x, v.bounds.min.y};
            d.p2 = {v.bounds.max.x, v.bounds.min.y};
            d.value = w;
            d.offset = 12.0;
            d.source = subject;
            sheet.dimensions.push_back(std::move(d));
        }
        if (h >= 0.5 && !sameAsLength(DimensionDirection::Vertical, h)) {
            Dimension2D d;
            d.id = sheet.allocateId();
            d.viewId = v.id;
            d.role = DimensionRole::OverallHeight;
            d.direction = DimensionDirection::Vertical;
            d.p1 = {v.bounds.max.x, v.bounds.min.y};
            d.p2 = {v.bounds.max.x, v.bounds.max.y};
            d.value = h;
            d.offset = 12.0;
            d.source = subject;
            sheet.dimensions.push_back(std::move(d));
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
