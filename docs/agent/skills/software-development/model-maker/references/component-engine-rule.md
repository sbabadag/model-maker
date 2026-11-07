# Component Engine Mimarisi (Phase C2) — KURAL

Yeni bir çelik birleşim ekleyen programcı, ortak elemanları (levha, bulon grubu,
kesme, kaynak) ve parametre altyapısını YENİDEN YAZMADAN component tipini sisteme
ekleyebilmeli. Bu kalıcı bir mimari kuraldır — eski `component.hpp` (ComponentModel,
isim-tabanlı, yalnız-double) yalnızca kendi testiyle kullanılır; yeni işler için
`component_engine.hpp/cpp` kullanılır.

## Katmanlar

1. TİPLİ PARAMETRE (CompParamSpec / CompParamValue): Length, Angle, Integer,
   Boolean, Enum, String, Profile, Material, EntityRef. Her parametre varsayılan +
   aralık (min/max) + birim + açıklama + enum seçenekleri taşır.
   - validateParamValue: tip zorlama + kelepçeleme + enum-geçersiz→varsayılan.
   - alignParamsToSpecs: eksik→varsayılan, bilinen→doğrula, BİLİNMEYEN→KORU
     (sessizce kaybetme; ileri uyumluluk için listenin sonuna eklenir).

2. REGISTRY / FACTORY (ComponentRegistry): her tip kalıcı TypeId (string) +
   SchemaVersion taşır. registerType(type, generator) ile kaydedilir. Yeni
   component = kendi TypeId + param şeması + bir `GenerationPlan generate(const
   ComponentInput&)` fonksiyonu. Document/renderer/serializer içine switch/case
   EKLEME — tip bilgisi registry'den okunur. instance() tek örnek; testler yerel
   registry kurar.

3. ÜRETİM PLANI (GenerationPlan): generator girdi olarak Main/Secondary
   MemberGeometry (id + eksen + kesit bbox) + parametreler alır; çıktı olarak
   PlateSpec / BoltGroup / CutModifier / WeldSpec specleri üretir. GERÇEK OCCT
   B-Rep burada YAPILMAZ — plan Document/OCC katmanında maddelestirilir
   (EntityId atanır, B-Rep üretilir, undo/MMW8). "Parametre üretimi" ile
   "gerçek geometri" ayrılır.

## Sahiplik kuralı

- Component kendi EntityId'sine sahiptir (ComponentInstanceRecord.id).
- Kullanıcı parçaları (main/secondary) SAHİPLENİLMEZ, yalnız EntityId ile
  referanslanır; component silinince bunlar korunur.
- Component'in ürettiği alt nesneler (levha, bulon grubu, delikler, kaynak)
  SAHİPLENİLİR; component silinince yalnız bunlar kaldırılır.
- Generator çıktısındaki BoltGroup.parts yalnız KULLANICI parçasını (ana parça)
  taşır; sahip olunan levha materializasyonda parts başına eklenir ({levha, kolon}).

## Doğrulama ölçütü (asıl başarı)

Yerleşik katalogda İKİ component var: End Plate (`beam_to_column_end_plate`) ve
Fin Plate (`fin_plate`). İkisi de AYNI registry + aynı parametre altyapısı + aynı
ortak elemanları kullanır; ikinci component eklenirken çekirdek yeniden yazılmadı.

## Document/MMW8 entegrasyonu (Phase C2b — TAMAM)

- MMW8 'I' blogu ComponentInstanceRecord'ı serileştirir (id, typeId, schemaVersion,
  mainPart, secondaryParts, tipli params, origin/u/v/axis, ownedPlateIds,
  ownedBoltGroupIds, enabled). `dirty`/`orphan` YAZILMAZ — yüklemede yeniden türetilir.
- CompParam değer etiketi: 0=double, 1=int, 2=bool, 3=wstring(utf8), 4=EntityId.
  wstring UTF-8 ile kodlanır (mmw8_codec.cpp anonim namespace: toUtf8/fromUtf8).
- Document::commitFromMmw8 yüklemede registry'ye (ComponentRegistry::instance())
  bakar: bilinmeyen typeId → orphan=true + params DOKUNULMAZ (bilgi kaybı yok);
  bilinen tip → orphan=false + alignParamsToSpecs (migration: eksik→varsayılan,
  bilinmeyen→KORUNUR) + schemaVersion = tipin güncel sürümü + dirty=true.
- Document API: addComponentInstance/removeComponentInstance/setComponentInstance
  (undo: ComponentAdd/Remove/Replace). Bu dilimde materializasyon YOK — owned alt
  nesne kimlikleri cagiran tarafından sağlanır.
- TUZAK: tests/run_occ_solid_ids.py CORE_SOURCES listesi document.cpp'nin
  bağımlılıklarını ELLE aynalar. document.cpp commitFromMmw8 artık
  component_engine.cpp + end_plate.cpp sembollerini kullanır; o listeye
  `src/component_engine.cpp` + `src/end_plate.cpp` eklendi. Yeni bir core->cpp
  bağımlılığı eklersen bu listeyi de güncelle (sessiz link hatası verir).

## Henüz bağlanmamış (sonraki dilim)

- Recompute/materializasyon: GenerationPlan -> levha/bulon/kesme üretimi + owned
  alt nesne kimliklerinin atanması; param değişimi / main move / secondary move /
  referans değişimi / BoltGroup değişimi → dirty işaretle, bağımlılık sırasına göre
  yalnız etkileneni hesapla, başarılıysa atomik tek Undo ile uygula.
- Ortak Qt özellik editörü: şemadan otomatik (elle yazılmış pencere YOK).
