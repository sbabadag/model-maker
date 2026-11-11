// MALZEME LISTESI / RAPOR (P-2/5) — modeldeki uye, levha, bulon grubu ve kaynak
// satirlari. Saf fonksiyon: Document disinda hicbir duruma dokunmaz, GUI'den ve
// testten ayni sekilde cagrilir.
//
// Kural (P-1-STUDIO-PAKETLERI.md, P-2): paketten gelen levha/bulon/kaynak,
// yerlesik komponentlerle AYNI haklara sahiptir — rapor satirlarinda da ayrim
// yapilmaz; ayni geometri + ayni ozellik = tek satir, adet artar.
#pragma once

#include "model_maker/document.hpp"

#include <string>

namespace mm {

// Rapor satiri: ayni (ad, malzeme, posNo/olcu) imzasina sahip nesneler tek
// satirda toplanir; `count` adet sayisidir.
struct MaterialListRow {
    std::wstring key;      // gruplama anahtari (rapor satirinda gorunen ad)
    std::wstring detail;   // olcu / sinif / cap vb. (Turkce, mm)
    int count{0};
};

struct MaterialList {
    std::vector<MaterialListRow> members;      // profilli uyeler
    std::vector<MaterialListRow> plates;       // levhalar (komponent dahil)
    std::vector<MaterialListRow> boltGroups;   // bulon gruplari
    std::vector<MaterialListRow> welds;        // kaynaklar
    int memberCount{0};
    int plateCount{0};
    int boltCount{0};        // toplam bulon (grup x grid)
    int weldCount{0};
};

// Modelden listeyi cikarir. Gorsel/yardimci modeller (bulon montaji, kaynak
// katisi gibi) ve silinmis (dangling) varliklar SAYILMAZ.
MaterialList buildMaterialList(const Document& document);

// Kullaniciya gosterilecek Turkce rapor metni (sabit genislik, siralanmis).
std::wstring materialListReport(const Document& document);

} // namespace mm
