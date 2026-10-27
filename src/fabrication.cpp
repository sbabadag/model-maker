#include "model_maker/fabrication.hpp"

#include "model_maker/bolt.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/utf8.hpp"
#include "model_maker/weld.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

namespace mm::fab {
namespace {

double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double len(const Vec3& a) { return std::sqrt(dot(a, a)); }
Vec3 unit(const Vec3& a) {
    const double l = len(a);
    return l < 1e-12 ? Vec3{} : Vec3{a.x / l, a.y / l, a.z / l};
}
Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 madd(const Vec3& a, const Vec3& d, double s) { return {a.x + d.x * s, a.y + d.y * s, a.z + d.z * s}; }

long long q10(double v) { return std::llround(v * 10.0); }

std::string num(long long v) { return std::to_string(v); }

std::vector<double> uniqueSorted(std::vector<double> xs, bool descending, double tol) {
    std::sort(xs.begin(), xs.end());
    std::vector<double> out;
    for (const double x : xs)
        if (out.empty() || x - out.back() > tol) out.push_back(x);
    if (descending) std::reverse(out.begin(), out.end());
    return out;
}

int indexNear(const std::vector<double>& xs, double v, double tol) {
    for (std::size_t i = 0; i < xs.size(); ++i)
        if (std::fabs(xs[i] - v) <= tol) return static_cast<int>(i);
    return -1;
}

// FNV-1a
void mix(std::uint64_t& h, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        h ^= (v >> (8 * i)) & 0xffu;
        h *= 1099511628211ull;
    }
}

struct PlateRef {
    const ComponentInstanceRecord* inst{nullptr};
    const ComponentChildRecord* child{nullptr};
};

PlateRef findPlateSpec(const Document& doc, EntityId part) {
    for (const ComponentInstanceRecord& r : doc.componentInstances())
        for (const ComponentChildRecord& c : r.children)
            if (c.kind == ComponentObjectKind::Plate && c.entityId == part && c.plate) return {&r, &c};
    return {};
}

// CCW outline, levha-yerel; pahlar dahil (tip 2/3 yay -> duz pah yaklasimi, exact=false).
std::vector<Vec2> plateOutline(const PlateSpec& p, bool& exact, bool& tapered) {
    exact = true;
    tapered = false;
    const double hw = p.width / 2.0, hh = p.height / 2.0;
    struct Cut { double h{0}, v{0}; };
    Cut cut[4];
    for (const PlateSpec::Chamfer& c : p.chamfers) {
        if (c.corner < 0 || c.corner > 3 || (c.h <= 0.0 && c.v <= 0.0)) continue;
        cut[c.corner] = {c.h, c.v};
        tapered = true;
        if (c.type != 1) exact = false;
    }
    std::vector<Vec2> o;
    // corner 0 (-u,-v): geliste yukaridan asagi inip sağa gider.
    if (cut[0].h > 0 || cut[0].v > 0) { o.push_back({-hw, -hh + cut[0].v}); o.push_back({-hw + cut[0].h, -hh}); }
    else o.push_back({-hw, -hh});
    if (cut[1].h > 0 || cut[1].v > 0) { o.push_back({hw - cut[1].h, -hh}); o.push_back({hw, -hh + cut[1].v}); }
    else o.push_back({hw, -hh});
    if (cut[2].h > 0 || cut[2].v > 0) { o.push_back({hw, hh - cut[2].v}); o.push_back({hw - cut[2].h, hh}); }
    else o.push_back({hw, hh});
    if (cut[3].h > 0 || cut[3].v > 0) { o.push_back({-hw + cut[3].h, hh}); o.push_back({-hw, hh - cut[3].v}); }
    else o.push_back({-hw, hh});
    return o;
}

} // namespace

std::string featureKey(FeatureKind kind, EntityId owner, EntityId group, long long a, long long b) {
    switch (kind) {
    case FeatureKind::MemberDatum: return "datum:" + num(static_cast<long long>(owner));
    case FeatureKind::Plate: return "plate:" + num(static_cast<long long>(owner));
    case FeatureKind::HoleGroup:
        return "group:" + num(static_cast<long long>(owner)) + "." + num(static_cast<long long>(group)) + "." + num(a);
    case FeatureKind::Hole:
        return "hole:" + num(static_cast<long long>(owner)) + "." + num(static_cast<long long>(group)) + "." +
               num(a) + "." + num(b);
    }
    return {};
}

