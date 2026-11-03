// MM-6b — OTOMATIK ONIZLEME TANIMI (duyarlilik analizi). OCCT'siz/Qt'siz.
//
// plan(p) ve plan(p+δ) farkindan hareket eden ozelligi ve eksenini bulur;
// olcu, hareket etmeyen en yakin anchor'dan hareket edene kurulur. Deterministik.

#include "model_maker/component_preview_auto.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

namespace mm {
namespace {

constexpr double kEps = 1e-6;

std::wstring wide(const std::string& s) {
    std::wstring w;
    w.reserve(s.size());
    for (unsigned char c : s) w.push_back(static_cast<wchar_t>(c));
    return w;
}
std::string narrow(const std::wstring& s) {
    std::string o;
    o.reserve(s.size());
    for (wchar_t c : s) o.push_back(c <= 0x7F ? static_cast<char>(c) : '?');
    return o;
}

double dot3(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double len3(const Vec3& a) { return std::sqrt(dot3(a, a)); }
Vec3 norm3(const Vec3& a) {
    const double l = len3(a);
    return l > 1e-12 ? Vec3{a.x / l, a.y / l, a.z / l} : Vec3{0.0, 0.0, 0.0};
}
Vec3 sub3(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

std::string roleAt(const std::vector<std::string>& keys, std::size_t i,
                   const char* positional) {
    if (i < keys.size() && !keys[i].empty()) return keys[i];
    return std::string(positional);
}

// Uyenin ekseni plate.axis'e paralel olan uye + rol adi. Bulunamazsa null.
const MemberGeometry* wrappedMember(const PlateSpec& plate, const ComponentInput& input,
                                    std::string& role) {
    const Vec3 paxis = norm3(plate.axis);
    if (len3(paxis) < 1e-9) return nullptr;
    auto tryMember = [&](const MemberGeometry& m, const std::string& r) -> const MemberGeometry* {
        const Vec3 ma = norm3({m.axisTo.x - m.axisFrom.x, m.axisTo.y - m.axisFrom.y,
                               m.axisTo.z - m.axisFrom.z});
        if (len3(ma) < 1e-9) return nullptr;
        if (std::fabs(dot3(paxis, ma)) > 0.9) {
            role = r;
            return &m;
        }
        return nullptr;
    };
    for (std::size_t i = 0; i < input.secondary.size(); ++i)
        if (const auto* m = tryMember(input.secondary[i], "secondary_" + std::to_string(i)))
            return m;
    if (const auto* m = tryMember(input.main, "main")) return m;
    return nullptr;
}

PreviewDimension makeDim(const std::wstring& param, const std::string& fromRole,
                         const std::string& fromAnchor, const std::string& toRole,
                         const std::string& toAnchor, wchar_t axis) {
    PreviewDimension d;
    d.param = param;
    d.from = wide(fromRole + "." + fromAnchor);
    d.to = wide(toRole + "." + toAnchor);
    d.axis = axis;
    return d;
}

}  // namespace

AutoPreviewResult autoPreviewDefinition(
    const std::function<GenerationPlan(const ComponentInput&)>& generate,
    const ComponentInput& baseInput, const std::vector<CompParamSpec>& specs) noexcept {
    AutoPreviewResult res;
    // Varsayilanlari hizala (temsili girdi).
    std::vector<CompParam> aligned = alignParamsToSpecs(specs, baseInput.params);

    ComponentInput base = baseInput;
    base.params = aligned;
    const GenerationPlan basePlan = generate(base);
    if (!basePlan.ok) {
        res.ok = false;
        res.error = L"otomatik onizleme: temel plan uretilemedi: " + basePlan.error;
        return res;
    }

    for (const auto& spec : specs) {
        // UZUNLUK/ACI/ADET: geometriyi hareket ettirebilen sayisal parametreler.
        // MM-7 sonrasi ADET de onemli: boltCountX/Y bulon izgarasinin uclarini
        // (col/row first-last) oynatir; eskiden yalniz Length/Angle geziyordu.
        const bool isCount = spec.type == CompParamType::Integer;
        if (spec.type != CompParamType::Length && spec.type != CompParamType::Angle && !isCount)
            continue;
        // δ ve yon: aralik ust sinirindaysa -δ (kenara carpma). Adette adim 1.
        double value = isCount ? static_cast<double>(paramAsInt(aligned, spec.name, spec.defInt))
                               : paramAsDouble(aligned, spec.name, spec.defDouble);
        double delta = 1.0;
        if (isCount) {
            if (spec.hasMaxInt && static_cast<int>(value) >= spec.maxInt) delta = -1.0;
        } else if (spec.hasMaxDouble && value >= spec.maxDouble - 1e-9) {
            delta = -1.0;
        }

        std::vector<CompParam> pert = aligned;
        bool found = false;
        for (auto& q : pert) {
            if (q.name == spec.name) {
                q.value = isCount ? CompParamValue{static_cast<int>(value + delta)}
                                  : CompParamValue{value + delta};
                found = true;
                break;
            }
        }
        if (!found)
            pert.push_back(CompParam{spec.name,
                                     isCount ? CompParamValue{static_cast<int>(value + delta)}
                                             : CompParamValue{value + delta}});

        ComponentInput pin = baseInput;
        pin.params = pert;
        const GenerationPlan pertPlan = generate(pin);
        // MM-7 notu: perturbasyon kontrolden gecemeyebilir (or. boltSpacingX
        // varsayilani 0 = "kesitten otomatik"; 1 mm'ye cekilince EC3 aralik
        // sarti FAIL eder ve plan.ok=false olur). Duyarlilik analizi URETIM
        // icin degil, "hangi parametre hangi ozelligi oynatiyor" icin: plan
        // reddedilse bile GEOMETRI uretildiyse karsilastirilir. Geometri hic
        // yoksa (erken donus) parametre etkisiz sayilir.
        if (!pertPlan.ok && pertPlan.plates.empty() && pertPlan.boltGroups.empty()) {
            res.ineffective.push_back(spec.name);
            continue;
        }

        std::vector<PreviewDimension> dims;
        // --- Levha farki ---
        const std::size_t np = std::min(basePlan.plates.size(), pertPlan.plates.size());
        for (std::size_t i = 0; i < np; ++i) {
            const PlateSpec& pb = basePlan.plates[i];
            const PlateSpec& pp = pertPlan.plates[i];
            const std::string role =
                roleAt(basePlan.plateKeys, i, ("plate_" + std::to_string(i)).c_str());
            const bool thChanged = std::fabs(pb.thickness - pp.thickness) > kEps;
            const bool wChanged = std::fabs(pb.width - pp.width) > kEps;
            const bool hChanged = std::fabs(pb.height - pp.height) > kEps;
            if (thChanged) {
                dims.push_back(makeDim(spec.name, role, "face_near", role, "face_far", L'w'));
                continue;
            }
            std::string mrole;
            const MemberGeometry* m = wrappedMember(pp, baseInput, mrole);
            if (wChanged && m && m->secWidth > 0.0) {
                // u yonunde tasma: uye yuzunden levha kenarina (iki taraf).
                // Isaret: uye width yonu levha u'suyla hizali (dot > 0 ise ayni).
                const ProfileSectionFrame mf = memberSectionFrame(*m);
                const double align = dot3(norm3(mf.width), norm3(pp.u));
                if (align >= 0.0) {
                    dims.push_back(makeDim(spec.name, mrole, "face_right", role, "edge_u_max", L'u'));
                    dims.push_back(makeDim(spec.name, role, "edge_u_min", mrole, "face_left", L'u'));
                } else {
                    dims.push_back(makeDim(spec.name, role, "edge_u_max", mrole, "face_left", L'u'));
                    dims.push_back(makeDim(spec.name, mrole, "face_right", role, "edge_u_min", L'u'));
                }
                continue;
            }
            if (hChanged && m && m->secHeight > 0.0) {
                const ProfileSectionFrame mf = memberSectionFrame(*m);
                const double align = dot3(norm3(mf.height), norm3(pp.v));
                if (align >= 0.0) {
                    dims.push_back(makeDim(spec.name, mrole, "face_top", role, "edge_v_max", L'v'));
                    dims.push_back(makeDim(spec.name, role, "edge_v_min", mrole, "face_bottom", L'v'));
                } else {
                    dims.push_back(makeDim(spec.name, role, "edge_v_max", mrole, "face_bottom", L'v'));
                    dims.push_back(makeDim(spec.name, mrole, "face_top", role, "edge_v_min", L'v'));
                }
                continue;
            }
        }
        // --- Bulon farki ---
        const std::size_t nb = std::min(basePlan.boltGroups.size(), pertPlan.boltGroups.size());
        for (std::size_t i = 0; i < nb; ++i) {
            const BoltGroup& bb = basePlan.boltGroups[i];
            const BoltGroup& pb = pertPlan.boltGroups[i];
            const std::string role =
                roleAt(basePlan.boltGroupKeys, i, ("bolt_group_" + std::to_string(i)).c_str());
            const bool sxChanged = std::fabs(bb.spacingX - pb.spacingX) > kEps ||
                                   !(bb.spacingXs == pb.spacingXs);
            const bool syChanged = std::fabs(bb.spacingY - pb.spacingY) > kEps ||
                                   !(bb.spacingYs == pb.spacingYs);
            if (sxChanged)
                dims.push_back(makeDim(spec.name, role, "col_first", role, "col_last", L'u'));
            if (syChanged)
                dims.push_back(makeDim(spec.name, role, "row_first", role, "row_last", L'v'));
            // Origin kaydi (kenar/offset): bulon'dan levha kenarina. Levha u/v
            // yonlerinde en buyuk yer degistirme ekseni secilir.
            const Vec3 od = sub3(pb.origin, bb.origin);
            if (!sxChanged && !syChanged && len3(od) > kEps) {
                // Levhanin eksenlerine izdusur; en buyuk bileşen yonu belirler.
                const PlateSpec* pl = nullptr;
                for (std::size_t k = 0; k < basePlan.plates.size(); ++k)
                    if (std::fabs(dot3(norm3(basePlan.plates[k].axis), norm3(bb.axis))) > 0.9) {
                        pl = &basePlan.plates[k];
                        break;
                    }
                if (pl) {
                    const Vec3 u = norm3(pl->u), v = norm3(pl->v);
                    const double du = std::fabs(dot3(od, u));
                    const double dv = std::fabs(dot3(od, v));
                    const std::string prole =
                        roleAt(basePlan.plateKeys, 0, "plate_0");
                    if (du >= dv)
                        dims.push_back(makeDim(spec.name, role, "col_last", prole, "edge_u_max", L'u'));
                    else
                        dims.push_back(makeDim(spec.name, role, "row_first", prole, "edge_v_min", L'v'));
                }
            }
        }

        if (dims.empty()) {
            res.ineffective.push_back(spec.name);
        } else {
            for (auto& d : dims) res.definition.dimensions.push_back(std::move(d));
        }
    }

    // --- Boolean -> toggle (plan(true) vs plan(false) rol farki) ---
    for (const auto& spec : specs) {
        if (spec.type != CompParamType::Boolean) continue;
        std::vector<CompParam> on = aligned, off = aligned;
        auto setBool = [&](std::vector<CompParam>& v, bool val) {
            bool f = false;
            for (auto& q : v)
                if (q.name == spec.name) { q.value = CompParamValue{val}; f = true; break; }
            if (!f) v.push_back(CompParam{spec.name, CompParamValue{val}});
        };
        setBool(on, true);
        setBool(off, false);
        ComponentInput inOn = baseInput, inOff = baseInput;
        inOn.params = on;
        inOff.params = off;
        const GenerationPlan planOn = generate(inOn);
        const GenerationPlan planOff = generate(inOff);
        if (planOn.ok && planOff.ok) {
            // true planinda var olup false'da olmayan roleKey on ekleri.
            std::vector<std::string> onlyOn;
            for (const auto& k : planOn.plateKeys)
                if (std::find(planOff.plateKeys.begin(), planOff.plateKeys.end(), k) ==
                    planOff.plateKeys.end())
                    onlyOn.push_back(k);
            for (const auto& k : planOn.boltGroupKeys)
                if (std::find(planOff.boltGroupKeys.begin(), planOff.boltGroupKeys.end(), k) ==
                    planOff.boltGroupKeys.end())
                    onlyOn.push_back(k);
            if (!onlyOn.empty()) {
                // En uzun ortak onek -> rolePrefix.
                std::string prefix = onlyOn.front();
                for (const auto& k : onlyOn)
                    while (prefix.size() && k.rfind(prefix, 0) != 0) prefix.pop_back();
                if (!prefix.empty()) {
                    PreviewToggle t;
                    t.param = spec.name;
                    t.rolePrefix = wide(prefix);
                    res.definition.toggles.push_back(std::move(t));
                }
            }
        }
    }

    res.ok = true;
    return res;
}

std::string autoPreviewCacheKey(const std::wstring& typeId,
                                const std::vector<CompParamSpec>& specs) noexcept {
    // typeId + parametre semasinin (ad,tur,min,max,default) kararli ozeti.
    // Deterministik; gercek SHA cekirdek katmaninda yoksa bile siralama sabittir.
    std::string key = narrow(typeId);
    key += "|";
    for (const auto& s : specs) {
        key += narrow(s.name);
        key += ":";
        key += std::to_string(static_cast<int>(s.type));
        key += ":";
        key += std::to_string(static_cast<long long>(std::llround(s.defDouble * 1e3)));
        key += ";";
    }
    return key;
}

}  // namespace mm
