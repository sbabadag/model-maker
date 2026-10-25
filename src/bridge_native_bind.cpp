// MODEL MAKER BRIDGE — native component uye baglama (uygulama).

#include "model_maker/bridge_native_bind.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

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

} // namespace

bool bridgeMemberCompatible(const Document& doc, EntityId id,
                            const BridgeReconstructedMember& m,
                            std::string& reason) noexcept {
    if (id == kInvalidEntityId || !doc.indexOfEntity(id)) {
        reason = "varlik yok";
        return false;
    }
    // YALNIZ profil (kesit/tur) uyumu kontrol edilir.
    // Eksen BOYU karsilastirilmaz: ayni fiziksel uye farkli modelde farkli uzunlukta
    // cizilmis olabilir (or. Tekla'da 7200 mm kolon, Model Maker'da 3382 mm cizilmis).
    // Baglanti geometrisi icin uyenin BOYU degil, KESITI ve KONUMU onemlidir; boy
    // karsilastirmasi dogru secimi yanlislikla reddediyordu.
    if (!m.profile.empty()) {
        const std::string p = toLower(entityProfile(doc, id));
        const std::string want = toLower(m.profile);
        if (!p.empty() && !want.empty() && p != want) {
            reason = "profil uyumsuz: belge=\"" + p + "\" beklenen=\"" + want + "\"";
            return false;
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

    // Kullanici secimlerini tek havuzda topla. SECIM SIRASI ONEMSIZ: her uye,
    // kendi hedefinin PROFILINE gore havuza eslestirilir (hea400->primary,
    // hea300->secondary), boylece kullanici yanlis sirada secse de dogru baglanir.
    std::vector<EntityId> pool;
    if (selection.primaryId != kInvalidEntityId) pool.push_back(selection.primaryId);
    for (EntityId id : selection.secondaryIds)
        if (id != kInvalidEntityId) pool.push_back(id);
    std::vector<bool> used(pool.size(), false);

    // Havuzdan, hedef uyeye profil olarak uyan ilk kullanilmamis varligi al.
    const auto takeFromPool = [&](const BridgeReconstructedMember& m) -> EntityId {
        for (std::size_t i = 0; i < pool.size(); ++i) {
            if (used[i]) continue;
            std::string reason;
            if (bridgeMemberCompatible(doc, pool[i], m, reason)) {
                used[i] = true;
                return pool[i];
            }
        }
        return kInvalidEntityId;
    };

    const auto bindOne = [&](const BridgeReconstructedMember& m) -> BridgeMemberBind {
        BridgeMemberBind b;
        b.guid = m.guid;
        // 1) Kullanici havuzundan profile uyan uye (sira onemsiz).
        const EntityId picked = takeFromPool(m);
        if (picked != kInvalidEntityId) {
            b.resolved = true;
            b.entityId = picked;
            return b;
        }
        // 2) Document'ta daha once iliskilendirilmis Tekla GUID.
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
        // 3) Kullanici secim yapti ama hicbir secim profile uymadi -> nedeni bildir.
        for (std::size_t i = 0; i < pool.size(); ++i) {
            if (used[i]) continue;
            std::string reason;
            if (!bridgeMemberCompatible(doc, pool[i], m, reason)) {
                b.conflict = true;
                b.reason = reason;
                return b;
            }
        }
        // 4) Cozulemedi (otomatik secim YAPILMAZ).
        b.reason = m.guid.empty() ? "uye GUID yok" : "GUID eslesmesi yok";
        return b;
    };

    out.primary = bindOne(mapping.primary);
    for (const auto& sec : mapping.secondary) out.secondary.push_back(bindOne(sec));

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