Vec3 MemberDatum::toLocal(const Vec3& p) const noexcept {
    const Vec3 d = sub(p, start);
    return {dot(d, x), dot(d, y), dot(d, z)};
}

double PlateFeature::offsetRef(int k, bool* straddle) const noexcept {
    const double mn = k == 0 ? localMin.x : k == 1 ? localMin.y : localMin.z;
    const double mx = k == 0 ? localMax.x : k == 1 ? localMax.y : localMax.z;
    const double cn = k == 0 ? localCentre.x : k == 1 ? localCentre.y : localCentre.z;
    if (straddle) *straddle = false;
    if (mn > 0.05) return mn;
    if (mx < -0.05) return mx;
    if (mn < -0.05 && mx > 0.05) {
        if (straddle) *straddle = true;
        return cn;
    }
    return 0.0;
}

const PlateFeature* FabricationInventory::plateOf(EntityId plate) const noexcept {
    for (const PlateFeature& p : plates)
        if (p.plate == plate) return &p;
    return nullptr;
}

bool FabricationInventory::hasCode(const std::string& code) const noexcept {
    for (const Unresolved& u : unresolved)
        if (u.code == code) return true;
    return false;
}

FabricationInventory extractFabrication(const Document& doc, const std::vector<EntityId>& parts,
                                        EntityId mainPart) {
    FabricationInventory inv;
    inv.documentRevision = doc.revision();
    inv.mainPart = mainPart;
    const auto unresolved = [&](const char* code, std::string detail, EntityId e) {
        const Unresolved u{code, std::move(detail), e};
        if (std::find(inv.unresolved.begin(), inv.unresolved.end(), u) == inv.unresolved.end())
            inv.unresolved.push_back(u);
    };
    const std::set<EntityId> partSet(parts.begin(), parts.end());

    // --- (1) MemberDatum: gercek uye ekseni ---
    if (const auto mi = doc.indexOfEntity(mainPart)) {
        const EntityProperties& pr = doc.models()[*mi].properties();
        const Vec3 a{pr.axisFromX, pr.axisFromY, pr.axisFromZ};
        const Vec3 b{pr.axisToX, pr.axisToY, pr.axisToZ};
        const double L = len(sub(b, a));
        if (L > 1e-6) {
            MemberDatum& d = inv.datum;
            d.valid = true;
            d.member = mainPart;
            d.id = featureKey(FeatureKind::MemberDatum, mainPart);
            d.start = a;
            d.end = b;
            d.length = L;
            d.x = unit(sub(b, a));
            Vec3 up{0, 0, 1};
            Vec3 zp = sub(up, Vec3{d.x.x * d.x.z, d.x.y * d.x.z, d.x.z * d.x.z});  // up - (up.x)x
            if (len(zp) < 1e-6) {
                const Vec3 ex{1, 0, 0};
                zp = sub(ex, Vec3{d.x.x * d.x.x, d.x.y * d.x.x, d.x.z * d.x.x});
                d.orientationBasis = "dusey uye: z' = dunya +X'in eksene dik bileseni, y' = z' x x'";
            } else {
                d.orientationBasis = "z' = dunya +Z'nin eksene dik bileseni, y' = z' x x' (konvansiyon, kesit rolu degil)";
            }
            d.z = unit(zp);
            d.y = unit(cross(d.z, d.x));
            d.rollResolved = std::fabs(pr.profileRotation) < 1e-9;
            if (!d.rollResolved)
                unresolved("MEMBER_ROLL_UNRESOLVED",
                           "uye profilRotation != 0: y'/z' ofsetleri kesit eksenlerine gore dogrulanamadi", mainPart);
        }
    }

    // --- (2) Levhalar: yalniz kalici PlateSpec cercevesi olanlar ---
    for (const EntityId part : parts) {
        const PlateRef pr = findPlateSpec(doc, part);
        if (!pr.child) continue;
        const PlateSpec& s = *pr.child->plate;
        PlateFeature pf;
        pf.plate = part;
        pf.component = pr.inst->id;
        pf.roleKey = pr.child->roleKey;
        pf.id = featureKey(FeatureKind::Plate, part);
        pf.origin = s.center;
        pf.u = unit(s.u);
        pf.v = unit(s.v);
        pf.n = unit(s.axis);
        pf.width = s.width;
        pf.height = s.height;
        pf.thickness = s.thickness;
        pf.material = text::toUtf8(s.material);
        pf.documentRevision = inv.documentRevision;
        if (s.width <= 0.0 || s.height <= 0.0 || s.thickness <= 0.0 || len(pf.u) < 0.5 || len(pf.v) < 0.5 ||
            len(pf.n) < 0.5) {
            unresolved("PLATE_SPEC_INVALID", "levha olculeri/cercevesi gecersiz", part);
            continue;
        }
        pf.outline = plateOutline(s, pf.outlineExact, pf.tapered);
        if (!pf.outlineExact)
            unresolved("PLATE_OUTLINE_APPROX", "levhada yay pah (tip 2/3) var; kontur duz pah olarak yaklastirildi", part);
        pf.envMinU = -s.width / 2.0;
        pf.envMaxU = s.width / 2.0;
        pf.envMinV = -s.height / 2.0;
        pf.envMaxV = s.height / 2.0;

        // Dogrulama (cikarim DEGIL): kati modelin tum kosesi bu cerceve icinde kalmali, yoksa cerceve BAYAT.
        bool consistent = true;
        if (const auto mi = doc.indexOfEntity(part)) {
            constexpr double tol = 0.5;
            for (const Vec3& p : doc.models()[*mi].vertices()) {
                const Vec3 d = sub(p, pf.origin);
                if (std::fabs(dot(d, pf.u)) > s.width / 2.0 + tol || std::fabs(dot(d, pf.v)) > s.height / 2.0 + tol ||
                    std::fabs(dot(d, pf.n)) > s.thickness / 2.0 + tol) {
                    consistent = false;
                    break;
                }
            }
        }
        if (!consistent) {
            unresolved("PLATE_FRAME_STALE", "kayitli levha cercevesi gercek levha geometrisiyle uyusmuyor", part);
            continue;
        }
        // Uye-yerel zarf (gercek outline koselerinden).
        if (inv.datum.valid) {
            bool first = true;
            for (const Vec2& o : pf.outline)
                for (const double sgn : {-1.0, 1.0}) {
                    Vec3 w = madd(madd(madd(pf.origin, pf.u, o.x), pf.v, o.y), pf.n, sgn * s.thickness / 2.0);
                    const Vec3 l = inv.datum.toLocal(w);
                    if (first) { pf.localMin = pf.localMax = l; first = false; }
                    else {
                        pf.localMin = {std::min(pf.localMin.x, l.x), std::min(pf.localMin.y, l.y), std::min(pf.localMin.z, l.z)};
                        pf.localMax = {std::max(pf.localMax.x, l.x), std::max(pf.localMax.y, l.y), std::max(pf.localMax.z, l.z)};
                    }
                }
            pf.localCentre = inv.datum.toLocal(pf.origin);
        }
        inv.plates.push_back(std::move(pf));
    }

    // Uye ekseni YALNIZ ana parcadan farkli bir levha varsa gereklidir (levha ofseti baslangictan olculur).
    if (!inv.datum.valid)
        for (const PlateFeature& pf : inv.plates)
            if (pf.plate != mainPart) {
                unresolved("MEMBER_AXIS_MISSING",
                           "ana parcanin gercek ekseni (axisFrom/axisTo) yok; uye baslangic ofseti uretilemez", mainPart);
                break;
            }

    // --- (3) Delikler (gercek Hole CutModifier) ---
    std::map<std::tuple<EntityId, EntityId, long long, int, long long, long long>, std::vector<std::size_t>> byGroup;
    const auto& mods = doc.cutModifiers();
    std::set<EntityId> nonPlateHoleParts, cutParts;
    for (std::size_t mi = 0; mi < mods.size(); ++mi) {
        const CutModifier& m = mods[mi];
        if (!m.enabled || !partSet.count(m.target)) continue;
        if (m.kind != CutKind::Hole) {
            if (cutParts.insert(m.target).second)
                unresolved("CUT_NOT_DIMENSIONED",
                           "parcada delik-disi kesim (plane/polygon/fitting/line/boolean) var; kesim olculeri uretilmedi",
                           m.target);
            continue;
        }
        if (m.diameter <= 0.0) continue;
        const PlateFeature* pf = inv.plateOf(m.target);
        if (!pf) {
            if (nonPlateHoleParts.insert(m.target).second)
                unresolved("HOLE_FRAME_UNAVAILABLE",
                           "parcada delik var ama kalici levha cercevesi (PlateSpec) yok: delik konumlari imalat verisi olarak uretilemedi",
                           m.target);
            continue;
        }
        if (m.owner == kInvalidEntityId) {
            unresolved("HOLE_OWNER_UNKNOWN", "delik bir bulon grubuna ait degil (sahiplik cozumlenemedi)", m.target);
            continue;
        }
        const double al = len(m.planeNormal);
        if (al < 1e-9 || std::fabs(dot(unit(m.planeNormal), pf->n)) < 0.9999) {
            unresolved("HOLE_NOT_NORMAL_TO_PLATE", "delik ekseni levha normaline paralel degil (egik delik desteklenmiyor)",
                       m.target);
            continue;
        }
        // Derinlik: silindir planeOrigin'de ORTALI; levha yuzleri icinde kalmali.
        if (m.depth > 0.0) {
            const double sOff = dot(sub(pf->origin, m.planeOrigin), pf->n);
            const double half = m.depth / 2.0 + 1e-6;
            if (std::fabs(sOff - pf->thickness / 2.0) > half || std::fabs(sOff + pf->thickness / 2.0) > half) {
                unresolved("HOLE_PARTIAL_DEPTH", "delik derinligi levha kalinligini kapsamiyor (kor/kismi delik)", m.target);
                continue;
            }
        }
        const Vec3 d = sub(m.planeOrigin, pf->origin);
        const double u = dot(d, pf->u), v = dot(d, pf->v);
        if (std::fabs(u) > pf->width / 2.0 + 1e-6 || std::fabs(v) > pf->height / 2.0 + 1e-6) {
            unresolved("HOLE_OUTSIDE_PLATE", "delik merkezi levha zarfinin disinda", m.target);
            continue;
        }
        const bool slot = m.slotAlong > 0.0 || m.slotAcross > 0.0;
        byGroup[{m.target, m.owner, q10(m.diameter), slot ? 1 : 0, q10(m.slotAlong), q10(m.slotAcross)}].push_back(mi);
    }

    for (const auto& [key, mlist] : byGroup) {
        const EntityId plateId = std::get<0>(key), bgId = std::get<1>(key);
        const PlateFeature& pf = *inv.plateOf(plateId);
        const CutModifier& m0 = mods[mlist.front()];
        HoleGroupFeature g;
        g.plate = plateId;
        g.boltGroup = bgId;
        g.diameter = m0.diameter;
        g.shape = std::get<3>(key) ? HoleShape::Slot : HoleShape::Round;
        std::vector<double> us, vs;
        std::vector<std::pair<double, double>> uv;
        for (const std::size_t mi : mlist) {
            const Vec3 d = sub(mods[mi].planeOrigin, pf.origin);
            uv.push_back({dot(d, pf.u), dot(d, pf.v)});
            us.push_back(uv.back().first);
            vs.push_back(uv.back().second);
        }
        g.colU = uniqueSorted(us, false, 0.05);
        g.rowV = uniqueSorted(vs, true, 0.05);
        g.cols = static_cast<int>(g.colU.size());
        g.rows = static_cast<int>(g.rowV.size());
        g.regular = static_cast<std::size_t>(g.cols) * static_cast<std::size_t>(g.rows) == mlist.size();
        if (g.regular) {  // her (col,row) tam bir kez mi?
            std::set<std::pair<int, int>> seen;
            for (const auto& p : uv)
                seen.insert({indexNear(g.colU, p.first, 0.05), indexNear(g.rowV, p.second, 0.05)});
            g.regular = seen.size() == mlist.size();
        }
        g.id = featureKey(FeatureKind::HoleGroup, plateId, bgId, q10(g.diameter));
        for (std::size_t i = 0; i + 1 < g.colU.size(); ++i) g.pitchU.push_back(g.colU[i + 1] - g.colU[i]);
        for (std::size_t i = 0; i + 1 < g.rowV.size(); ++i) g.pitchV.push_back(g.rowV[i] - g.rowV[i + 1]);
        const double r = g.diameter / 2.0 + (g.shape == HoleShape::Slot ? 0.0 : 0.0);
        const double halfU = g.shape == HoleShape::Slot ? r + std::max(m0.slotAlong, m0.slotAcross) / 2.0 : r;
        // (slot yonu levha-yerelde cozumlenmediginden slot icin zarf muhafazakar; slot Unresolved zaten)
        g.edgeMinU = (g.colU.front() - halfU) - pf.envMinU;
        g.edgeMaxU = pf.envMaxU - (g.colU.back() + halfU);
        g.edgeTop = pf.envMaxV - (g.rowV.front() + halfU);
        g.edgeBottom = (g.rowV.back() - halfU) - pf.envMinV;
        if (g.edgeMinU < 0.0 || g.edgeMaxU < 0.0 || g.edgeTop < 0.0 || g.edgeBottom < 0.0)
            unresolved("HOLE_BREAKS_EDGE", "delik levha kenarini kesiyor (net kenar mesafesi < 0)", plateId);
        if (!g.regular)
            unresolved("HOLE_PATTERN_IRREGULAR",
                       "delik dizilimi duzgun dikdortgen ag degil (kademeli/silinmis bulon): periyodik olcu zinciri uretilmedi",
                       plateId);
        if (g.shape == HoleShape::Slot)
            unresolved("SLOT_NOT_DRAWN", "yuvali delik: merkez ve olculer raporlandi ama yuva konturu cizilmiyor", plateId);

        const std::size_t gi = inv.groups.size();
        for (std::size_t k = 0; k < mlist.size(); ++k) {
            const CutModifier& m = mods[mlist[k]];
            HoleFeature h;
            h.plate = plateId;
            h.boltGroup = bgId;
            h.modifierIndex = mlist[k];
            h.u = uv[k].first;
            h.v = uv[k].second;
            h.col = indexNear(g.colU, h.u, 0.05) + 1;
            h.row = indexNear(g.rowV, h.v, 0.05) + 1;
            h.world = m.planeOrigin;
            h.shape = g.shape;
            h.diameter = m.diameter;
            h.slotAlong = m.slotAlong;
            h.slotAcross = m.slotAcross;
            h.id = featureKey(FeatureKind::Hole, plateId, bgId, h.row, h.col);
            g.holes.push_back(inv.holes.size());
            inv.holes.push_back(std::move(h));
        }
        (void)gi;
        inv.groups.push_back(std::move(g));
    }

    // --- (4) Kaynaklar: tip/olcu imalat cizimine HENUZ aktarilmiyor ---
    for (const Weld& w : doc.welds())
        if (partSet.count(w.mainPart) || partSet.count(w.secondaryPart))
            unresolved("WELD_NOT_DIMENSIONED", "kaynak(lar) var; kaynak sembolu/olcusu cizimde henuz uretilmiyor",
                       w.id);
    return inv;
}

