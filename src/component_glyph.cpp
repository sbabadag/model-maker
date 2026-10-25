#include "model_maker/component_glyph.hpp"

#include <algorithm>

namespace mm {

namespace {

// Model sinir kutusunun merkezi; model yoksa nullopt.
std::optional<Vec3> modelCenter(const Document& document, EntityId id) {
    if (id == kInvalidEntityId) return std::nullopt;
    const auto index = document.indexOfEntity(id);
    if (!index || *index >= document.modelBounds().size()) return std::nullopt;
    const Bounds3& b = document.modelBounds()[*index];
    return Vec3{(b.minimum.x + b.maximum.x) * 0.5,
                (b.minimum.y + b.maximum.y) * 0.5,
                (b.minimum.z + b.maximum.z) * 0.5};
}

} // namespace

std::vector<ComponentGlyph> collectComponentGlyphs(const Document& document) {
    std::vector<ComponentGlyph> out;
    std::vector<EntityId> seen;

    const auto add = [&](EntityId entity, EntityId component, bool universal) {
        if (entity == kInvalidEntityId) return false;
        if (std::find(seen.begin(), seen.end(), entity) != seen.end()) return false;
        const auto center = modelCenter(document, entity);
        if (!center) return false;
        seen.push_back(entity);
        ComponentGlyph glyph;
        glyph.anchor = *center;
        glyph.entityId = entity;
        glyph.componentId = component;
        glyph.universal = universal;
        out.push_back(glyph);
        return true;
    };

    // 1) ALIN LEVHASI: mevcut production yolu. Levha merkezi = ankraj.
    for (const auto& conn : document.endPlateConnections()) {
        if (!conn.enabled) continue;
        add(conn.plateEntityId, conn.id, /*universal=*/false);
    }

    // 2) UNIVERSAL PARAMETRIK COMPONENT (Fin Plate vb.): sahip olunan levha
    //    varsa onun merkezi, yoksa bulon display merkezi.
    for (const auto& rec : document.componentInstances()) {
        if (!rec.enabled || rec.orphan) continue;
        bool added = false;
        for (const EntityId plate : rec.ownedPlateIds) {
            if (add(plate, rec.id, /*universal=*/true)) {
                added = true;
                break;
            }
        }
        if (added) continue;
        for (const EntityId boltGroup : rec.ownedBoltGroupIds) {
            const auto bi = document.indexOfBoltGroup(boltGroup);
            if (!bi) continue;
            if (add(document.boltGroups()[*bi].displayEntityId, rec.id,
                    /*universal=*/true))
                break;
        }
    }

    return out;
}

} // namespace mm
