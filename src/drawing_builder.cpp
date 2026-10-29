#include "model_maker/drawing_builder.hpp"

#include "model_maker/drawing_layout.hpp"
#include "model_maker/utf8.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <initializer_list>
#include <set>
#include <tuple>

namespace mm::drawing {
namespace {

const char* labelOf(ViewKind k) {
    switch (k) {
    case ViewKind::Front: return "FRONT";
    case ViewKind::Top: return "TOP";
    case ViewKind::Side: return "SIDE";
    case ViewKind::Detail: return "DETAIL";
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

// --- GIZLI CIZGI: GERCEK OKKLUZYON (view-space derinlik testi) ---
// Onceki surum yalniz "arka yuz" testiydi: bir parcanin kendi arka kenarlarini gizli sayiyor,
// ONUNDE duran baska bir parcayi (assembly'de levha/kiris) hic dikkate almiyor ve KISMI ortulmeyi
// (kenarin bir bolumu kapali) ifade edemiyordu. Yeni yontem:
//   * gorunuse giren TUM parcalarin yuzleri izdusurulur (2B ucgen + derinlik),
//   * kenar boyunca orneklenir; nokta KENDISINDEN DAHA YAKIN bir ucgenle ortuluyorsa gizli,
//   * gorunur<->gizli gecisleri ikili aramayla hassaslastirilir,
//   * kenar gorunur/gizli PARCALARA bolunur -> kismi gizli kenar dogru cizilir (kesik + duz).
namespace {
constexpr double kDepthEps = 0.05;   // mm: ayni yuzeydeki (ortak) kenar kendini ortmez

struct ViewTri {
    Vec2 a{}, b{}, c{};
    double da{0}, db{0}, dc{0};   // dot(p, normal): BUYUK = izleyiciye daha yakin
    Bounds2 box{};
};

// Barycentric ic testi + duzlem derinligi (ortak kenarlarda esit cikar -> eps ile elenir).
inline bool triContains(const ViewTri& t, Vec2 p, double& depth) {
    const double den = (t.b.y - t.c.y) * (t.a.x - t.c.x) + (t.c.x - t.b.x) * (t.a.y - t.c.y);
    if (std::fabs(den) < 1e-12) return false;
    const double w1 = ((t.b.y - t.c.y) * (p.x - t.c.x) + (t.c.x - t.b.x) * (p.y - t.c.y)) / den;
    const double w2 = ((t.c.y - t.a.y) * (p.x - t.c.x) + (t.a.x - t.c.x) * (p.y - t.c.y)) / den;
    const double w3 = 1.0 - w1 - w2;
    constexpr double edgeEps = -1e-9;
    if (w1 < edgeEps || w2 < edgeEps || w3 < edgeEps) return false;
    depth = w1 * t.da + w2 * t.db + w3 * t.dc;
    return true;
}

void collectViewTriangles(const WireframeModel& m, const ViewFrame& fr, std::vector<ViewTri>& out) {
    const auto& verts = m.vertices();
    if (verts.empty()) return;
    for (const Face& f : m.faces()) {
        if (f.size() < 3) continue;
        for (std::size_t i = 1; i + 1 < f.size(); ++i) {
            if (f[0] >= verts.size() || f[i] >= verts.size() || f[i + 1] >= verts.size()) continue;
            const Vec3& p0 = verts[f[0]];
            const Vec3& p1 = verts[f[i]];
            const Vec3& p2 = verts[f[i + 1]];
            ViewTri t;
            t.a = fr.project(p0);
            t.b = fr.project(p1);
            t.c = fr.project(p2);
            const double area = (t.b.x - t.a.x) * (t.c.y - t.a.y) - (t.c.x - t.a.x) * (t.b.y - t.a.y);
            if (std::fabs(area) < 1e-9) continue;   // gorunuse dik yuz: izdusumde cizgi
            t.da = p0.x * fr.normal.x + p0.y * fr.normal.y + p0.z * fr.normal.z;
            t.db = p1.x * fr.normal.x + p1.y * fr.normal.y + p1.z * fr.normal.z;
            t.dc = p2.x * fr.normal.x + p2.y * fr.normal.y + p2.z * fr.normal.z;
            t.box.include(t.a);
            t.box.include(t.b);
            t.box.include(t.c);
            out.push_back(t);
        }
    }
}

struct HiddenRun {
    double t0{0.0}, t1{1.0};
    bool hidden{false};
};

// q noktasi, verilen duzlem derinliginden DAHA YAKIN bir yuzeyle ortuluyor mu?
inline bool pointOccluded(const std::vector<ViewTri>& tris, Vec2 q, double depth) {
    for (const ViewTri& tr : tris) {
        if (q.x < tr.box.min.x || q.x > tr.box.max.x || q.y < tr.box.min.y || q.y > tr.box.max.y) continue;
        double td = 0.0;
        if (!triContains(tr, q, td)) continue;
        if (td > depth + kDepthEps) return true;   // daha yakin yuzey: bu nokta kapali
    }
    return false;
}

// 3B kenar p0->p1 icin gorunurluk parcalari (t: 0..1 kenar parametresi).
std::vector<HiddenRun> classifyHiddenRuns(const std::vector<ViewTri>& tris, const ViewFrame& fr,
                                         const Vec3& p0, const Vec3& p1) {
    std::vector<HiddenRun> runs;
    const Vec2 q0 = fr.project(p0), q1 = fr.project(p1);
    const double len = std::hypot(q1.x - q0.x, q1.y - q0.y);
    if (len < 1e-6) return runs;
    const double d0 = p0.x * fr.normal.x + p0.y * fr.normal.y + p0.z * fr.normal.z;
    const double d1 = p1.x * fr.normal.x + p1.y * fr.normal.y + p1.z * fr.normal.z;
    const auto hiddenAt = [&](double t) {
        const Vec2 q{q0.x + (q1.x - q0.x) * t, q0.y + (q1.y - q0.y) * t};
        return pointOccluded(tris, q, d0 + (d1 - d0) * t);
    };
    const int samples = std::clamp(static_cast<int>(std::ceil(len / 2.0)), 8, 256);   // ~2 mm
    bool prevHidden = hiddenAt(0.0);
    double runStart = 0.0;
    for (int i = 1; i <= samples; ++i) {
        const double t = static_cast<double>(i) / samples;
        const bool h = hiddenAt(t);
        if (h == prevHidden) continue;
        // Gecis: [onceki, t] araliginda ikili arama ile sinir (8 adim ~ %0.4 hassasiyet)
        double lo = static_cast<double>(i - 1) / samples, hi = t;
        for (int k = 0; k < 8; ++k) {
            const double mid = 0.5 * (lo + hi);
            if (hiddenAt(mid) == prevHidden) lo = mid;
            else hi = mid;
        }
        const double cut = 0.5 * (lo + hi);
        runs.push_back({runStart, cut, prevHidden});
        runStart = cut;
        prevHidden = h;
    }
    runs.push_back({runStart, 1.0, prevHidden});
    return runs;
}
} // namespace


std::string fmtNum(double v) {
    // MM HASSASIYETI (kullanici kurali): olcu metni YALNIZ tam mm yazar (3591.3 -> 3591).
    // Alt-mm basamak gosterilmez; deger yuvarlanarak yazilir (tek yerde kural).
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0f", std::round(v));
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
    case ViewKind::Detail: return {p.x, p.y};   // detay kendi cercevesini tasir; dunya yedegi
    }
    return {p.x, p.z};
}

Bounds2 markLabelBox(const MarkAnnotation& m) noexcept {
    // GUI cizimiyle AYNI formul: kutu genisligi = max(metin + 3, h + 3), yukseklik = h + 2.
    const double h = m.isAssembly ? 5.0 : 3.5;
    const double w = std::max(textWidthEstimate(m.text, h, m.isAssembly) + 3.0, h + 3.0);
    Bounds2 b;
    b.include(m.labelPos);
    b.include({m.labelPos.x + w, m.labelPos.y + h + 2.0});
    return b;
}

// Poz numarasi etiketi: "P11/PL10*100" (mark / profil). Part mark'inda parcanin
// KENDI profili, assembly mark'inda ana parcanin profili kullanilir. Profil yoksa
// yalniz mark yazilir (eski davranis korunur).
std::string markLabelText(const MarkRegistry& reg, const Mark& mark, const std::wstring& mainPartProfile) {
    std::wstring profile;
    if (mark.kind == MarkKind::Part) {
        if (const PartSignature* sig = reg.partSignatureFor(mark)) profile = sig->profile;
    } else {
        profile = mainPartProfile;
    }
    if (profile.empty()) return text::toUtf8(mark.text());
    return text::toUtf8(mark.text() + L"/" + profile);
}

ViewFrame frameFor(ViewKind kind, const fab::MemberDatum* datum, bool memberAligned) noexcept {
    ViewFrame f;   // dunya: Front (x,z), Top (x,y), Side (y,z)
    const Vec3 X{1, 0, 0}, Y{0, 1, 0}, Z{0, 0, 1};
    Vec3 x = X, y = Y, z = Z;
    if (datum && datum->valid && memberAligned) {
        const Vec3 a{std::fabs(datum->x.x), std::fabs(datum->x.y), std::fabs(datum->x.z)};
        const bool worldAligned = std::max({a.x, a.y, a.z}) > 1.0 - 1e-9;
        if (!worldAligned) {
            x = datum->x;
            y = datum->y;
            z = datum->z;
            f.memberAligned = true;
        }
    }
    const auto neg = [](const Vec3& v) { return Vec3{-v.x, -v.y, -v.z}; };
    switch (kind) {
    case ViewKind::Front: f.right = x; f.up = z; f.normal = neg(y); break;
    case ViewKind::Top: f.right = x; f.up = y; f.normal = z; break;
    case ViewKind::Side: f.right = y; f.up = z; f.normal = x; break;
    case ViewKind::Detail: f.right = x; f.up = y; f.normal = z; break;   // yedek; gercek detay cercevesi cagirandan gelir
    }
    return f;
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
        PartSignature sig = partSignatureOf(models[i]);
        sig.drillHash = fab::drillingFingerprint(doc, doc.modelId(i));   // P2A: delik/kesme imzaya katilir
        reg.markPart(doc.modelId(i), sig);
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
                         ViewKind kind, bool hiddenLines, const ViewFrame* framePtr) {
    DrawingView view;
    const ViewFrame fr = framePtr ? *framePtr : frameFor(kind, nullptr);
    view.frame = fr;
    const auto proj = [&](const Vec3& p) { return fr.project(p); };
    view.kind = kind;
    view.subject = subject;
    view.label = labelOf(kind);

    struct Raw { Vec2 a, b; LineStyle style; };
    std::vector<Raw> raw;
    std::vector<std::pair<EntityId, Bounds2>> memberRaw;
    Bounds2 rawBounds;

    // Gizli cizgi: gorunuse giren TUM parcalarin yuzleri (assembly'de parcalar birbirini orter).
    std::vector<ViewTri> tris;
    if (hiddenLines)
        for (const EntityId id : parts) {
            const auto idx = doc.indexOfEntity(id);
            if (idx) collectViewTriangles(doc.models()[*idx], fr, tris);
        }

    for (const EntityId id : parts) {
        const auto idx = doc.indexOfEntity(id);
        if (!idx) continue;
        const WireframeModel& m = doc.models()[*idx];
        const auto& v = m.vertices();
        Bounds2 mb;
        for (const Vec3& p : v) mb.include(proj(p));
        for (const Edge& e : m.edges()) {
            if (e.from >= v.size() || e.to >= v.size()) continue;
            const Vec2 a = proj(v[e.from]);
            const Vec2 b = proj(v[e.to]);
            if (std::hypot(b.x - a.x, b.y - a.y) < 1e-6) continue; // goruntude nokta
            if (!hiddenLines || tris.empty()) {
                raw.push_back({a, b, LineStyle::Visible});
                rawBounds.include(a);
                rawBounds.include(b);
                continue;
            }
            // Kismi ortulme: kenar gorunur/gizli parcalara bolunur.
            for (const HiddenRun& run : classifyHiddenRuns(tris, fr, v[e.from], v[e.to])) {
                if (run.t1 - run.t0 < 1e-9) continue;
                const Vec2 ra{a.x + (b.x - a.x) * run.t0, a.y + (b.y - a.y) * run.t0};
                const Vec2 rb{a.x + (b.x - a.x) * run.t1, a.y + (b.y - a.y) * run.t1};
                if (std::hypot(rb.x - ra.x, rb.y - ra.y) < 1e-6) continue;
                raw.push_back({ra, rb, run.hidden ? LineStyle::Hidden : LineStyle::Visible});
                rawBounds.include(ra);
                rawBounds.include(rb);
            }
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

    // AYNI IZDUSUM CIZGISI UZERINDE GORUNUR PARCA GIZLIYI EZER.
    // Arka kenar (gizli) ile on kenar (gorunur) ayni cizgiye dusebilir; ikisini birden cizmek
    // cizgiyi kesik+girift yapar ve toplam uzunlugu sisirir. Kural: gizli parcalar, ayni cizgi
    // uzerindeki GORUNUR parcalarin kapladigi araliklardan CIKARILIR; kalan araliklar kesik cizilir.
    if (hiddenLines) {
        std::vector<Raw> resolved;
        resolved.reserve(unique.size());
        for (const Raw& r : unique)
            if (r.style == LineStyle::Visible) resolved.push_back(r);
        for (const Raw& r : unique) {
            if (r.style != LineStyle::Hidden) continue;
            double dx = r.b.x - r.a.x, dy = r.b.y - r.a.y;
            const double len = std::hypot(dx, dy);
            if (len < 1e-9) continue;
            dx /= len;
            dy /= len;
            std::vector<std::pair<double, double>> cuts;   // ayni cizgideki gorunur araliklar
            for (const Raw& v : unique) {
                if (v.style != LineStyle::Visible) continue;
                const double crossAngle = dx * (v.b.y - v.a.y) - dy * (v.b.x - v.a.x);
                if (std::fabs(crossAngle) > 0.02 * len) continue;   // yon farkli: ayni cizgi degil
                const double off1 = -dy * (v.a.x - r.a.x) + dx * (v.a.y - r.a.y);
                const double off2 = -dy * (v.b.x - r.a.x) + dx * (v.b.y - r.a.y);
                if (std::fabs(off1) > 0.05 || std::fabs(off2) > 0.05) continue;   // ayri paralel cizgi
                double t0 = dx * (v.a.x - r.a.x) + dy * (v.a.y - r.a.y);
                double t1 = dx * (v.b.x - r.a.x) + dy * (v.b.y - r.a.y);
                if (t0 > t1) std::swap(t0, t1);
                cuts.push_back({t0, t1});
            }
            std::sort(cuts.begin(), cuts.end());
            const auto emit = [&](double s, double e) {
                if (e - s <= 0.05) return;
                resolved.push_back({{r.a.x + dx * s, r.a.y + dy * s}, {r.a.x + dx * e, r.a.y + dy * e},
                                    LineStyle::Hidden});
            };
            double cursor = 0.0;
            for (const auto& [s, e] : cuts) {
                if (cursor >= len) break;
                if (s > cursor) emit(cursor, std::min(s, len));
                cursor = std::max(cursor, e);
            }
            if (cursor < len) emit(cursor, len);
        }
        // Cikarma sonrasi birebir ayni kesik parcalar tekrarlanmasin.
        std::map<Key, std::size_t> seenHidden;
        std::vector<Raw> compact;
        for (const Raw& r : resolved) {
            const Key k = keyOf(r.a, r.b);
            if (seenHidden.emplace(k, compact.size()).second) compact.push_back(r);
        }
        unique = std::move(compact);
    }

    const Vec2 lo = rawBounds.min;
    view.projectionOffset = lo;
    const auto local = [&](const Vec2& p) { return Vec2{p.x - lo.x, p.y - lo.y}; };
    for (const Raw& r : unique) {
        // 0.05 mm altindaki kil (sliver) parcalar cizime girmez: kesme/bisection artiklari.
        if (std::hypot(r.b.x - r.a.x, r.b.y - r.a.y) < 0.05) continue;
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
        const Vec3 n = fr.normal;
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
                const Vec2 c = local(proj(hm.planeOrigin));
                if (d >= 0.999) {
                    Circle2 ci;
                    ci.center = c;
                    ci.radius = hm.diameter / 2.0;
                    ci.owner = id;
                    // Kapali (arka) yuzdeki delik KESIK daire: merkez + 4 kadran orneklenir;
                    // hepsi daha yakin bir yuzeyle ortuluyorsa gizli.
                    if (!tris.empty()) {
                        const Vec2 cg = proj(hm.planeOrigin);
                        const double depth = hm.planeOrigin.x * n.x + hm.planeOrigin.y * n.y + hm.planeOrigin.z * n.z;
                        const double r = ci.radius;
                        int occluded = 0, probes = 0;
                        for (const Vec2 off : {Vec2{0.0, 0.0}, Vec2{r, 0.0}, Vec2{-r, 0.0}, Vec2{0.0, r}, Vec2{0.0, -r}}) {
                            ++probes;
                            if (pointOccluded(tris, {cg.x + off.x, cg.y + off.y}, depth)) ++occluded;
                        }
                        if (occluded == probes) ci.style = LineStyle::Hidden;
                    }
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
                        return local(proj({hm.planeOrigin.x + ax.x * t, hm.planeOrigin.y + ax.y * t,
                                           hm.planeOrigin.z + ax.z * t}));
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
        case ViewKind::Detail: return std::pair{2, 0};
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

// ================================================================ P2A: imalat olculeri + yerlesim
namespace {

constexpr double kMinGap = 0.5;

// 3B nokta -> gorunum-yerel (projectionOffset cikarilmis).
Vec2 localOf(const DrawingView& v, const Vec3& p) {
    const Vec2 q = v.frame.project(p);
    return {q.x - v.projectionOffset.x, q.y - v.projectionOffset.y};
}

// Eksen gorunum duzleminde mi (gercek uzunluk)? dx/dy: gorunumdeki izdusum bilesenleri.
struct AxisInView {
    bool inPlane{false};
    double px{0}, py{0};   // gorunum (right, up) bilesenleri (birim vektor icin)
    DimensionDirection dir{DimensionDirection::Aligned};
};
AxisInView axisInView(const DrawingView& v, const Vec3& unitAxis) {
    AxisInView r;
    const double px = unitAxis.x * v.frame.right.x + unitAxis.y * v.frame.right.y + unitAxis.z * v.frame.right.z;
    const double py = unitAxis.x * v.frame.up.x + unitAxis.y * v.frame.up.y + unitAxis.z * v.frame.up.z;
    r.px = px;
    r.py = py;
    r.inPlane = std::hypot(px, py) >= 1.0 - 1e-6;   // gercek uzunluk: eksen gorunum duzleminde
    if (std::fabs(py) < 1e-6) r.dir = DimensionDirection::Horizontal;
    else if (std::fabs(px) < 1e-6) r.dir = DimensionDirection::Vertical;
    return r;
}

std::string fmt1(double v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.1f", v);
    return b;
}

} // namespace

void evaluateFabricationStatus(DrawingSheet& sheet, const fab::FabricationInventory& inv, bool layoutOk) {
    FabricationStatus& st = sheet.fabrication;
    st.evaluated = true;
    st.checklist.clear();
    st.unresolvedCodes.clear();
    const auto item = [&](const char* key, const char* label, ChecklistState state, std::string detail,
                          std::string fk = {}) {
        st.checklist.push_back({key, label, state, std::move(detail), std::move(fk)});
    };
    const auto countDims = [&](DimensionRole r, const std::string& fk) {
        int n = 0;
        for (const Dimension2D& d : sheet.dimensions)
            if (d.role == r && (fk.empty() || d.featureKey == fk)) ++n;
        return n;
    };
    const auto hasText = [&](TextKind k, const std::string& fk) {
        for (const TextAnnotation& t : sheet.texts)
            if (t.kind == k && t.featureKey == fk) return true;
        return false;
    };
    const auto codeDetail = [&](std::initializer_list<const char*> codes) {
        std::string out;
        for (const fab::Unresolved& u : inv.unresolved)
            for (const char* c : codes)
                if (u.code == c) out += (out.empty() ? "" : "; ") + u.code + ": " + u.detail;
        return out;
    };
    const auto anyOf = [&](std::initializer_list<const char*> codes) {
        for (const fab::Unresolved& u : inv.unresolved)
            for (const char* c : codes)
                if (u.code == c) return true;
        return false;
    };

    // Mark'lar
    item("PART_MARK", "Parca mark'i", sheet.marks.empty() ? ChecklistState::Unresolved : ChecklistState::Satisfied,
         sheet.marks.empty() ? "mark etiketi yok" : "");
    item("ASSEMBLY_MARK", "Assembly mark'i",
         sheet.kind == DrawingKind::Assembly
             ? (std::any_of(sheet.marks.begin(), sheet.marks.end(), [](const MarkAnnotation& m) { return m.isAssembly; })
                    ? ChecklistState::Satisfied
                    : ChecklistState::Unresolved)
             : ChecklistState::NotApplicable,
         "");

    // Uye datumu
    const bool needDatum = std::any_of(inv.plates.begin(), inv.plates.end(), [&](const fab::PlateFeature& p) {
        return p.plate != inv.mainPart;
    });
    if (needDatum)
        item("MEMBER_DATUM", "Uye baslangic datumu ve yerel eksenler",
             inv.datum.valid && inv.datum.rollResolved ? ChecklistState::Satisfied : ChecklistState::Unresolved,
             inv.datum.valid ? (inv.datum.rollResolved ? inv.datum.orientationBasis : codeDetail({"MEMBER_ROLL_UNRESOLVED"}))
                             : codeDetail({"MEMBER_AXIS_MISSING"}),
             inv.datum.id);

    // Levhalar
    if (inv.plates.empty()) {
        item("PLATE_OUTLINE", "Levha olculeri (boy/en)", ChecklistState::NotApplicable, "cizimde kayitli cerceveli levha yok");
    }
    for (const fab::PlateFeature& p : inv.plates) {
        const int w = countDims(DimensionRole::PlateWidth, p.id), h = countDims(DimensionRole::PlateHeight, p.id);
        const bool ok = w >= 1 && h >= 1 && p.outlineExact;
        item("PLATE_OUTLINE", "Levha olculeri (boy/en)", ok ? ChecklistState::Satisfied : ChecklistState::Unresolved,
             ok ? "" : (!p.outlineExact ? "yay pah yaklasik" : "levha yuzunu gosteren gercek-uzunluk gorunumu yok"), p.id);
        item("PLATE_THICKNESS", "Levha kalinligi", hasText(TextKind::PlateNote, p.id) ? ChecklistState::Satisfied : ChecklistState::Unresolved,
             hasText(TextKind::PlateNote, p.id) ? "PL " + fmt1(p.thickness) + " notu" : "kalinlik notu uretilemedi", p.id);
        if (p.plate != inv.mainPart) {
            bool satisfied = inv.datum.valid;
            std::string missing;
            if (inv.datum.valid) {
                const DimensionRole rr[3] = {DimensionRole::PlateOffsetX, DimensionRole::PlateOffsetY, DimensionRole::PlateOffsetZ};
                static const char* an[3] = {"x'", "y'", "z'"};
                for (int k = 0; k < 3; ++k) {
                    const double ref = p.offsetRef(k);
                    if (std::fabs(ref) > 0.5 && countDims(rr[k], p.id) == 0) {
                        satisfied = false;
                        missing += std::string(an[k]) + " ";
                    }
                }
            }
            item("PLATE_OFFSET", "Levha ofseti (uye baslangicindan)",
                 satisfied ? ChecklistState::Satisfied : ChecklistState::Unresolved,
                 satisfied ? "" : (inv.datum.valid ? "gercek-uzunluk gorunumu yok: " + missing : codeDetail({"MEMBER_AXIS_MISSING"})),
                 p.id);
        }
    }
    // Delikler
    if (inv.groups.empty() && !anyOf({"HOLE_FRAME_UNAVAILABLE", "HOLE_OWNER_UNKNOWN", "HOLE_NOT_NORMAL_TO_PLATE",
                                      "HOLE_PARTIAL_DEPTH", "HOLE_OUTSIDE_PLATE"})) {
        item("HOLES", "Delik capi/adedi/konumu/adimlari/kenar mesafeleri", ChecklistState::NotApplicable, "delik yok");
    }
    for (const fab::HoleGroupFeature& g : inv.groups) {
        const bool callout = hasText(TextKind::HoleCallout, g.id);
        item("HOLE_DIAMETER_COUNT", "Delik capi ve adedi", callout ? ChecklistState::Satisfied : ChecklistState::Unresolved,
             callout ? "" : "callout uretilemedi", g.id);
        // u adimlari: ayni levhada ayni sutun imzali gruplardan biri tasir; v adimlari ve net kenarlar grubun kendi anahtarinda.
        int vPitch = 0, uPitch = 0, edge = 0, ord = 0;
        for (const Dimension2D& d : sheet.dimensions) {
            if (d.role == DimensionRole::HolePitch && d.basis.find("along plate u") != std::string::npos) {
                for (const fab::HoleGroupFeature& o : inv.groups)
                    if (o.id == d.featureKey && o.plate == g.plate && o.colU.size() == g.colU.size() &&
                        std::fabs(o.diameter - g.diameter) < 1e-6) {
                        bool same = true;
                        for (std::size_t i = 0; i < g.colU.size(); ++i) same = same && std::fabs(o.colU[i] - g.colU[i]) < 0.05;
                        if (same) { ++uPitch; break; }
                    }
            }
            if (d.featureKey != g.id) continue;
            if (d.role == DimensionRole::HolePitch && d.basis.find("along plate v") != std::string::npos) ++vPitch;
            if (d.role == DimensionRole::HoleEdge) ++edge;
            if (d.role == DimensionRole::HoleOrdinate) ++ord;
        }
        // P2D: duzgun dizilim -> adim + net kenar; duzensiz/manual dizilim -> her delik icin
        // iki GERCEK merkez koordinati (u,v). Ikisi de kaynak geometriden; uydurma adim yok.
        const int neededOrd = static_cast<int>(2 * g.holes.size());
        const bool posOk = g.shape == fab::HoleShape::Round &&
                           (g.regular ? (uPitch >= g.cols - 1 && vPitch >= g.rows - 1 && edge >= 1) : ord >= neededOrd);
        item("HOLE_POSITIONS", "Delik merkezleri / adimlar / net kenar mesafeleri",
             posOk ? ChecklistState::Satisfied : ChecklistState::Unresolved,
             posOk ? "" : (g.shape == fab::HoleShape::Slot
                               ? "yuvali delik: kontur/olcu desteklenmiyor"
                               : (g.regular ? "gercek-uzunluk gorunumu yok ya da adim/kenar olcusu uretilemedi"
                                            : "duzensiz dizilimde delik merkez koordinatlari uretilemedi")),
             g.id);
    }
    // HOLE_PATTERN_IRREGULAR artik TASLAK nedeni DEGIL: duzensiz dizilim gercek merkez koordinatlariyla
    // olculuyor (HOLE_POSITIONS). Yuva/kontur ve cerceve sorunlari TASLAK kalir.
    if (anyOf({"HOLE_FRAME_UNAVAILABLE", "HOLE_OWNER_UNKNOWN", "HOLE_NOT_NORMAL_TO_PLATE", "HOLE_PARTIAL_DEPTH",
               "HOLE_OUTSIDE_PLATE", "SLOT_NOT_DRAWN", "HOLE_BREAKS_EDGE"}))
        item("HOLES_UNRESOLVED", "Cozumlenemeyen delik/yuva", ChecklistState::Unresolved,
             codeDetail({"HOLE_FRAME_UNAVAILABLE", "HOLE_OWNER_UNKNOWN", "HOLE_NOT_NORMAL_TO_PLATE", "HOLE_PARTIAL_DEPTH",
                         "HOLE_OUTSIDE_PLATE", "SLOT_NOT_DRAWN", "HOLE_BREAKS_EDGE"}));
    // Kesim / kaynak
    item("CUTS", "Kesim/kopya olculeri",
         anyOf({"CUT_NOT_DIMENSIONED"}) ? ChecklistState::Unresolved : ChecklistState::NotApplicable,
         codeDetail({"CUT_NOT_DIMENSIONED"}));
    item("WELDS", "Kaynak sembolleri/olculeri",
         anyOf({"WELD_NOT_DIMENSIONED"}) ? ChecklistState::Unresolved : ChecklistState::NotApplicable,
         codeDetail({"WELD_NOT_DIMENSIONED"}));
    if (anyOf({"PLATE_FRAME_STALE", "PLATE_SPEC_INVALID", "PLATE_OUTLINE_APPROX"}))
        item("PLATE_FRAME", "Levha cercevesi dogrulugu", ChecklistState::Unresolved,
             codeDetail({"PLATE_FRAME_STALE", "PLATE_SPEC_INVALID", "PLATE_OUTLINE_APPROX"}));
    // Yerlesim
    item("LAYOUT", "Cakismasiz sayfa yerlesimi", layoutOk ? ChecklistState::Satisfied : ChecklistState::Unresolved,
         layoutOk ? "" : (sheet.layout.issues.empty() ? "yerlesim calistirilmadi" : sheet.layout.issues.front().code));

    for (const fab::Unresolved& u : inv.unresolved) st.unresolvedCodes.push_back(u.code);
    std::sort(st.unresolvedCodes.begin(), st.unresolvedCodes.end());
    st.unresolvedCodes.erase(std::unique(st.unresolvedCodes.begin(), st.unresolvedCodes.end()), st.unresolvedCodes.end());
    st.draft = false;
    for (const ChecklistItem& c : st.checklist)
        if (c.state == ChecklistState::Unresolved) st.draft = true;
}

namespace {

// Sayfa duzeyi metin (damga/legend/checklist).
TextAnnotation& addText(DrawingSheet& sheet, TextKind kind, std::vector<std::string> lines, double height) {
    TextAnnotation t;
    t.id = sheet.allocateId();
    t.kind = kind;
    t.lines = std::move(lines);
    t.height = height;
    sheet.texts.push_back(std::move(t));
    return sheet.texts.back();
}

} // namespace

BuildResult buildSheet(const Document& doc, MarkRegistry& reg, EntityId subject, const DrawingOptions& opt) {
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
    sheet.layoutConfig = opt.layout;
    sheet.documentRevision = doc.revision();

    // Ana parca: assembly ise mainPart, degilse konu.
    EntityId mainPart = parts.front();
    if (const auto ai = doc.indexOfAssembly(subject)) {
        const EntityId mp = doc.assemblies()[*ai].mainPart;
        if (std::find(parts.begin(), parts.end(), mp) != parts.end()) mainPart = mp;
    }
    const WireframeModel& mainModel = doc.models()[*doc.indexOfEntity(mainPart)];
    const std::wstring mainPartProfile = text::fromUtf8(mainModel.properties().profileName);
    Vec3 axA{}, axB{};
    const bool hasAxis = axisOf(mainModel, axA, axB);
    const double axisLen = hasAxis ? dist3(axA, axB) : 0.0;

    // Imalat envanteri (Document'tan; cizimden bagimsiz).
    fab::FabricationInventory inv = fab::extractFabrication(doc, parts, mainPart);

    for (const ViewKind k : opt.views) {
        const ViewFrame fr = frameFor(k, &inv.datum, opt.memberAlignedViews);
        DrawingView v = generateView(doc, parts, subject, k, opt.hiddenLines, &fr);
        if (v.bounds.empty) continue;
        v.id = sheet.allocateId();
        sheet.views.push_back(std::move(v));
    }
    if (sheet.views.empty()) {
        res.error = "gorunus uretilemedi (geometri bos)";
        return res;
    }
    // P2A: assembly'de delikli levhalar icin DETAY gorunumu (levha yuzu, kendi olcegi). Delik/levha olculeri burada.
    std::set<EntityId> detailPlates;
    if (opt.fabrication && opt.detailViews && (sheet.kind == DrawingKind::Assembly || parts.size() > 1)) {
        for (const fab::PlateFeature& P : inv.plates) {
            bool hasHoles = false;
            for (const fab::HoleGroupFeature& g : inv.groups) hasHoles = hasHoles || g.plate == P.plate;
            if (!hasHoles || P.plate == mainPart) continue;
            ViewFrame fr;
            fr.right = P.u;
            fr.up = P.v;
            fr.normal = {P.u.y * P.v.z - P.u.z * P.v.y, P.u.z * P.v.x - P.u.x * P.v.z, P.u.x * P.v.y - P.u.y * P.v.x};
            DrawingView dv = generateView(doc, {P.plate}, P.plate, ViewKind::Detail, false, &fr);
            if (dv.bounds.empty) continue;
            dv.id = sheet.allocateId();
            dv.detailPlate = P.plate;
            const auto pm = reg.markOf(P.plate);
            dv.label = "DETAIL" + (pm ? " " + text::toUtf8(pm->text()) : std::string());
            detailPlates.insert(P.plate);
            sheet.views.push_back(std::move(dv));
        }
    }
    if (!placeViews(sheet, opt)) {
        res.error = "gorunusler kagit alanina yerlestirilemedi";
        return res;
    }

    // Cozumlenmis levha-delik sahipleri: P1 yedek zinciri bunlar icin URETILMEZ (yinelenen zincir yok).
    std::set<EntityId> fabOwners;
    for (const fab::HoleGroupFeature& g : inv.groups) fabOwners.insert(g.plate);

    // Olcu birikimi (gorunum bazinda yerel liste; sonra sheet.dimensions'a).
    struct Pending { Dimension2D d; int group; };
    std::vector<Pending> pend;
    const auto sameDim = [](const Dimension2D& a, const Dimension2D& b) {
        const auto close = [](const Vec2& p, const Vec2& q) { return std::fabs(p.x - q.x) < 0.01 && std::fabs(p.y - q.y) < 0.01; };
        return a.viewId == b.viewId && a.direction == b.direction &&
               ((close(a.p1, b.p1) && close(a.p2, b.p2)) || (close(a.p1, b.p2) && close(a.p2, b.p1)));
    };
    const auto isFabRole = [](DimensionRole r) {
        return r == DimensionRole::PlateWidth || r == DimensionRole::PlateHeight || r == DimensionRole::HolePitch ||
               r == DimensionRole::HoleEdge || r == DimensionRole::HoleOrdinate ||
               r == DimensionRole::PlateOffsetX || r == DimensionRole::PlateOffsetY ||
               r == DimensionRole::PlateOffsetZ;
    };

    for (DrawingView& v : sheet.views) {
        const Bounds2 pb = v.paperBounds();

        // --- mark etiketleri ---
        if (v.kind == ViewKind::Detail) {
            if (const auto pm = reg.markOf(v.detailPlate)) {
                MarkAnnotation m;
                m.id = sheet.allocateId();
                m.viewId = v.id;
                m.target = v.detailPlate;
                m.text = markLabelText(reg, *pm, mainPartProfile);
                m.isAssembly = false;
                m.anchor = v.bounds.centre();
                m.labelPos = {pb.min.x, pb.max.y + 8.0};
                sheet.marks.push_back(std::move(m));
            }
        } else {
            MarkAnnotation m;
            m.id = sheet.allocateId();
            m.viewId = v.id;
            m.target = subject;
            m.text = markLabelText(reg, *subjectMark, mainPartProfile);
            m.isAssembly = subjectMark->kind == MarkKind::Assembly;
            m.anchor = v.bounds.centre();
            m.labelPos = {pb.min.x, pb.max.y + 8.0};
            sheet.marks.push_back(std::move(m));
        }
        if (sheet.kind == DrawingKind::Assembly && v.kind != ViewKind::Detail) {
            std::set<int> doneParts;
            for (const ViewMember& vm : v.members) {
                const auto pm = reg.markOf(vm.entity);
                if (!pm || !doneParts.insert(pm->number).second) continue;
                MarkAnnotation m;
                m.id = sheet.allocateId();
                m.viewId = v.id;
                m.target = vm.entity;
                m.text = markLabelText(reg, *pm, mainPartProfile);
                m.isAssembly = false;
                m.anchor = vm.bounds.centre();
                const Vec2 tr = v.toPaper(vm.bounds.max);
                m.labelPos = {tr.x + 4.0, tr.y + 4.0};
                sheet.marks.push_back(std::move(m));
            }
        }
        // P1 yedek yerlesimi (autoLayout kapaliyken): cakisan etiketi yukari kaydir.
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

        // --- gorunum basligi (yerlesim bunu da engel sayar) ---
        {
            TextAnnotation& t = addText(sheet, TextKind::ViewTitle, {v.label + "  " + sheet.titleBlock.scaleText},
                                        opt.layout.noteTextHeight);
            t.viewId = v.id;
        }

        const auto newDim = [&](DimensionRole role, DimensionDirection dir, Vec2 p1, Vec2 p2, double value,
                                EntityId src, int group, std::string fkey = {}, std::string basis = {},
                                int side = 1) {
            if (isFabRole(role) && value < kMinGap) return;   // sifir/negatif imalat olcusu uretilmez (Unresolved ayrica raporlanir)
            Dimension2D d;
            d.id = 0;
            d.viewId = v.id;
            d.role = role;
            d.direction = dir;
            d.p1 = p1;
            d.p2 = p2;
            d.value = value;
            d.offset = 0.0;
            d.side = side;
            d.source = src;
            d.featureKey = std::move(fkey);
            d.basis = std::move(basis);
            d.documentRevision = doc.revision();
            // Yinelenen/fazlalik zincir engeli: ayni gorunum+yon+uc noktalar zaten varsa EKLENMEZ
            // (imalat rolu, genel olcuden onceliklidir).
            for (Pending& q : pend) {
                if (!sameDim(q.d, d)) continue;
                if (isFabRole(role) && !isFabRole(q.d.role)) { d.id = sheet.allocateId(); q.d = d; q.group = group; }
                return;
            }
            d.id = sheet.allocateId();
            pend.push_back({d, group});
        };
        constexpr int kLengthGroup = 100, kOverallGroup = 101, kPlateGroup = 60, kOffsetGroup = 70;

        const bool isDetail = v.kind == ViewKind::Detail;
        // (1) Montaj uyelerinin kenar konumlari zinciri (yalniz assembly, >1 uye).
        if (!isDetail && sheet.kind == DrawingKind::Assembly && v.members.size() > 1) {
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
                           {xs[i + 1], v.bounds.min.y}, xs[i + 1] - xs[i], subject, 0, {}, "member edge chain");
            if (ys.size() > 2)
                for (std::size_t i = 0; i + 1 < ys.size(); ++i)
                    newDim(DimensionRole::PartPosition, DimensionDirection::Vertical, {v.bounds.max.x, ys[i]},
                           {v.bounds.max.x, ys[i + 1]}, ys[i + 1] - ys[i], subject, 0, {}, "member edge chain");
        }

        // (2) P1 yedek delik zincirleri: yalniz imalat cercevesi OLMAYAN sahipler icin.
        {
            std::vector<EntityId> owners;
            for (const Circle2& c : v.circles)
                if (!fabOwners.count(c.owner) && !isDetail && std::find(owners.begin(), owners.end(), c.owner) == owners.end())
                    owners.push_back(c.owner);
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
                const Bounds2& pbm = pm->bounds;
                std::vector<std::pair<double, double>> hx, hy;
                hx.push_back({pbm.min.x, pbm.min.y});
                for (const double x : cx) {
                    double y = 1e300;
                    for (const Vec2& c : centres) if (std::fabs(c.x - x) <= 0.05) y = std::min(y, c.y);
                    hx.push_back({x, y});
                }
                hx.push_back({pbm.max.x, pbm.min.y});
                hy.push_back({pbm.min.y, pbm.max.x});
                for (const double y : cy) {
                    double x = -1e300;
                    for (const Vec2& c : centres) if (std::fabs(c.y - y) <= 0.05) x = std::max(x, c.x);
                    hy.push_back({y, x});
                }
                hy.push_back({pbm.max.y, pbm.max.x});
                for (std::size_t i = 0; i + 1 < hx.size(); ++i) {
                    const double gap = hx[i + 1].first - hx[i].first;
                    if (gap >= kMinGap)
                        newDim(DimensionRole::HoleSpacing, DimensionDirection::Horizontal, {hx[i].first, hx[i].second},
                               {hx[i + 1].first, hx[i + 1].second}, gap, owner, group, {},
                               "P1 fallback: view-projected hole circles (no plate frame)");
                }
                for (std::size_t i = 0; i + 1 < hy.size(); ++i) {
                    const double gap = hy[i + 1].first - hy[i].first;
                    if (gap >= kMinGap)
                        newDim(DimensionRole::HoleSpacing, DimensionDirection::Vertical, {hy[i].second, hy[i].first},
                               {hy[i + 1].second, hy[i + 1].first}, gap, owner, group, {},
                               "P1 fallback: view-projected hole circles (no plate frame)");
                }
                ++group;
            }
        }

        // (2b) P2A: imalat olculeri — YALNIZ levha yuzunu gercek uzunlukta gosteren gorunumde.
        if (opt.fabrication) {
            for (const fab::PlateFeature& P : inv.plates) {
                if (isDetail) { if (v.detailPlate != P.plate) continue; }
                else if (detailPlates.count(P.plate)) continue;   // bu levhanin olculeri DETAY gorunumunde
                const double facing = std::fabs(P.n.x * v.frame.normal.x + P.n.y * v.frame.normal.y + P.n.z * v.frame.normal.z);
                if (facing < 1.0 - 1e-6) continue;   // levha bu gorunumde yuz-yuze degil
                const auto uvLocal = [&](double u, double w) {
                    return localOf(v, {P.origin.x + P.u.x * u + P.v.x * w, P.origin.y + P.u.y * u + P.v.y * w,
                                       P.origin.z + P.u.z * u + P.v.z * w});
                };
                const AxisInView au = axisInView(v, P.u), av = axisInView(v, P.v);
                if (!au.inPlane || !av.inPlane) continue;
                // Zincir yonu: u ekseninin gorunumdeki yonu.
                const DimensionDirection dirU = au.dir, dirV = av.dir;
                // Levha iki yonu da yatay/dusey degilse Aligned.
                std::vector<const fab::HoleGroupFeature*> gs;
                for (const fab::HoleGroupFeature& g : inv.groups)
                    if (g.plate == P.plate) gs.push_back(&g);
                // Gorunumde en alt / en sag satir-sutun secimi.
                const auto viewYOfV = [&](double u, double w) { return uvLocal(u, w).y; };
                const auto viewXOfU = [&](double u, double w) { return uvLocal(u, w).x; };
                int chainNo = 1;
                std::set<std::string> doneU;   // ayni sutun listesi yinelenmez
                // --- u yonu zinciri (her grup kendi sutunlariyla) ---
                std::vector<const fab::HoleGroupFeature*> ordered = gs;
                std::sort(ordered.begin(), ordered.end(), [](const fab::HoleGroupFeature* a, const fab::HoleGroupFeature* b) {
                    return a->rowV.front() > b->rowV.front();   // ustten alta
                });
                for (const fab::HoleGroupFeature* g : ordered) {
                    // YUVALI DELIK (slot) de olculendirilir: imalatta slot MERKEZININ
                    // konumu ayni sekilde olculur (kontur cizimi ayri is — SLOT_NOT_DRAWN).
                    // Kullanici geri bildirimi (2026-10-28): "paper drawing delik
                    // yerlerine olcu vermedi" — Tekla'da bulon deligi yuva olarak
                    // modellendiginde TUM delik olculeri kayboluyordu.
                    if (!g->regular) {
                        // P2D: duzensiz (kademeli/silinmis bulon ya da elle acilmis karisik) dizilim.
                        // Uydurma periyodik adim URETILMEZ; her deligin levha kenarindan GERCEK
                        // merkez koordinati (u ve v) olculur. Boylece hicbir delik olcusuz kalmaz.
                        const int grp = 10 + chainNo++;
                        for (const std::size_t hi : g->holes) {
                            const fab::HoleFeature& h = inv.holes[hi];
                            newDim(DimensionRole::HoleOrdinate, dirU, uvLocal(P.envMinU, h.v), uvLocal(h.u, h.v),
                                   h.u - P.envMinU, P.plate, grp, g->id,
                                   "true centre ordinate: plate min-u edge to hole centre u (irregular pattern)");
                            newDim(DimensionRole::HoleOrdinate, dirV, uvLocal(h.u, P.envMinV), uvLocal(h.u, h.v),
                                   h.v - P.envMinV, P.plate, grp, g->id,
                                   "true centre ordinate: plate min-v edge to hole centre v (irregular pattern)");
                        }
                        continue;
                    }
                    std::string sig;
                    for (const double cu : g->colU) sig += std::to_string(std::llround(cu * 10)) + ",";
                    sig += "d" + std::to_string(std::llround(g->diameter * 10));
                    if (!doneU.insert(sig).second) continue;   // ayni sutun zinciri zaten var
                    // temsilci satir: u ekseni yataysa gorunumde en alt, dikeyse en sag
                    double rep = g->rowV.front();
                    double bestScore = 1e300;
                    for (const double rv : g->rowV) {
                        const double sc = dirU == DimensionDirection::Vertical ? -viewXOfU(g->colU.front(), rv)
                                                                              : viewYOfV(g->colU.front(), rv);
                        if (sc < bestScore - 1e-9) { bestScore = sc; rep = rv; }
                    }
                    const double r = g->diameter / 2.0;
                    const int grp = 10 + chainNo++;
                    const auto add = [&](DimensionRole role, double ua, double ub, double value, const char* basis) {
                        newDim(role, dirU, uvLocal(ua, rep), uvLocal(ub, rep), value, P.plate, grp, g->id, basis);
                    };
                    add(DimensionRole::HoleEdge, P.envMinU, g->colU.front() - r, g->edgeMinU, "net edge: plate min-u edge to hole outer edge");
                    for (std::size_t i = 0; i + 1 < g->colU.size(); ++i)
                        add(DimensionRole::HolePitch, g->colU[i], g->colU[i + 1], g->pitchU[i], "centre pitch along plate u");
                    add(DimensionRole::HoleEdge, g->colU.back() + r, P.envMaxU, g->edgeMaxU, "net edge: hole outer edge to plate max-u edge");
                }
                // --- v yonu zinciri: grup zincirlerini tek plaka zincirine bagla (ustten alta) ---
                {
                    std::vector<const fab::HoleGroupFeature*> vg;
                    for (const fab::HoleGroupFeature* g : ordered)
                        if (g->regular) vg.push_back(g);   // slot gruplari da zincire girer
                    if (!vg.empty()) {
                        const int grp = 10 + chainNo++;
                        // temsilci sutun: v ekseni dikeyse gorunumde en sag; yataysa en alt
                        const auto repCol = [&](const fab::HoleGroupFeature* g) {
                            double rep = g->colU.back();
                            double best = 1e300;
                            for (const double cu : g->colU) {
                                const double sc = dirV == DimensionDirection::Horizontal ? viewYOfV(cu, g->rowV.front()) : -viewXOfU(cu, g->rowV.front());
                                if (sc < best - 1e-9) { best = sc; rep = cu; }
                            }
                            return rep;
                        };
                        const auto addv = [&](DimensionRole role, double ua, double va, double ub, double vb, double value,
                                              const std::string& fk, const char* basis) {
                            newDim(role, dirV, uvLocal(ua, va), uvLocal(ub, vb), value, P.plate, grp, fk, basis);
                        };
                        const fab::HoleGroupFeature* top = vg.front();
                        addv(DimensionRole::HoleEdge, repCol(top), P.envMaxV, repCol(top), top->rowV.front() + top->diameter / 2.0,
                             top->edgeTop, top->id, "net edge: plate max-v edge to upper hole outer edge");
                        for (std::size_t gi = 0; gi < vg.size(); ++gi) {
                            const fab::HoleGroupFeature* g = vg[gi];
                            const double r = g->diameter / 2.0;
                            const double uc = repCol(g);
                            for (std::size_t i = 0; i + 1 < g->rowV.size(); ++i)
                                addv(DimensionRole::HolePitch, uc, g->rowV[i], uc, g->rowV[i + 1], g->pitchV[i], g->id,
                                     "centre pitch along plate v");
                            if (gi + 1 < vg.size()) {
                                const fab::HoleGroupFeature* nx = vg[gi + 1];
                                const double upper = g->rowV.back() - r;
                                const double lower = nx->rowV.front() + nx->diameter / 2.0;
                                const double gap = upper - lower;
                                if (gap > 0.0)
                                    addv(DimensionRole::HoleEdge, repCol(g), upper, repCol(nx), lower, gap, nx->id,
                                         "net gap between independent bolt groups");
                            }
                        }
                        const fab::HoleGroupFeature* bot = vg.back();
                        addv(DimensionRole::HoleEdge, repCol(bot), bot->rowV.back() - bot->diameter / 2.0, repCol(bot), P.envMinV,
                             bot->edgeBottom, bot->id, "net edge: lower hole outer edge to plate min-v edge");
                    }
                }
                // --- levha boy/en ---
                if (P.outlineExact) {
                    newDim(DimensionRole::PlateWidth, dirU, uvLocal(P.envMinU, P.envMinV), uvLocal(P.envMaxU, P.envMinV),
                           P.width, P.plate, kPlateGroup, P.id, "plate width along u (outline envelope)");
                    newDim(DimensionRole::PlateHeight, dirV, uvLocal(P.envMaxU, P.envMinV), uvLocal(P.envMaxU, P.envMaxV),
                           P.height, P.plate, kPlateGroup + 1, P.id, "plate height along v (outline envelope)");
                }
                // --- callout'lar (anchor view-yerel) ---
                for (const fab::HoleGroupFeature* g : ordered) {
                    TextAnnotation& t = addText(sheet, TextKind::HoleCallout, {}, opt.layout.noteTextHeight);
                    const std::string mm = fmt1(g->diameter);
                    std::string dia = mm;
                    if (dia.size() > 2 && dia.substr(dia.size() - 2) == ".0") dia.resize(dia.size() - 2);
                    t.lines = {std::to_string(g->count()) + " x \xC3\x98" + dia +
                               (g->shape == fab::HoleShape::Slot ? " (YUVA)" : "")};
                    t.viewId = v.id;
                    t.hasAnchor = true;
                    t.leader = true;
                    t.anchor = uvLocal(g->colU.front(), g->rowV.front());
                    t.source = P.plate;
                    t.featureKey = g->id;
                }
                {
                    TextAnnotation& t = addText(sheet, TextKind::PlateNote, {}, opt.layout.noteTextHeight);
                    t.lines = {"PL " + (std::fabs(P.thickness - std::round(P.thickness)) < 0.05 ? std::to_string(std::lround(P.thickness)) : fmt1(P.thickness)) +
                               (P.material.empty() ? std::string() : "  " + P.material)};
                    t.viewId = v.id;
                    t.hasAnchor = true;
                    t.leader = true;
                    t.anchor = uvLocal(P.envMaxU, P.envMaxV);
                    t.source = P.plate;
                    t.featureKey = P.id;
                }
            }
        }

        // (2c) P2A: levha ofsetleri (uye BASLANGICINDAN) — uye ekseninin gorunum duzleminde oldugu gorunum.
        if (opt.fabrication && inv.datum.valid && !isDetail) {
            const fab::MemberDatum& D = inv.datum;
            for (const fab::PlateFeature& P : inv.plates) {
                if (P.plate == D.member) continue;
                const double cn[3] = {P.localCentre.x, P.localCentre.y, P.localCentre.z};
                const Vec3 axes[3] = {D.x, D.y, D.z};
                const DimensionRole roles[3] = {DimensionRole::PlateOffsetX, DimensionRole::PlateOffsetY, DimensionRole::PlateOffsetZ};
                static const char* an[3] = {"x'", "y'", "z'"};
                for (int k = 0; k < 3; ++k) {
                    bool straddle = false;
                    const double ref = P.offsetRef(k, &straddle);
                    if (std::fabs(ref) <= 0.5) continue;   // levha yuzu/merkezi uye baslangicinda: ofset yok
                    // Bu gorunumde k. eksen gercek uzunluk mu? (yalniz bir gorunumde uret: ilk uygun)
                    const AxisInView ai = axisInView(v, axes[k]);
                    if (!ai.inPlane || ai.dir == DimensionDirection::Aligned) continue;
                    bool alreadyElsewhere = false;
                    for (const Pending& q : pend) if (q.d.role == roles[k] && q.d.featureKey == P.id) alreadyElsewhere = true;
                    for (const Dimension2D& q : sheet.dimensions) if (q.role == roles[k] && q.featureKey == P.id) alreadyElsewhere = true;
                    if (alreadyElsewhere) continue;
                    // Uc noktalar: uye baslangici (eksen uzerinde) ve levha ofset noktasi.
                    Vec3 target = D.start;
                    target = {target.x + axes[k].x * ref, target.y + axes[k].y * ref, target.z + axes[k].z * ref};
                    const int other = k == 0 ? 1 : 0;   // dik koordinat: levha zarf merkezi (ext cizgisi levhaya degsin)
                    (void)other;
                    // Levha noktasi: zarfin bu eksendeki ref kademesinde, gorunumdeki dik koordinat levha merkezine esit.
                    Vec3 plateP = target;
                    for (int m = 0; m < 3; ++m) {
                        if (m == k) continue;
                        const AxisInView am = axisInView(v, axes[m]);
                        if (!am.inPlane) continue;   // gorunume dik eksen: izdusumde etkisiz
                        plateP = {plateP.x + axes[m].x * cn[m], plateP.y + axes[m].y * cn[m], plateP.z + axes[m].z * cn[m]};
                    }
                    newDim(roles[k], ai.dir, localOf(v, D.start), localOf(v, plateP), std::fabs(ref), P.plate, kOffsetGroup + k,
                           P.id,
                           std::string("offset along member-local ") + an[k] + " from member START to " +
                               (straddle ? "plate centre (plate straddles axis)" : "plate near edge"),
                           -1);
                }
            }
        }

        // (3) Uye boyu (ana parca ekseni, gorunum duzleminde ise) + genel olculer.
        bool haveLength = false;
        DimensionDirection lengthDir = DimensionDirection::Horizontal;
        if (hasAxis && !isDetail) {
            const Vec2 pa = v.frame.project(axA), pb2 = v.frame.project(axB);
            const double projLen = std::hypot(pb2.x - pa.x, pb2.y - pa.y);
            if (projLen >= axisLen * 0.999) {   // eksen gorunum duzleminde: gercek boy olculur
                const double adx = std::fabs(pb2.x - pa.x), ady = std::fabs(pb2.y - pa.y);
                lengthDir = ady < projLen * 1e-3   ? DimensionDirection::Horizontal
                            : adx < projLen * 1e-3 ? DimensionDirection::Vertical
                                                   : DimensionDirection::Aligned;
                // KULLANICI GERI BILDIRIMI (2026-10-28): "5000 olcusu hangi uzunluga ait belli degil".
                // Eksen boyu CIZILEN govdenin uzunlugundan farkliysa (or. eksen uclari kolon yuzunde,
                // govde 95 mm iceride) olcu, gorunen parcanin disina tasar ve neyi olctugu anlasilmaz.
                // KURAL: eksen olcusu YALNIZ eksen uclari gorunen govde uclariyla cakisiyorsa yazilir;
                // aksi halde cizilen govde (bounding box) olculur.
                const double drawnLen = lengthDir == DimensionDirection::Horizontal ? v.bounds.width()
                                        : lengthDir == DimensionDirection::Vertical ? v.bounds.height()
                                                                                   : axisLen;
                if (std::fabs(drawnLen - axisLen) <= 0.5) {
                    newDim(DimensionRole::MemberLength, lengthDir, {pa.x - v.projectionOffset.x, pa.y - v.projectionOffset.y},
                           {pb2.x - v.projectionOffset.x, pb2.y - v.projectionOffset.y}, axisLen, mainPart, kLengthGroup,
                           inv.datum.valid ? inv.datum.id : std::string(), "member axis length (true 3D axis, axis in view plane)");
                    haveLength = true;
                }
            }
        }
        // KULLANICI GERI BILDIRIMI (2026-10-28): "dis tan disa 5000 olcusu alakasiz" —
        // uye boyu olcusu (4810) varken AYNI yonde genel/bounding-box olcusu (5000) eklenince iki
        // uzun olcu ust uste biniyor ve hangisinin ne oldugu anlasilmiyor. Kural: ayni yonde uye
        // boyu olcusu varsa genel (bbox) olcu EKLENMEZ; dis-tan-disa toplam imalat olcu zincirinden
        // (565 + ... + 565) zaten okunur.
        const auto hasLengthIn = [&](DimensionDirection dir) { return haveLength && lengthDir == dir; };
        const double w = v.bounds.width(), h = v.bounds.height();
        if (!isDetail && w >= 0.5 && !hasLengthIn(DimensionDirection::Horizontal))
            newDim(DimensionRole::OverallWidth, DimensionDirection::Horizontal, {v.bounds.min.x, v.bounds.min.y},
                   {v.bounds.max.x, v.bounds.min.y}, w, subject, kOverallGroup, {}, "view bounding box width");
        if (!isDetail && h >= 0.5 && !hasLengthIn(DimensionDirection::Vertical))
            newDim(DimensionRole::OverallHeight, DimensionDirection::Vertical, {v.bounds.max.x, v.bounds.min.y},
                   {v.bounds.max.x, v.bounds.max.y}, h, subject, kOverallGroup, {}, "view bounding box height");

        // (4) P1 yedek satir atamasi (autoLayout kapaliyken kullanilir; layout bunu yeniden hesaplar).
        {
            int rowsH = 0, rowsV = 0;
            for (const auto dir : {DimensionDirection::Horizontal, DimensionDirection::Vertical}) {
                std::vector<int> groups;
                for (const Pending& p : pend)
                    if (p.d.viewId == v.id && p.d.direction == dir) groups.push_back(p.group);
                std::sort(groups.begin(), groups.end());
                groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
                (dir == DimensionDirection::Horizontal ? rowsH : rowsV) = static_cast<int>(groups.size());
                for (Pending& p : pend)
                    if (p.d.viewId == v.id && p.d.direction == dir) {
                        const auto row = std::find(groups.begin(), groups.end(), p.group) - groups.begin();
                        p.d.offset = 10.0 + 8.0 * static_cast<double>(row);
                    }
            }
            for (Pending& p : pend)
                if (p.d.viewId == v.id && p.d.direction == DimensionDirection::Aligned)
                    p.d.offset = 10.0 + 8.0 * static_cast<double>(std::max(rowsH, rowsV));
        }
    }

    // Olculeri sheet'e al: oncelik sirasi (grup) korunur; zincir kimligi = grup x yon.
    std::stable_sort(pend.begin(), pend.end(), [](const Pending& a, const Pending& b) { return a.group < b.group; });
    for (Pending& p : pend) {
        p.d.chain = static_cast<ElementId>(p.group * 4 + static_cast<int>(p.d.direction) + 1) + p.d.viewId * 1000;
        sheet.dimensions.push_back(std::move(p.d));
    }

    // Circle2 etiketleri: callout'u olan imalat gruplarinin dairelerinde P1 etiketini kaldir (cift metin yok).
    if (opt.fabrication)
        for (DrawingView& v : sheet.views)
            for (Circle2& c : v.circles)
                if (fabOwners.count(c.owner)) c.label.clear();

    // --- title block ---
    TitleBlock& tb = sheet.titleBlock;
    tb.project = opt.project;
    tb.drawnBy = opt.drawnBy;
    tb.date = opt.date;
    tb.drawingNumber = sheet.name;
    tb.title = std::string(sheet.kind == DrawingKind::Assembly ? "Assembly " : "Part ") + sheet.name;
    const std::string nm = deriveName(mainModel);
    if (!nm.empty()) tb.title += " - " + nm;

    // --- yerlesim ---
    bool layoutOk = true;
    if (opt.autoLayout) {
        const auto stampLines = [&]() {
            std::vector<std::string> st;
            if (sheet.fabrication.draft) {
                st.push_back("TASLAK / EKSIK IMALAT VERISI");
                st.push_back("IMALATA UYGUN DEGIL");
                int shown = 0;
                for (const ChecklistItem& c : sheet.fabrication.checklist)
                    if (c.state == ChecklistState::Unresolved && shown++ < 3)
                        st.push_back("- " + c.key + (c.detail.empty() ? std::string() : ": " + c.detail.substr(0, 38)));
            } else {
                st.push_back("Imalat verisi: desteklenen kapsam icin tam");
            }
            return st;
        };
        const auto installNotes = [&]() {
            sheet.texts.erase(std::remove_if(sheet.texts.begin(), sheet.texts.end(), [](const TextAnnotation& t) {
                                  return t.kind == TextKind::StatusStamp || t.kind == TextKind::Legend;
                              }), sheet.texts.end());
            if (!opt.fabrication) return;
            TextAnnotation& stamp = addText(sheet, TextKind::StatusStamp, stampLines(), opt.layout.noteTextHeight);
            stamp.bold = sheet.fabrication.draft;
            addText(sheet, TextKind::Legend,
                    {"Kapsam: levha olculeri+kalinlik, yuvarlak delik", "capi/adet/merkez/adim/net kenar, uye ofseti.",
                     "Disi: kesim, kaynak, yuvali delik, egik delik."},
                    opt.layout.noteTextHeight);
        };
        if (opt.fabrication) evaluateFabricationStatus(sheet, inv, true);
        installNotes();
        layoutOk = layoutSheet(sheet);
        for (int round = 0; round < 2 && opt.fabrication; ++round) {
            evaluateFabricationStatus(sheet, inv, layoutOk);
            const std::vector<std::string> want = stampLines();
            const auto it = std::find_if(sheet.texts.begin(), sheet.texts.end(),
                                         [](const TextAnnotation& t) { return t.kind == TextKind::StatusStamp; });
            if (it != sheet.texts.end() && it->lines == want) break;   // damga guncel
            installNotes();
            layoutOk = layoutSheet(sheet) && layoutOk;
        }
    } else if (opt.fabrication) {
        evaluateFabricationStatus(sheet, inv, false);
        sheet.layout.ran = false;
    }

    res.inventory = std::move(inv);
    res.sheet = std::move(sheet);
    res.ok = true;
    return res;
}

} // namespace mm::drawing
