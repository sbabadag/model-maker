#pragma once
// MODEL MAKER BRIDGE — native component uye baglama (Phase C6).
//
// Tekla GUID'leri Model Maker EntityId'si DEGILDIR. Bu katman, Bridge V3
// paketinin primary/secondary uyelerini GERCEK Document uyelerine GUVENLI
// sekilde baglar:
//
//   Baglama:
//     1) Secilen gercek Model Maker tasiyici uyeleri rol eksenlerine gore ayir;
//        Tekla kaynak profil etiketini kesit zorunlulugu olarak KULLANMA.
//     2) Acik secim yoksa onceden kaydedilmis Tekla GUID link'lerini dene.
//     3) Ayni Document EntityId primary ve secondary olamaz.
//     4) Kolon ve kiris baglanmadan native 146 uretimi yapilmaz.
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

// Belgedeki gercek celik uyenin profil etiketinin ve fiziksel ekseninin
// mevcut oldugunu dogrular. Kaynak Tekla profil ADI veya eksen BOYU ile
// esitlik aranmaz; component secilen Document kesitlerine adapte olur.
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