std::uint64_t drillingFingerprint(const Document& doc, EntityId part) {
    const auto& mods = doc.cutModifiers();
    std::vector<std::size_t> mine;
    for (std::size_t i = 0; i < mods.size(); ++i)
        if (mods[i].enabled && mods[i].target == part) mine.push_back(i);
    if (mine.empty()) return 0;
    const PlateRef pr = findPlateSpec(doc, part);
    std::vector<std::array<long long, 6>> rec;
    Vec3 centre{};
    if (!pr.child) {
        if (const auto mi = doc.indexOfEntity(part)) {
            const auto& vs = doc.models()[*mi].vertices();
            if (!vs.empty()) {
                Vec3 lo = vs[0], hi = vs[0];
                for (const Vec3& p : vs) {
                    lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
                    hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
                }
                centre = {(lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2};
            }
        }
    }
    for (const std::size_t i : mine) {
        const CutModifier& m = mods[i];
        if (m.kind != CutKind::Hole) {
            rec.push_back({100 + static_cast<long long>(m.kind), 0, 0, 0, 0, 0});
            continue;
        }
        if (pr.child) {
            const PlateSpec& s = *pr.child->plate;
            const Vec3 d = sub(m.planeOrigin, s.center);
            rec.push_back({1, q10(dot(d, unit(s.u))), q10(dot(d, unit(s.v))), q10(m.diameter), q10(m.slotAlong),
                           q10(m.slotAcross)});
        } else {
            rec.push_back({2, q10(len(sub(m.planeOrigin, centre))), 0, q10(m.diameter), q10(m.slotAlong),
                           q10(m.slotAcross)});
        }
    }
    std::sort(rec.begin(), rec.end());
    std::uint64_t h = 14695981039346656037ull;
    mix(h, rec.size());
    for (const auto& r : rec)
        for (const long long x : r) mix(h, static_cast<std::uint64_t>(x));
    return h == 0 ? 1 : h;
}

} // namespace mm::fab
