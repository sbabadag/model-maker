// MODEL MAKER BRIDGE — native component uye baglama (uygulama).

#include "model_maker/bridge_native_bind.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

namespace mm {

namespace {

// Source Tekla profile names are template metadata, NOT constraints on target
// Model Maker members. The actual member's current section drives generation.
Vec3 memberAxis(const Document& doc, EntityId id) {
    const auto idx=doc.indexOfEntity(id);
    if (!idx) return {};
    const auto& p=doc.models()[*idx].properties();
    return {p.axisToX-p.axisFromX,p.axisToY-p.axisFromY,p.axisToZ-p.axisFromZ};
}
double axisLength(const Vec3& v) {
    return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
}
double roleAlignment(const Document& doc, EntityId id,
                     const BridgeReconstructedMember& source) {
    if (!source.hasAxis) return 0.0;
    const Vec3 target=memberAxis(doc,id);
    const Vec3 ref={source.geometry.axisTo.x-source.geometry.axisFrom.x,
                    source.geometry.axisTo.y-source.geometry.axisFrom.y,
                    source.geometry.axisTo.z-source.geometry.axisFrom.z};
    const double scale=axisLength(target)*axisLength(ref);
    if (scale<=1e-8) return 0.0;
    return std::fabs((target.x*ref.x+target.y*ref.y+target.z*ref.z)/scale);
}

} // namespace

bool bridgeMemberCompatible(const Document& doc, EntityId id,
                            const BridgeReconstructedMember& source,
                            std::string& reason) noexcept {
    (void)source; // source profile/length must NOT lock the new native component
    if (id==kInvalidEntityId || !doc.indexOfEntity(id)) {
        reason="Document entity not found";
        return false;
    }
    const auto& p=doc.models()[*doc.indexOfEntity(id)].properties();
    if (p.profileName.empty()) {
        reason="selected entity has no steel profile";
        return false;
    }
    if (axisLength(memberAxis(doc,id))<=1e-6) {
        reason="selected member has no valid physical axis";
        return false;
    }
    reason.clear();
    return true;
}

MemberGeometry bridgeMemberGeometry(const Document& doc, EntityId id,
                                    const std::vector<SteelProfile>& catalog) noexcept {
    MemberGeometry g;
    g.id = id;
    const auto idx = doc.indexOfEntity(id);
    if (!idx) return g;
    const auto& p = doc.models()[*idx].properties();
    g.axisFrom = {p.axisFromX, p.axisFromY, p.axisFromZ};
    g.axisTo = {p.axisToX, p.axisToY, p.axisToZ};
    g.refNormal = {p.sectionNormalX, p.sectionNormalY, p.sectionNormalZ};
    g.secWidth = 200.0;
    g.secHeight = 300.0;
    g.sectionCenterOffset = 0.0;
    if (const SteelProfile* prof = findProfile(catalog, p.profileName)) {
        g.secWidth = prof->width;
        g.secHeight = prof->height;
        g.secWebThickness = prof->plateThickness;
        g.secFlangeThickness = prof->flangeThickness;
        g.sectionCenterOffset = profileSectionAnchorOffset(*prof, p.sectionAnchor);
    } else {
        switch (p.sectionAnchor) {
        case SectionAnchor::Top: g.sectionCenterOffset = -0.5 * g.secHeight; break;
        case SectionAnchor::Bottom: g.sectionCenterOffset = 0.5 * g.secHeight; break;
        case SectionAnchor::Center: g.sectionCenterOffset = 0.0; break;
        }
    }
    return g;
}

BridgeMemberBinding resolveBridgeMemberBinding(
    const Document& doc, const BridgeNativeMapping& mapping,
    const BridgeMemberSelection& selection) noexcept {
    BridgeMemberBinding out;
    std::vector<EntityId> pool;
    if (selection.primaryId!=kInvalidEntityId) pool.push_back(selection.primaryId);
    for (EntityId id:selection.secondaryIds) {
        if (id!=kInvalidEntityId &&
            std::find(pool.begin(),pool.end(),id)==pool.end()) pool.push_back(id);
    }
    // Preserve existing user selected members as authoritative candidates.
    // If both roles were selected in reverse order, compare actual member
    // direction to SOURCE ROLE directions (vertical column / beam axis).
    // No profile string comparison or source member-length requirement.
    const auto chooseRole=[&](const BridgeReconstructedMember& source)
        -> EntityId {
        double best=-1.0;
        EntityId chosen=kInvalidEntityId;
        for (const EntityId id:pool) {
            if (id==kInvalidEntityId) continue;
            std::string reason;
            if (!bridgeMemberCompatible(doc,id,source,reason)) continue;
            const double score=roleAlignment(doc,id,source);
            if (score>best+1e-8) {best=score;chosen=id;}
        }
        return chosen;
    };

    // Determine primary from user selection by geometric role if two+ picked;
    // if only one selected it is explicitly the primary slot.
    EntityId selectedPrimary=kInvalidEntityId;
    if(pool.size()==1) selectedPrimary=pool.front();
    else if(pool.size()>=2) {
        // Choose the pair with maximum role-axis alignment among selected
        // members; do not silently put one EntityId into two roles.
        double best=-1.0;
        for (EntityId p:pool) {
            std::string reason;
            if (!bridgeMemberCompatible(doc,p,mapping.primary,reason)) continue;
            double top=-1.0;
            for (const auto& srcSec:mapping.secondary) {
                for (EntityId q:pool) {
                    if(q==p || !bridgeMemberCompatible(doc,q,srcSec,reason)) continue;
                    top=std::max(top,roleAlignment(doc,q,srcSec));
                }
            }
            if(top<0) continue;
            const double score=roleAlignment(doc,p,mapping.primary)+top;
            if (score>best+1e-8) {best=score;selectedPrimary=p;}
        }
        if(selectedPrimary==kInvalidEntityId) selectedPrimary=chooseRole(mapping.primary);
    }

    std::vector<EntityId> used;
    const auto bind=[&](const BridgeReconstructedMember& source,
                        EntityId explicitId)->BridgeMemberBind {
        BridgeMemberBind b;
        b.guid=source.guid;
        if(explicitId!=kInvalidEntityId) {
            std::string reason;
            if(std::find(used.begin(),used.end(),explicitId)!=used.end() ||
               !bridgeMemberCompatible(doc,explicitId,source,reason)) {
                b.conflict=true;
                b.reason=reason.empty() ? "same member selected for both roles" : reason;
                return b;
            }
            b.resolved=true;b.entityId=explicitId;used.push_back(explicitId);
            return b;
        }
        // Explicit selection only: use remaining selected members for
        // secondary (closest axis to source role, never profile labels).
        EntityId picked=kInvalidEntityId;
        double best=-1.0;
        for(EntityId id:pool) {
            if (std::find(used.begin(),used.end(),id)!=used.end()) continue;
            std::string reason;
            if (!bridgeMemberCompatible(doc,id,source,reason)) continue;
            const double score=roleAlignment(doc,id,source);
            if(score>best+1e-8) {best=score;picked=id;}
        }
        if(picked!=kInvalidEntityId) {
            b.resolved=true;b.entityId=picked;used.push_back(picked);return b;
        }
        // No free selected role: fall back to already linked source GUID,
        // never general search of the entire Document.
        if(!source.guid.empty()) {
            const auto id=doc.entityForTeklaGuid(source.guid);
            if(id) {
                std::string reason;
                if(std::find(used.begin(),used.end(),*id)!=used.end()) {
                    b.conflict=true;b.reason="same Document member mapped to both roles";
                    return b;
                }
                if(!bridgeMemberCompatible(doc,*id,source,reason)) {
                    b.conflict=true;b.reason=reason;return b;
                }
                b.resolved=true;b.entityId=*id;used.push_back(*id);return b;
            }
        }
        b.reason="select a separate physical member for this role";
        return b;
    };

    out.primary=bind(mapping.primary,selectedPrimary);
    for(const auto& sec:mapping.secondary)out.secondary.push_back(bind(sec,kInvalidEntityId));

    const bool primaryOk=out.primary.resolved;
    const bool secondaryOk=std::any_of(out.secondary.begin(),out.secondary.end(),
        [](const BridgeMemberBind& b){return b.resolved;});
    bool conflict=out.primary.conflict;
    for(const auto& b:out.secondary)conflict|=b.conflict;
    out.ok=primaryOk && secondaryOk && !conflict;
    out.needsSelection=!out.ok;
    if(!primaryOk)out.missing.push_back("primary: "+out.primary.reason);
    for(const auto& b:out.secondary)
        if(!b.resolved || b.conflict)
            out.missing.push_back("secondary: "+b.reason);
    return out;
}

} // namespace mm
