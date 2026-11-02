#include "model_maker/component_checks.hpp"

#include <algorithm>
#include <cmath>

namespace mm {
namespace {

double dot3(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double len3(const Vec3& a) { return std::sqrt(dot3(a, a)); }
Vec3 sub3(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 add3(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 mul3(const Vec3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
Vec3 norm3(const Vec3& a) {
    const double l = len3(a);
    return l > 1e-12 ? Vec3{a.x / l, a.y / l, a.z / l} : Vec3{0.0, 0.0, 0.0};
}

std::wstring narrow(const std::string& s) {
    std::wstring w;
    w.reserve(s.size());
    for (unsigned char c : s) w.push_back(static_cast<wchar_t>(c));
    return w;
}

// Noktadan (px,py) dikdortgene [xlo,xhi]x[ylo,yhi] uzaklik (icerdeyse 0).
double distPointRect(double px, double py, double xlo, double xhi, double ylo,
                     double yhi) noexcept {
    double dx = 0.0, dy = 0.0;
    if (px < xlo) dx = xlo - px;
    else if (px > xhi) dx = px - xhi;
    if (py < ylo) dy = ylo - py;
    else if (py > yhi) dy = py - yhi;
    return std::sqrt(dx * dx + dy * dy);
}

// Bulon grubunun deldigi levhayi bul: ekseni bulon eksenine paralel olan ilk
// levha (end plate'te levha ekseni == bulon ekseni == kiris ekseni).
const PlateSpec* findBoltPlate(const GenerationPlan& plan, const BoltGroup& b) noexcept {
    const Vec3 baxis = norm3(b.axis);
    if (len3(baxis) < 1e-9) return nullptr;
    for (std::size_t i = 0; i < plan.plates.size(); ++i) {
        const Vec3 paxis = norm3(plan.plates[i].axis);
        if (len3(paxis) < 1e-9) continue;
        if (std::fabs(dot3(baxis, paxis)) > 0.9) return &plan.plates[i];
    }
    return nullptr;
}

double effectiveHoleDiameter(const BoltGroup& b) noexcept {
    double d0 = b.params.holeDiameter;
    if (!(d0 > 0.0)) d0 = standardBoltHoleDiameter(b.params.nominalDiameter);
    return d0;
}

// Ikincil uyenin rol adi ("secondary_N").
std::string memberRoleName(std::size_t idx) noexcept {
    return "secondary_" + std::to_string(idx);
}

// Bir (bulon grubu x ikincil uye) cakisma kontrolu ekler.
void checkBoltMemberClash(const BoltGroup& b, const std::vector<Vec3>& origins,
                          const MemberGeometry& m, std::size_t memberIdx,
                          double weldAllowance, std::vector<ComponentCheck>& out) {
    const double tf = m.secFlangeThickness;
    const double tw = m.secWebThickness;
    const double bW = m.secWidth;
    const double bH = m.secHeight;
    if (!(tf > 0.0) || !(tw > 0.0) || !(bW > 0.0) || !(bH > 0.0)) return;  // kesit bilinmiyor

    const ProfileSectionFrame sec = memberSectionFrame(m);
    const Vec3 mid = mul3(add3(m.axisFrom, m.axisTo), 0.5);
    const Vec3 center = add3(mid, mul3(sec.height, m.sectionCenterOffset));
    const double hw = bW * 0.5;
    const double hh = bH * 0.5;
    const double tw2 = tw * 0.5;

    const double R = effectiveHoleDiameter(b) * 0.5 + weldAllowance;

    // En kotu (en kucuk) net mesafeyi ve hangi bulon oldugunu izle.
    double worstClear = 1e18;
    int worstI = -1, worstJ = -1;
    bool clash = false;
    std::wstring region = L"baslik";

    const int nx = std::max(1, b.countX);
    const int ny = std::max(1, b.countY);
    for (std::size_t k = 0; k < origins.size(); ++k) {
        const Vec3 d = sub3(origins[k], center);
        const double w = dot3(d, sec.width);
        const double h = dot3(d, sec.height);
        // Uc dikdortgen: ust baslik, alt baslik, govde.
        const double dTop = distPointRect(w, h, -hw, hw, hh - tf, hh);
        const double dBot = distPointRect(w, h, -hw, hw, -hh, -hh + tf);
        const double dWeb = distPointRect(w, h, -tw2, tw2, -hh + tf, hh - tf);
        double dmin = std::min({dTop, dBot, dWeb});
        if (dmin < R) clash = true;
        if (dmin < worstClear) {
            worstClear = dmin;
            worstI = static_cast<int>(k % static_cast<std::size_t>(nx));
            worstJ = static_cast<int>(k / static_cast<std::size_t>(nx));
            if (dWeb <= dTop && dWeb <= dBot) region = L"govde";
            else region = L"baslik";
        }
    }
    if (!origins.empty()) {
        ComponentCheck c;
        c.id = (region == L"govde") ? std::string("bolt_web_clash")
                                    : std::string("bolt_flange_clash");
        c.pass = !clash;
        c.value = worstClear;
        c.limit = R;
        if (!c.pass) {
            const std::wstring role = narrow(memberRoleName(memberIdx));
            c.message = L"bulon (" + std::to_wstring(worstJ + 1) + L"," +
                        std::to_wstring(worstI + 1) + L") " + role + L" " + region +
                        L" izdusumuyle cakisiyor: net mesafe " +
                        std::to_wstring(static_cast<int>(std::round(worstClear))) + L" mm < " +
                        std::to_wstring(static_cast<int>(std::round(R))) + L" mm";
        }
        out.push_back(std::move(c));
    }
}

// Bir bulon grubu icin kenar + aralik kontrolu.
void checkBoltEdgeAndPitch(const BoltGroup& b, const GenerationPlan& plan,
                           const ComponentCheckOptions& opts,
                           std::vector<ComponentCheck>& out) {
    const double d0 = effectiveHoleDiameter(b);
    const int nx = std::max(1, b.countX);
    const int ny = std::max(1, b.countY);

    // Kenar mesafesi (e1/e2): grubun deldigi levhaya gore min kenar uzakligi.
    const PlateSpec* plate = findBoltPlate(plan, b);
    if (plate && plate->width > 0.0 && plate->height > 0.0) {
        const std::vector<Vec3> origins = boltGroupBoltOrigins(b);
        double e = 1e18;
        const Vec3 u = norm3(plate->u);
        const Vec3 v = norm3(plate->v);
        for (const Vec3& o : origins) {
            const Vec3 d = sub3(o, plate->center);
            const double uc = dot3(d, u);
            const double vc = dot3(d, v);
            const double eU = plate->width * 0.5 - std::fabs(uc);
            const double eV = plate->height * 0.5 - std::fabs(vc);
            e = std::min(e, std::min(eU, eV));
        }
        if (!origins.empty()) {
            const double lim = opts.minEdgeRatio * d0;
            ComponentCheck c;
            c.id = "edge_distance";
            c.pass = e >= lim;
            c.value = e;
            c.limit = lim;
            if (!c.pass) {
                c.message = L"kenar mesafesi " + std::to_wstring(static_cast<int>(std::round(e))) +
                            L" mm < " + std::to_wstring(static_cast<int>(std::round(lim))) +
                            L" mm (1.2*d0)";
            }
            out.push_back(std::move(c));
        }
    }

    // Aralik p1 (dikey / kuvvet yonu = spacingY) ve p2 (yatay = spacingX).
    if (ny > 1) {
        double p1 = b.spacingY;
        if (!b.spacingYs.empty()) {
            p1 = 1e18;
            for (double s : b.spacingYs)
                if (s > 0.0) p1 = std::min(p1, s);
        }
        if (p1 > 0.0) {
            const double lim = opts.minPitch1Ratio * d0;
            ComponentCheck c;
            c.id = "pitch_p1";
            c.pass = p1 >= lim;
            c.value = p1;
            c.limit = lim;
            if (!c.pass) {
                c.message = L"bulon sira araligi (p1) " +
                            std::to_wstring(static_cast<int>(std::round(p1))) + L" mm < " +
                            std::to_wstring(static_cast<int>(std::round(lim))) + L" mm (2.2*d0)";
            }
            out.push_back(std::move(c));
        }
    }
    if (nx > 1) {
        double p2 = b.spacingX;
        if (!b.spacingXs.empty()) {
            p2 = 1e18;
            for (double s : b.spacingXs)
                if (s > 0.0) p2 = std::min(p2, s);
        }
        if (p2 > 0.0) {
            const double lim = opts.minPitch2Ratio * d0;
            ComponentCheck c;
            c.id = "pitch_p2";
            c.pass = p2 >= lim;
            c.value = p2;
            c.limit = lim;
            if (!c.pass) {
                c.message = L"bulon kolon araligi (p2) " +
                            std::to_wstring(static_cast<int>(std::round(p2))) + L" mm < " +
                            std::to_wstring(static_cast<int>(std::round(lim))) + L" mm (2.4*d0)";
            }
            out.push_back(std::move(c));
        }
    }
}

}  // namespace

std::vector<ComponentCheck> runComponentChecks(const GenerationPlan& plan,
                                               const ComponentInput& input,
                                               const ComponentCheckOptions& opts) noexcept {
    std::vector<ComponentCheck> out;
    for (std::size_t gi = 0; gi < plan.boltGroups.size(); ++gi) {
        const BoltGroup& b = plan.boltGroups[gi];
        const std::vector<Vec3> origins = boltGroupBoltOrigins(b);
        if (origins.empty()) continue;
        // Cakisma: her ikincil uyeye karsi.
        for (std::size_t si = 0; si < input.secondary.size(); ++si)
            checkBoltMemberClash(b, origins, input.secondary[si], si, opts.weldAllowance, out);
        // Kenar + aralik.
        checkBoltEdgeAndPitch(b, plan, opts, out);
    }
    return out;
}

bool allChecksPass(const std::vector<ComponentCheck>& checks) noexcept {
    for (const auto& c : checks)
        if (!c.pass) return false;
    return true;
}

std::wstring failingChecksMessage(const std::vector<ComponentCheck>& checks) noexcept {
    std::wstring msg;
    for (const auto& c : checks) {
        if (c.pass) continue;
        if (!msg.empty()) msg += L"; ";
        msg += c.message;
    }
    return msg;
}

}  // namespace mm
