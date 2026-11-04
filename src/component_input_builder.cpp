#include "model_maker/component_input_builder.hpp"
#include "model_maker/document.hpp"

#include <cmath>

namespace mm {

namespace {

// Bir EntityProperties + profil katalogundan kesit spec'i kurar (tek kaynak).
MemberSectionSpec sectionSpecFromProperties(
    const EntityProperties& p, const std::vector<SteelProfile>& catalog) {
    MemberSectionSpec spec;
    spec.width = 200.0;
    spec.height = 300.0;
    if (const SteelProfile* prof = findProfile(catalog, p.profileName)) {
        spec.width = prof->width;
        spec.height = prof->height;
        spec.webThickness = prof->plateThickness;
        spec.flangeThickness = prof->flangeThickness;
        // SectionAnchor: eksen kesitin ustunden gecebilir (varsayilan Top).
        // Levha/bulon govde merkezine otursun diye kesit merkez ofsetini gecir.
        spec.centerOffset = profileSectionAnchorOffset(*prof, p.sectionAnchor);
    } else {
        switch (p.sectionAnchor) {
        case SectionAnchor::Top: spec.centerOffset = -0.5 * spec.height; break;
        case SectionAnchor::Bottom: spec.centerOffset = 0.5 * spec.height; break;
        case SectionAnchor::Center: spec.centerOffset = 0.0; break;
        }
    }
    return spec;
}

// Noktadan dogru parcasina (segment) olan uzaklik (mm). main uyenin eksen
// dogrusu icin kullanilir: kirisin hangi ucunun kolona daha yakin oldugunu
// olcer (E / secenek C). Kesit sinir kutusu yerine eksen dogrusu yeterlidir —
// baglanti ucu eksene (kolon gövdesine) yaklasir, uzak uc bariz sekilde uzak
// kalir; Studio OCCT olcumu de ayni eksen-kesit kuralini uygular.
double pointSegmentDistance(const Vec3& q, const Vec3& a, const Vec3& b) {
    const double abx = b.x - a.x, aby = b.y - a.y, abz = b.z - a.z;
    const double apx = q.x - a.x, apy = q.y - a.y, apz = q.z - a.z;
    const double ab2 = abx * abx + aby * aby + abz * abz;
    double t = 0.0;
    if (ab2 > 1e-12) {
        t = (apx * abx + apy * aby + apz * abz) / ab2;
        if (t < 0.0) t = 0.0;
        else if (t > 1.0) t = 1.0;
    }
    const double cx = a.x + t * abx, cy = a.y + t * aby, cz = a.z + t * abz;
    const double dx = q.x - cx, dy = q.y - cy, dz = q.z - cz;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

MemberGeometry memberGeometryFromEntityProperties(
    const EntityProperties& p, const std::vector<SteelProfile>& catalog) noexcept {
    // MAIN (ve tek basina kullanim): EKSEN YONU KURALI — axisTo = baglanti ucu.
    // Modelde kolon alttan uste cizilir; axisTo = tepe = baglanti ucu. Bu kural
    // ikincil uyenin cizim yonunden BAGIMSIZDIR (yalniz kendi eksenine bakar).
    return memberGeometryFromProfile(
        {p.axisToX, p.axisToY, p.axisToZ},       // endA = baglanti ucu
        {p.axisFromX, p.axisFromY, p.axisFromZ}, // endB = uzak uc
        /*connectionAtA=*/true,
        {p.sectionNormalX, p.sectionNormalY, p.sectionNormalZ},
        p.profileRotation, sectionSpecFromProperties(p, catalog));
}

std::optional<MemberGeometry> memberGeometryFromEntityPropertiesAsSecondary(
    const EntityProperties& p, const std::vector<SteelProfile>& catalog,
    const MemberGeometry& main, std::wstring* error) noexcept {
    const MemberSectionSpec spec = sectionSpecFromProperties(p, catalog);
    const Vec3 sf{p.axisFromX, p.axisFromY, p.axisFromZ};
    const Vec3 st{p.axisToX, p.axisToY, p.axisToZ};
    const Vec3 n{p.sectionNormalX, p.sectionNormalY, p.sectionNormalZ};
    // Belirsizlik toleransi (mm): iki uc bu esik icinde esit uzaksa ret.
    constexpr double kAmbiguityTol = 1.0;
    const double dFrom = pointSegmentDistance(sf, main.axisFrom, main.axisTo);
    const double dTo = pointSegmentDistance(st, main.axisFrom, main.axisTo);
    if (dFrom < dTo - kAmbiguityTol) {
        // axisFrom = baglanti ucu.
        return memberGeometryFromProfile(sf, st, /*connectionAtA=*/true, n,
                                         p.profileRotation, spec);
    }
    if (dTo < dFrom - kAmbiguityTol) {
        // axisTo = baglanti ucu.
        return memberGeometryFromProfile(sf, st, /*connectionAtA=*/false, n,
                                         p.profileRotation, spec);
    }
    // Belirsiz: iki uc esit uzaklikta (kiriş kolonun tam ortasindan geciyor
    // gibi). Tahmin YOK — acik ret.
    if (error)
        *error = L"baglanti ucu belirsiz: kirisin iki ucu kolona esit uzaklikta (cizim yonunu degistirip tekrar deneyin)";
    return std::nullopt;
}

std::optional<ComponentInput> componentInputFromDocument(
    const Document& doc, const std::vector<SteelProfile>& catalog,
    EntityId mainPart, const std::vector<EntityId>& secondaryParts,
    const std::vector<CompParam>& params, std::wstring* error) {
    const auto mainIndex = doc.indexOfEntity(mainPart);
    if (!mainIndex) return std::nullopt;
    const auto hasSection = [&](std::size_t idx) {
        return findProfile(catalog,
                           doc.models()[idx].properties().profileName) != nullptr;
    };
    if (!hasSection(*mainIndex)) return std::nullopt;
    ComponentInput in;
    in.main = memberGeometryFromEntityProperties(doc.models()[*mainIndex].properties(),
                                                 catalog);
    in.main.id = doc.modelId(*mainIndex);
    // Ikincil uye baglanti ucu: GEOMETRIDEN (E / secenek C). Cizim yonu
    // (axisFrom/axisTo yer degistirmis) sonucu DEGISTIRMEZ. Iki uc tolerans
    // icinde esit uzaksa ACIK ret (tahmin yok).
    for (EntityId sid : secondaryParts) {
        const auto memberIndex = doc.indexOfEntity(sid);
        if (!memberIndex || !hasSection(*memberIndex)) continue;
        std::wstring e2;
        std::optional<MemberGeometry> g = memberGeometryFromEntityPropertiesAsSecondary(
            doc.models()[*memberIndex].properties(), catalog, in.main, &e2);
        if (!g) {
            if (error) *error = e2;
            return std::nullopt;
        }
        g->id = doc.modelId(*memberIndex);
        in.secondary.push_back(*g);
    }
    // Ikincil uye ZORUNLU DEGIL (taban levhasi gibi tek parcali tipler).
    in.params = params;
    return in;
}

} // namespace mm
