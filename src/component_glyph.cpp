#include "model_maker/component_glyph.hpp"

#include "model_maker/color_contrast.hpp"

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

// FNV-1a (32-bit) — wchar_t'nin Unicode kod noktasi uzerinden. wchar_t boyutu
// platforma gore degisse de DEGER (kod noktasi) aynidir; bu yuzden hash Linux
// ve Windows'ta birebir ayni cikar (renkler tutarli kalir).
std::uint32_t fnv1a(const std::wstring& s) noexcept {
    std::uint32_t h = 2166136261u;
    for (const wchar_t ch : s) {
        h ^= static_cast<std::uint32_t>(ch);
        h *= 16777619u;
    }
    return h;
}

} // namespace

std::vector<ComponentGlyph> collectComponentGlyphs(const Document& document) {
    std::vector<ComponentGlyph> out;
    std::vector<EntityId> seen;

    const auto add = [&](EntityId entity, EntityId component, bool universal,
                         const std::wstring& typeId) {
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
        glyph.typeId = typeId;
        out.push_back(glyph);
        return true;
    };

    // 1) ALIN LEVHASI: mevcut production yolu. Levha merkezi = ankraj.
    for (const auto& conn : document.endPlateConnections()) {
        if (!conn.enabled) continue;
        add(conn.plateEntityId, conn.id, /*universal=*/false,
            L"beam_to_column_end_plate");
    }

    // 2) UNIVERSAL PARAMETRIK COMPONENT (Fin Plate vb.): sahip olunan levha
    //    varsa onun merkezi, yoksa bulon display merkezi.
    for (const auto& rec : document.componentInstances()) {
        if (!rec.enabled || rec.orphan) continue;
        bool added = false;
        for (const EntityId plate : rec.ownedPlateIds) {
            if (add(plate, rec.id, /*universal=*/true, rec.typeId)) {
                added = true;
                break;
            }
        }
        if (added) continue;
        for (const EntityId boltGroup : rec.ownedBoltGroupIds) {
            const auto bi = document.indexOfBoltGroup(boltGroup);
            if (!bi) continue;
            if (add(document.boltGroups()[*bi].displayEntityId, rec.id,
                    /*universal=*/true, rec.typeId))
                break;
        }
    }

    return out;
}

std::uint32_t componentTypeColor(const std::wstring& typeId) noexcept {
    // Bilinen yerlesik tipler icin sabit, ayirt edici palet renkleri (0xRRGGBB).
    if (typeId == L"beam_to_column_end_plate") return 0x4285F4u; // mavi
    if (typeId == L"fin_plate") return 0x34A853u;                // yesil
    if (typeId == L"tekla_shear_plate_146") return 0xF4A000u;    // turuncu
    if (typeId == L"tekla_haunch_40") return 0xAB47BCu;          // mor
    // Bos tip -> turkuaz (eski glif rengi, geriye uyumlu gorunum).
    if (typeId.empty()) return 0x00A8C4u;
    // Bilinmeyen / ozel (kullanici katalogu) tipler: deterministik hash -> ton.
    const double hue = static_cast<double>(fnv1a(typeId) % 360u);
    return color_contrast::hslToRgb(hue, 0.70, 0.55);
}

} // namespace mm
