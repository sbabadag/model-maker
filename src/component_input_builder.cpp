#include "model_maker/component_input_builder.hpp"
#include "model_maker/document.hpp"

namespace mm {

MemberGeometry memberGeometryFromEntityProperties(
    const EntityProperties& p, const std::vector<SteelProfile>& catalog) noexcept {
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
    // Kanonik fonksiyon: axisTo = baglanti ucu (modelde oldugu gibi), axisFrom
    // = uzak uc; refNormal eksene diklestirilir ve isareti kanoniklestirilir.
    // (Dusey kolonda n ∥ axis -> profileSectionFrame fallback cercevesi birebir
    // korunur; bkz. memberGeometryFromProfile.)
    return memberGeometryFromProfile(
        {p.axisToX, p.axisToY, p.axisToZ},       // endA = baglanti ucu
        {p.axisFromX, p.axisFromY, p.axisFromZ}, // endB = uzak uc
        /*connectionAtA=*/true,
        {p.sectionNormalX, p.sectionNormalY, p.sectionNormalZ},
        p.profileRotation, spec);
}

std::optional<ComponentInput> componentInputFromDocument(
    const Document& doc, const std::vector<SteelProfile>& catalog,
    EntityId mainPart, const std::vector<EntityId>& secondaryParts,
    const std::vector<CompParam>& params) {
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
    for (EntityId sid : secondaryParts) {
        const auto memberIndex = doc.indexOfEntity(sid);
        if (!memberIndex || !hasSection(*memberIndex)) continue;
        MemberGeometry g = memberGeometryFromEntityProperties(
            doc.models()[*memberIndex].properties(), catalog);
        g.id = doc.modelId(*memberIndex);
        in.secondary.push_back(g);
    }
    // Ikincil uye ZORUNLU DEGIL (taban levhasi gibi tek parcali tipler).
    in.params = params;
    return in;
}

} // namespace mm
