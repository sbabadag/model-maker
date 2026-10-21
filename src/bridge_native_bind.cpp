// MODEL MAKER BRIDGE — native component uye baglama (uygulama).

#include "model_maker/bridge_native_bind.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace mm {

namespace {

std::string toLower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Varlik profil adi (bos olabilir).
std::string entityProfile(const Document& doc, EntityId id) {
    const auto idx = doc.indexOfEntity(id);
    if (!idx) return {};
    return doc.models()[*idx].properties().profileName;
}

// Varlik eksen uzunlugu (sifir ise eksen bilinmiyor).
double entityAxisLength(const Document& doc, EntityId id) {
    const auto idx = doc.indexOfEntity(id);
    if (!idx) return 0.0;
    const auto& p = doc.models()[*idx].properties();
    const double dx = p.axisToX - p.axisFromX;
    const double dy = p.axisToY - p.axisFromY;
    const double dz = p.axisToZ - p.axisFromZ;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

bool bridgeMemberCompatible(const Document& doc, EntityId id,
                            const BridgeReconstructedMember& m,
                            std::string& reason) noexcept {
    if (id == kInvalidEntityId || !doc.indexOfEntity(id)) {
        reason = "varlik yok";
        return false;
    }
    // Profil uyumu (case-insensitive). Reconstructed profil bos ise kontrol atlanir.
    if (!m.profile.empty()) {
        const std::string p = toLower(entityProfile(doc, id));
        const std::string want = toLower(m.profile);
        if (!p.empty() && !want.empty() && p != want) {
            reason = "profil uyumsuz: belge=\"" + p + "\" beklenen=\"" + want + "\"";
            return false;
        }
    }
    // Eksen uzunluk uyumu (varsa, %5 tolerans).
    if (m.hasAxis) {
        const double docLen = entityAxisLength(doc, id);
        const double wantLen = std::sqrt(
            (m.geometry.axisTo.x - m.geometry.axisFrom.x) *
                (m.geometry.axisTo.x - m.geometry.axisFrom.x) +
            (m.geometry.axisTo.y - m.geometry.axisFrom.y) *
                (m.geometry.axisTo.y - m.geometry.axisFrom.y) +
            (m.geometry.axisTo.z - m.geometry.axisFrom.z) *
                (m.geometry.axisTo.z - m.geometry.axisFrom.z));
        if (docLen > 0.0 && wantLen > 0.0) {
            const double tol = std::max(50.0, wantLen * 0.05);  // 5% + 50mm pay
            if (std::fabs(docLen - wantLen) > tol) {
                reason = "eksen boyu uyumsuz: belge=" + std::to_string(docLen) +
                         " beklenen=" + std::to_string(wantLen);
                return false;
            }
        }
    }
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

    const auto bindOne = [&](const BridgeReconstructedMember& m,
                             EntityId selectedId) -> BridgeMemberBind {
        BridgeMemberBind b;
        b.guid = m.guid;
        // 1) Acik kullanici secimi (varsa) — yine dogrula.
        if (selectedId != kInvalidEntityId) {
            std::string reason;
            if (!bridgeMemberCompatible(doc, selectedId, m, reason)) {
                b.conflict = true;
                b.reason = reason;
                return b;
            }
            b.resolved = true;
            b.entityId = selectedId;
            return b;
        }
        // 2) Document'ta daha once iliskilendirilmis GUID.
        if (!m.guid.empty()) {
            const auto prev = doc.entityForTeklaGuid(m.guid);
            if (prev) {
                std::string reason;
                if (!bridgeMemberCompatible(doc, *prev, m, reason)) {
                    b.conflict = true;
                    b.reason = reason;
                    return b;
                }
                b.resolved = true;
                b.entityId = *prev;
                return b;
            }
        }
        // 3) Cozulemedi (otomatik secim YAPILMAZ).
        b.reason = m.guid.empty() ? "uye GUID yok" : "GUID eslesmesi yok";
        return b;
    };

    out.primary = bindOne(mapping.primary, selection.primaryId);
    for (std::size_t i = 0; i < mapping.secondary.size(); ++i) {
        const EntityId sel = i < selection.secondaryIds.size()
                                 ? selection.secondaryIds[i]
                                 : kInvalidEntityId;
        out.secondary.push_back(bindOne(mapping.secondary[i], sel));
    }

    // Sonuc: primary + en az bir secondary cozulmeli, conflict olmamali.
    bool anyConflict = out.primary.conflict;
    bool primaryOk = out.primary.resolved;
    bool secondaryOk = false;
    for (const auto& s : out.secondary) {
        if (s.conflict) anyConflict = true;
        if (s.resolved) secondaryOk = true;
    }

    if (anyConflict) {
        out.ok = false;
        out.needsSelection = true;  // conflict -> kullanici netlestirmeli
        if (out.primary.conflict)
            out.missing.push_back("primary (\"" + out.primary.guid +
                                  "\") uyumsuz: " + out.primary.reason);
        for (const auto& s : out.secondary)
            if (s.conflict)
                out.missing.push_back("secondary (\"" + s.guid +
                                      "\") uyumsuz: " + s.reason);
    } else if (primaryOk && secondaryOk) {
        out.ok = true;
        out.needsSelection = false;
    } else {
        out.ok = false;
        out.needsSelection = true;
        if (!primaryOk)
            out.missing.push_back("primary (\"" + out.primary.guid +
                                  "\") cozulemedi: " + out.primary.reason);
        if (!secondaryOk)
            out.missing.push_back("secondary cozulemedi (uye secilmeli)");
    }

    return out;
}

} // namespace mm
