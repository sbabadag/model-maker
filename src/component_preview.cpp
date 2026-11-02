// MM-6 — ANCHOR COZUCU + OLCU HESABI (OCCT'siz, Qt'siz).
//
// Onizleme olculeri veriyle tanimlanir (bakiniz component_preview.hpp); bu dosya
// "<role>.<anchor>" ifadesini GenerationPlan + MemberGeometry uzerinden 3B noktaya
// cevirir ve iki nokta arasindaki olcuyu hesaplar. Cizim/spin editoru Qt
// katmaninda (component_live_preview_widget) bu noktalari kullanir.

#include "model_maker/component_preview.hpp"

#include <algorithm>
#include <cmath>

namespace mm {
namespace {

std::wstring narrow(const std::string& s) {
    std::wstring w;
    w.reserve(s.size());
    for (unsigned char c : s) w.push_back(static_cast<wchar_t>(c));
    return w;
}

// roleKey'ler ASCII'dir; karsilastirma icin daraltma yeterli.
std::string toNarrow(const std::wstring& s) {
    std::string out;
    out.reserve(s.size());
    for (wchar_t c : s) out.push_back(c <= 0x7F ? static_cast<char>(c) : '?');
    return out;
}

Vec3 add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 mul(const Vec3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double len(const Vec3& a) { return std::sqrt(dot(a, a)); }
Vec3 norm(const Vec3& a) {
    const double l = len(a);
    return l > 1e-12 ? Vec3{a.x / l, a.y / l, a.z / l} : Vec3{0.0, 0.0, 0.0};
}
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

const PlateSpec* findPlate(const GenerationPlan& plan, const std::string& role) {
    for (std::size_t i = 0; i < plan.plates.size() && i < plan.plateKeys.size(); ++i)
        if (plan.plateKeys[i] == role) return &plan.plates[i];
    return nullptr;
}

const BoltGroup* findBolts(const GenerationPlan& plan, const std::string& role) {
    for (std::size_t i = 0; i < plan.boltGroups.size() && i < plan.boltGroupKeys.size(); ++i)
        if (plan.boltGroupKeys[i] == role) return &plan.boltGroups[i];
    return nullptr;
}

// "main" / "secondary_0" .. "secondary_N"
const MemberGeometry* findMember(const ComponentInput& input, const std::string& role) {
    if (role == "main") return &input.main;
    const std::string prefix = "secondary_";
    if (role.rfind(prefix, 0) == 0) {
        const std::string idx = role.substr(prefix.size());
        if (idx.empty()) return nullptr;
        std::size_t pos = 0;
        int n = 0;
        try {
            n = std::stoi(idx, &pos);
        } catch (...) {
            return nullptr;
        }
        if (pos != idx.size() || n < 0) return nullptr;
        if (static_cast<std::size_t>(n) < input.secondary.size())
            return &input.secondary[static_cast<std::size_t>(n)];
    }
    return nullptr;
}

bool memberAnchor(const MemberGeometry& m, const std::string& anchor, PreviewAnchor& out) {
    const Vec3 dir = norm(sub(m.axisTo, m.axisFrom));
    if (len(dir) < 1e-9) {
        out.error = L"uye ekseni sifir uzunlukta";
        return false;
    }
    if (anchor == "start") {
        out.point = m.axisFrom;
        return true;
    }
    if (anchor == "end") {
        out.point = m.axisTo;
        return true;
    }
    if (anchor == "center") {
        out.point = mul(add(m.axisFrom, m.axisTo), 0.5);
        return true;
    }
    // Kesit cercevesi (refNormal + profileRotation) — uyelerin yuz noktalari.
    const ProfileSectionFrame sec = memberSectionFrame(m);
    const Vec3 mid = mul(add(m.axisFrom, m.axisTo), 0.5);
    const Vec3 center = add(mid, mul(sec.height, m.sectionCenterOffset));
    const double hw = m.secWidth * 0.5;
    const double hh = m.secHeight * 0.5;
    if (anchor == "face_top") {
        out.point = add(center, mul(sec.height, hh));
        return true;
    }
    if (anchor == "face_bottom") {
        out.point = add(center, mul(sec.height, -hh));
        return true;
    }
    if (anchor == "face_left") {
        out.point = add(center, mul(sec.width, -hw));
        return true;
    }
    if (anchor == "face_right") {
        out.point = add(center, mul(sec.width, hw));
        return true;
    }
    out.error = L"bilinmeyen uye anchor'i: " + narrow(anchor);
    return false;
}

bool plateAnchor(const PlateSpec& p, const std::string& anchor, PreviewAnchor& out) {
    const double hw = p.width * 0.5;
    const double hh = p.height * 0.5;
    const double ht = p.thickness * 0.5;
    out.hasFrame = true;
    out.u = norm(p.u);
    out.v = norm(p.v);
    out.w = norm(p.axis);
    out.frameOrigin = p.center;
    if (anchor == "center") {
        out.point = p.center;
        return true;
    }
    if (anchor == "edge_u_min") {
        out.point = add(p.center, mul(p.u, -hw));
        return true;
    }
    if (anchor == "edge_u_max") {
        out.point = add(p.center, mul(p.u, hw));
        return true;
    }
    if (anchor == "edge_v_min") {
        out.point = add(p.center, mul(p.v, -hh));
        return true;
    }
    if (anchor == "edge_v_max") {
        out.point = add(p.center, mul(p.v, hh));
        return true;
    }
    if (anchor == "face_near") {
        out.point = add(p.center, mul(p.axis, -ht));
        return true;
    }
    if (anchor == "face_far") {
        out.point = add(p.center, mul(p.axis, ht));
        return true;
    }
    if (anchor == "corner_bl") {
        out.point = add(add(p.center, mul(p.u, -hw)), mul(p.v, -hh));
        return true;
    }
    if (anchor == "corner_br") {
        out.point = add(add(p.center, mul(p.u, hw)), mul(p.v, -hh));
        return true;
    }
    if (anchor == "corner_tr") {
        out.point = add(add(p.center, mul(p.u, hw)), mul(p.v, hh));
        return true;
    }
    if (anchor == "corner_tl") {
        out.point = add(add(p.center, mul(p.u, -hw)), mul(p.v, hh));
        return true;
    }
    out.error = L"bilinmeyen levha anchor'i: " + narrow(anchor);
    return false;
}

bool boltAnchor(const BoltGroup& b, const std::string& anchor, PreviewAnchor& out) {
    const int nx = std::max(1, b.countX);
    const int ny = std::max(1, b.countY);
    const Vec3 xdir = norm(b.xDir);
    Vec3 ydir = norm(cross(b.axis, b.xDir));
    if (len(xdir) < 1e-9) {
        out.error = L"bulon grubu x ekseni gecersiz";
        return false;
    }
    if (len(ydir) < 1e-9) ydir = norm(cross(b.axis, Vec3{0.0, 0.0, 1.0}));
    const double spanX = (nx - 1) * b.spacingX * 0.5;
    const double spanY = (ny - 1) * b.spacingY * 0.5;
    const Vec3 first = add(add(b.origin, mul(xdir, -spanX)), mul(ydir, -spanY));
    const Vec3 last = add(add(b.origin, mul(xdir, spanX)), mul(ydir, spanY));
    if (anchor == "center") {
        out.point = b.origin;
        return true;
    }
    if (anchor == "row_first") {
        out.point = add(b.origin, mul(ydir, -spanY));
        return true;
    }
    if (anchor == "row_last") {
        out.point = add(b.origin, mul(ydir, spanY));
        return true;
    }
    if (anchor == "col_first") {
        out.point = add(b.origin, mul(xdir, -spanX));
        return true;
    }
    if (anchor == "col_last") {
        out.point = add(b.origin, mul(xdir, spanX));
        return true;
    }
    if (anchor == "corner_first") {
        out.point = first;
        return true;
    }
    if (anchor == "corner_last") {
        out.point = last;
        return true;
    }
    out.error = L"bilinmeyen bulon anchor'i: " + narrow(anchor);
    return false;
}

// "role.anchor" ayristir; rol icinde nokta olmadigi varsayilir.
bool splitSpec(const std::wstring& spec, std::string& role, std::string& anchor,
               std::wstring& error) {
    std::string s;
    s.reserve(spec.size());
    for (wchar_t c : spec) {
        if (c > 0x7F) {
            error = L"anchor ifadesi ASCII olmali: " + spec;
            return false;
        }
        s.push_back(static_cast<char>(c));
    }
    const std::size_t dotPos = s.rfind('.');
    if (dotPos == std::string::npos || dotPos == 0 || dotPos + 1 >= s.size()) {
        error = L"anchor ifadesi '<rol>.<anchor>' biciminde degil: " + spec;
        return false;
    }
    role = s.substr(0, dotPos);
    anchor = s.substr(dotPos + 1);
    return true;
}

}  // namespace

PreviewAnchor resolvePreviewAnchor(const GenerationPlan& plan, const ComponentInput& input,
                                   const std::wstring& spec) noexcept {
    PreviewAnchor out;
    std::string role, anchor;
    if (!splitSpec(spec, role, anchor, out.error)) return out;

    if (const PlateSpec* p = findPlate(plan, role)) {
        out.ok = plateAnchor(*p, anchor, out);
        return out;
    }
    if (const BoltGroup* b = findBolts(plan, role)) {
        out.ok = boltAnchor(*b, anchor, out);
        return out;
    }
    if (const MemberGeometry* m = findMember(input, role)) {
        out.ok = memberAnchor(*m, anchor, out);
        return out;
    }
    out.error = L"cozulemeyen rol: " + narrow(role) + L" (plan roleKey'leri veya main/secondary_N olmali)";
    return out;
}

PreviewMeasurement measurePreviewDimension(const GenerationPlan& plan,
                                           const ComponentInput& input,
                                           const PreviewDimension& dim) noexcept {
    PreviewMeasurement m;
    const PreviewAnchor from = resolvePreviewAnchor(plan, input, dim.from);
    if (!from.ok) {
        m.error = L"from: " + from.error;
        return m;
    }
    const PreviewAnchor to = resolvePreviewAnchor(plan, input, dim.to);
    if (!to.ok) {
        m.error = L"to: " + to.error;
        return m;
    }
    m.from = from.point;
    m.to = to.point;
    const Vec3 delta = sub(to.point, from.point);
    m.distance = len(delta);
    Vec3 axis{0.0, 0.0, 1.0};
    // Eksen kaynagi: once `from` cercevesi, yoksa `to` cercevesi (uye anchor'i
    // levha anchor'i ile olculurken levha cercevesi kullanilir), yoksa dunya.
    const bool haveFrom = from.hasFrame;
    const PreviewAnchor& frame = haveFrom ? from : to;
    if (frame.hasFrame) {
        if (dim.axis == L'u') axis = frame.u;
        else if (dim.axis == L'v') axis = frame.v;
        else axis = frame.w;
    } else if (dim.axis == L'u') {
        axis = Vec3{1.0, 0.0, 0.0};
    } else if (dim.axis == L'v') {
        axis = Vec3{0.0, 1.0, 0.0};
    }
    if (len(axis) < 1e-9) {
        m.error = L"olcu ekseni gecersiz (cerceve ekseni sifir)";
        return m;
    }
    m.value = dot(delta, axis);
    m.ok = true;
    return m;
}

std::vector<std::string> previewToggleRoles(const GenerationPlan& plan,
                                            const PreviewToggle& toggle) noexcept {
    std::vector<std::string> roles;
    const std::string prefix = toNarrow(toggle.rolePrefix);
    if (prefix.empty()) return roles;
    for (const auto& key : plan.plateKeys)
        if (key.rfind(prefix, 0) == 0) roles.push_back(key);
    for (const auto& key : plan.boltGroupKeys)
        if (key.rfind(prefix, 0) == 0) roles.push_back(key);
    return roles;
}

bool previewToggleValue(const PreviewToggle& toggle,
                        const std::vector<CompParam>& params,
                        const std::vector<CompParamSpec>& specs) noexcept {
    if (const CompParam* p = findParam(params, toggle.param)) {
        if (const auto* b = std::get_if<bool>(&p->value)) return *b;
        if (const auto* d = std::get_if<double>(&p->value)) return *d != 0.0;
        if (const auto* i = std::get_if<int>(&p->value)) return *i != 0;
    }
    for (const auto& spec : specs)
        if (spec.name == toggle.param) return spec.defBool;
    return false;
}

}  // namespace mm
