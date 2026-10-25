#pragma once
// MODEL MAKER BRIDGE — native component uye baglama (Phase C6).
//
// Tekla GUID'leri Model Maker EntityId'si DEGILDIR. Bu katman, Bridge V3
// paketinin primary/secondary uyelerini GERCEK Document uyelerine GUVENLI
// sekilde baglar:
//
//   Baglama onceligi:
//     1) Document'ta daha once iliskilendirilmis Tekla GUID varsa kullan;
//        nesne turu + geometri uyumunu dogrula.
//     2) Kullanici secimi (BridgeMemberSelection) varsa kullan; yine dogrula.
//     3) Belirsiz/celiski durumda OTOMATIK secim YAPMA — needsSelection doner.
//     4) Her iki uye (primary + en az bir secondary) cozulmeden tamamlanmis
//        sayilmaz (ok=false).
//
// OCCT'sizdir; Linux OCC-off ctest'te kosar. Yalnizca Document + profile katalogu
// + bridge_native_mapper bagimliliklari vardir. GERCEK materializasyon (generator
// + ComponentMaterializer) OCC-on katmanda (bridge_occ_adapter) yapilir.

#include "model_maker/bridge_native_mapper.hpp"
#include "model_maker/document.hpp"
#include "model_maker/profile_database.hpp"

#include <string>
#include <vector>

namespace mm {

// Kullanicinin acikca sectigi EntityId'ler (GUI'den). Bos = GUID map'e basvur.
struct BridgeMemberSelection {
    EntityId primaryId{kInvalidEntityId};
    std::vector<EntityId> secondaryIds;
};

// Bir uyenin baglanma durumu.
struct BridgeMemberBind {
    bool resolved{false};      // gecerli bir EntityId'ye baglandi
    bool conflict{false};      // GUID/eslesme var ama tip/geometri uyumsuz
    EntityId entityId{kInvalidEntityId};
    std::string guid;          // hedef Tekla GUID (bos = yok)
    std::string reason;        // conflict/unresolved aciklamasi (diagnostic)
};

// Baglama sonucu.
struct BridgeMemberBinding {
    bool ok{false};                    // tum zorunlu uyeler baglandi (conflict yok)
    bool needsSelection{false};        // kullanici secimi gerekli (unresolved var)
    BridgeMemberBind primary;
    std::vector<BridgeMemberBind> secondary;
    std::vector<std::string> missing;  // cozulemeyen GUID/rol aciklamalari
};

// Bir Document varliginin, yeniden olusturulmus uyeyle TIP + GEOMETRI uyumunu
// dogrular. Profil adi (case-insensitive) eslesmeli; eksen uzunlugu (varsa)
// %5 tolerans icinde eslesmeli. Uyumsuzlukta false + reason.
bool bridgeMemberCompatible(const Document& doc, EntityId id,
                            const BridgeReconstructedMember& m,
                            std::string& reason) noexcept;

// Bir Document varligindan generator girdisi MemberGeometry kurar: axis +
// refNormal + kesit bbox (profil katalogundan; yoksa 200x300 varsayilan) +
// SectionAnchor ofseti. OCC gerektirmez (Document + profil katalogu yeter).
MemberGeometry bridgeMemberGeometry(const Document& doc, EntityId id,
                                    const std::vector<SteelProfile>& catalog) noexcept;

// Bridge V3 paketi + native mapping sonucunu kullanarak primary/secondary
// uyelerini Document uyelerine baglar. selection bos ise yalnizca Document'taki
// Tekla GUID map'i kullanilir (setTeklaGuid ile kurulmus iliskiler). Belirsiz
// durumda otomatik secim YAPILMAZ; needsSelection=true + missing dolu doner.
BridgeMemberBinding resolveBridgeMemberBinding(
    const Document& doc, const BridgeNativeMapping& mapping,
    const BridgeMemberSelection& selection) noexcept;

} // namespace mm
