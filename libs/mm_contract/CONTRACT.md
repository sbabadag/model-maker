# mm_component_contract — PUBLIC sözleşme yüzeyi

Bu belge, Studio ile Model Maker arasındaki ortak sözleşmenin (contract) hangi
imzalarını kapsadığını ve sürüm kurallarını tanımlar. `contract_version.hpp`
içindeki semver yalnız **aşağıda listelenen PUBLIC yüzeydeki değişiklikler** için
artar; listenin dışında kalan iç kod serbesttir.

## Sürüm kuralları

| Değişiklik | Sürüm etkisi |
|---|---|
| Davranış değişmeyen düzeltme (ABI/API aynı) | patch |
| Geriye uyumlu ekleme (yeni tip/alan/fonksiyon; mevcut imza bozulmaz) | minor |
| Kırıcı değişiklik (imza/alan kaldırıldı ya da semantiği değişti) | major |

Paketler `manifest.json` içinde `minContractVersion` bildirir; yükleyici
`mm::contractSupports()` ile uyumluluğu denetler.

## PUBLIC başlıklar (sürüm denetimli)

`include/model_maker/` altında; hiçbiri Qt, OCCT, `windows.h` veya `Document`'a
bağımlı değildir.

| Başlık | Kapsam |
|---|---|
| `entity_id.hpp` | `EntityId` (header-only) |
| `geometry.hpp` | `Vec2`, `Vec3`, `WorkPlane`, `Bounds3`, `ProfileSectionFrame` |
| `reference_geometry.hpp` | `FaceReference`, `GeometryReference` |
| `contact_resolver.hpp` | üye temas noktası/çerçevesi çözümü |
| `cut_modifier.hpp` | `CutModifier`, `CutModifier::PlaneCut` |
| `bolt.hpp` | `BoltGroup` |
| `constraint_system.hpp` | kısıt sistemi tipleri |
| `magnetic_frame.hpp` | `ComponentLocalFrame` |
| `end_plate.hpp` | `EndPlateParams`, `EndPlateConnection` |
| `component_engine.hpp` | parametre/üretim sözleşmesi (aşağıda) |
| `component_dialog.hpp` | diyalog şeması + doğrulama (aşağıda) |
| `component_codec.hpp` | JSON codec + kanonik + digest (aşağıda) |
| `recipe.hpp` | recipe yorumlayıcısı (aşağıda) |
| `haunch40.hpp` | Haunch 40 taşları + `HaunchParams` (aşağıda) |
| `component_framework.hpp` | component çatı tipleri |
| `component_catalog.hpp` | katalog tipleri |

Ayrıca `libs/mm_contract/include/mm_contract/`:
`contract_version.hpp` (`kContractVersion*`, `mm::contractSupports()`) ve
`component_visual.hpp` (levha görünüm kuralı: `kPlateGreyRgb`).

## Anahtar PUBLIC tipler ve fonksiyonlar

### Parametre şeması (`component_engine.hpp`)
- `enum class CompParamType` — `Length, Angle, Integer, Boolean, Enum, String,
  Profile, Material, EntityRef`
- `struct CompParamSpec` — ad, tip, birim, açıklama, varsayılan, aralık,
  `optional` (Tekla "Default"), `documentedOnly`
- `using CompParamValue = std::variant<double, int, bool, std::wstring, EntityId>`
- `struct CompParam` — `{name, value}`
- `paramDefaultValue`, `validateParamValue`, `alignParamsToSpecs`,
  `findParamSpec`, `findParam`, `paramAsDouble/Int/Bool/String/Entity`,
  `migrateHaunch40Edges`

### Üretim girdi/çıktısı
- `struct MemberGeometry` — eksen, kesit bbox, gövde/flanş kalınlıkları,
  `sectionCenterOffset`, `profileRotation`
- `memberSectionFrame(const MemberGeometry&)` — üyenin kesit çerçevesi
  (eksen + `refNormal` + `profileRotation`; dönüş uygulanır). MM-4'te public
  yapıldı (önceden TU-ici `rotateSectionFrame`).
- `struct ComponentInput` — `{main, secondary[], params[]}`
- `struct PlateSpec` — poligon `outline`, pahlar, kalınlık, malzeme
- `struct GenerationPlan` — `{ok, message, plates[], bolts[], cuts[], welds[],
  roleKey'ler (plateKeys, boltKeys, ...)}`; `ok == false` ise belge değişmez.
  `checks[]` (MM-7) **bağlayıcıdır (fail-closed)**: `{id, pass, value, limit,
  message}`; herhangi bir kontrol `pass == false` ise üretim reddedilir
  (`ok == false`, belge değişmez) ve ret nedeni `checks[]` ile raporlanır —
  kullanıcıya önizleme panelindeki checks tablosunda kırmızı satır, durum
  çubuğu ve log olarak görünür; OK ancak tüm kontroller geçince açılır.
  `checks[]` digest'e **girmez** (digest yalnız geometri), JSON'a boşsa yazılmaz.

### PlateSpec çerçevesi — el yönü ve köşe numaralama (`component_engine.hpp`)

Bir levhanın çerçevesi `{center, u, v, axis}` ile verilir. Kural (bağlayıcı):

1. **Genişlik `u` boyunca, yükseklik `v` boyunca** ölçülür (`width` × `height`).
2. **Kalınlık `axis` boyunca simetriktir**: levha `center` etrafında `±t/2`
   aralığını kaplar (extrüzyon yönü `+axis`). El yönü bu aralığı değiştirmez;
   iki elde de aynı hacim ve aynı dünya köşe noktaları çıkar.
3. **Köşe numaraları materializer nokta sırasına göredir**: kontur
   `0 = (−u,−v) → 1 = (+u,−v) → 2 = (+u,+v) → 3 = (−u,+v)` sırasında, saat
   yönünün tersi. `PlateSpec::Chamfer::corner` bu numaralandırmayı kullanır;
   `h` bacağı `u` boyunca, `v` bacağı `v` boyunca ölçülür.
4. **El yönü serbesttir**: `u × v = +axis` (sağ elli) zorunlu değildir; sol elli
   çerçeve (`u × v = −axis`) de aynı formülle yorumlanır. Materializer çerçeveyi
   "düzeltmez" (eksen takas etmez, `v` işaretini çevirmez) — yukarıdaki üç kural
   her iki elde de aynen geçerlidir.

Studio (mmcs) bu kuralı şöyle uygular: sol elli çerçevede sağ elli yerel ekseni
`x = u`, `y = −v`, `z = axis` alınır ve pah köşesi `3 − corner` olarak yeniden
numaralandırılır (0↔3, 1↔2). Dünya köşe noktaları Model Maker formülüyle aynıdır
(sapma < 1e-9 mm); `generationPlanDigest` iki tarafta da aynı çıkar.

Doğrulama testi: `tests/plate_frame_handedness_test.cpp` (sağ ve sol elli levha;
köşe noktaları, kalınlık yönü ve pah köşesi).

### Üye geometrisi — kanonik fonksiyon (1.8.0)

`memberGeometryFromProfile(endA, endB, connectionAtA, refNormal, profileRotationDeg, MemberSectionSpec)`
aynı fiziksel üyeyi, çağıranın hangi uçtan başladığından bağımsız **tek** bir
`MemberGeometry`'ye çevirir. Model Maker (`componentPreviewInput`) ve Studio
adaptörü bu fonksiyonu kullanır; ikinci bir dönüşüm yazılmaz.

Kurallar:

1. **Eksen yönü ve bağlantı ucu.** `axisTo` **daima bağlantı ucu**, `axisFrom`
   **daima uzak uç**tur. Çağıran hangi ucun bağlantı olduğunu `connectionAtA`
   ile bildirir (A = ilk argüman). `(A,B,true)` ile `(B,A,false)` **birebir aynı**
   `MemberGeometry`'yi verir; üye ters yazıldı diye plan digest'i değişmez.
2. **`refNormal`.** Eksene dikleştirilir (`n − (n·a)a`), normalleştirilir;
   dejenere ise dünya Z, o da dejenere ise dünya X kullanılır. Ardından
   **işareti kanonikleştirilir**: ilk sıfırdan farklı bileşeni pozitif olacak
   şekilde çevrilir. Böylece `n` ve `−n` aynı kesit çerçevesini verir (u/v
   ters dönmez, bulon `xDir` ters dönmez).
3. **`profileRotation`.** `[0,360)` aralığına indirgenir ve **kanonik eksene**
   göre yorumlanır. **Çift-simetrik kesitte** (I/H, `centerOffset == 0`) eksen
   etrafında 180° roll aynı fiziksel üyedir; dönüş `[0,180)` aralığına
   indirgenir (180 → 0, 270 → 90). **Asimetrik kesitte** (`centerOffset != 0`)
   180° roll ayna görüntüsü üyedir; `[0,360)` kalır. Böylece aynı fiziksel üye
   iki uygulamada farklı roll ile yazılsa da aynı `MemberGeometry` ve aynı
   digest'i verir; digest yön duyarlı değildir.
4. **Kesit.** `width/height/webThickness/flangeThickness` olduğu gibi taşınır.
   `centerOffset` ise refNormal isaret-kanoniklestirmesine bagli tasinir: isaret
   kanoniklestirme `n → −n` cevirdiginde kesit cercevesinin genislik VE yukseklik
   yonu ters doner (`height = axis × (n × axis)`); kesitin FIZIKSEL yerlesimini
   korumak icin `centerOffset` da negatiflenir. Bu olmadan egik kiriste (Top/Bottom
   ankraj) kesit bir tam yukseklik kadar yanlis tarafa kayar. (Dejenere `n ∥ axis`
   dalinda isaret kanoniklestirme YOK — cerceve zaten ham ile birebir, ofset aynen
   kalir.) Sıfır uzunluk ya da eksik kesit çağıranın reddedeceği bir girdidir:
   `memberGeometryUsable(g)` bunu söyler (fail-closed).
5. **Digest gevşetilmez.** Kanonikleştirme sadece aynı fiziksel üyenin tek
   gösterimini üretir; `generationPlanDigest` hesabı değişmez.
6. **Golden ve paket test girdileri kanonik olmak zorundadır (1.10.0).**
   Golden/test girdileri `MemberGeometry` alan atamasıyla DEĞİL,
   `memberGeometryFromProfile` üzerinden kurulur; aksi halde aynı fiziksel üye
   iki gösterimde farklı `MemberGeometry` (ve farklı digest) üretir. Denetim
   `isCanonicalMemberGeometry(g)` ile yapılır: `g`, `memberGeometryFromProfile`
   tarafından aynı girdiyle birebir yeniden kurulabilirse kanoniktir (id alanı
   denetim dışıdır). Golden testleri ve paket yükleyicinin test girdisi replay'i
   kanonik olmayan girdiyi açık mesajla REDDEDER (sessizce kanonikleştirmez).
   Kanonik kural özeti: `axisTo` bağlantı ucu, `axisFrom` uzak uç; dönüş çift
   simetrikte `[0,180)`, asimetrikte `[0,360)`.
7. **Document'ten kurulan girdide bağlantı ucu geometriden (E / secenek C).**
   `componentInputFromDocument` **ikincil** üyenin bağlantı ucunu çizim
   yönünden değil **geometriden** belirler: iki uçtan `main` üyenin eksen
   doğrusuna (kesit sınır kutusuna) **daha yakın olan uç** bağlantı ucudur
   (`connectionAtA` buna göre kurulur). Böylece aynı kiriş iki çizim yönüyle
   (axisFrom/axisTo yer değiştirmiş) aynı `MemberGeometry` ve aynı digest'i
   verir; Studio OCCT ölçümüyle aynı kuraldır. İki uç tolerans içinde eşit
   uzaksa (belirsiz) **açık ret** (`nullopt` + hata mesajı) — tahmin yok.
   **main** (kolon) için eksen yönü kuralı korunur: `axisTo` bağlantı ucu
   (ikincil üyenin çizim yönünden bağımsız).

Kabul testi: `mm_contract_member_geometry_canonical_test` — aynı üye iki farklı
uç sırasıyla verildiğinde aynı `MemberGeometry` **ve** aynı `planDigest`;
`n`/`−n`, eğik `n`, 180°/−90°/360° dönüş, `n ∥ axis` ve sıfır uzunluk durumları.

### Levha görünüm kuralı (`mm_contract/component_visual.hpp`)

Levhalar (Plate) **her zaman gridir**: `kPlateGreyRgb = RGB(170,170,170)`
(`0xAAAAAA`). Tek kaynak budur; Studio aynı sabite bağlanır. Komponent
önizlemesi, canlı mock view ve modelde geçerlidir; seçim/hover vurgusu bu rengin
**üstüne** çizilir (levha rengini değiştirmez). Profiller ve bulonlar mevcut
renklerini korur.

### Registry ve kayıt
- `struct ComponentCatalogMeta`, `struct ComponentType`
- `class ComponentRegistry` — tip + generator kaydı + `unregisterType` (1.7.0: paket yükleyicinin "kısmi kayıt yok" geri alması)
- `struct ComponentChildRecord`, `struct ComponentInstanceRecord`
- Generator'lar: `endPlateComponentType()`, `generateEndPlate()`,
  `finPlateComponentType()`, `generateFinPlate()`,
  `shearPlate146ComponentType()`, `generateShearPlateSimple146()`,
  `haunch40ComponentType()`, `generateHaunch40()`, `registerBuiltInComponents()`

### Diyalog şeması (`component_dialog.hpp`)
- `struct DiagramPrimitive`, `struct DiagramHotspot`, `struct DiagramPoint`
- `struct ComponentDialogSchema` — primitives, hotspots (parametre bağlantısı),
  gruplar, sekmeler, weld satırları
- `struct DialogSchemaValidation` — `{bool ok, ...}`
- `validateDialogSchema(schema, specs)`, `hitTestDiagramHotspot(schema, p)`

### JSON codec (`component_codec.hpp`) — MM-2
- `kCodecFormatVersion` — codec format sürümü ("formatVersion" olarak yazılır)
- `utf8FromWide`, `wideFromUtf8` — wstring ↔ UTF-8 (16/32-bit wchar_t portable)
- `compParamSpecToJson/FromJson`, `compParamToJson/FromJson` — parametre şeması/değer
- `componentCatalogMetaToJson/FromJson`, `componentTypeToJson/FromJson`
- `componentDialogSchemaToJson/FromJson` — items/primitives, hotspots, groups,
  weldRows, canvas alanları
- `generationPlanToJson/FromJson` — PlateSpec (outline/chamfer), BoltGroup,
  CutModifier, WeldSpec, roleKey dizileri
- `canonicalJsonString` — sıralı anahtar, 1e-6 yuvarlama, sabit biçim, -0→0, UTF-8
- `generationPlanCanonicalJson`, `generationPlanDigest` — kanonik metin + SHA-256
  hex (planDigest); Linux GCC == Windows mingw (deterministik)

**Digest kimlik-bağımsızdır (MM-2b, 1.9.0).** `generationPlanDigest` yalnız
GEOMETRİ eşdeğerliğini ölçer: plandaki üye referansları EntityId yerine
**semantik rol anahtarı** ile hash'lenir — `WeldSpec.partA/partB` → `partARole/
partBRole` ("main"/"secondary_N"/plateKey), `BoltGroup.parts` → `partRoles`,
`CutModifier.target` → `targetRole`. Malzemeleşme-zamanı kimlikleri (bolt `id`,
`displayEntityId`, `shapeParts`, cut `tool/source/owner`) digest'e hiç girmez.
Böylece aynı fiziksel girdi, Studio'da ve Model Maker'da farklı EntityId ile de
aynı `planDigest`'i verir. (Yeni alanlar: `BoltGroup.partRoles`,
`CutModifier.targetRole` — geriye uyumlu ekleme.)

Codec kuralları: enum'lar string; bilinmeyen alanlar hata vermez; wstring ↔ UTF-8
kayıpsız; digest 1e-6'ya yuvarlanmış kanonik JSON üzerinden hesaplanır.

### Recipe yorumlayıcısı (`recipe.hpp`) — MM-3
- `struct Recipe`, `struct RecipeStep`, `struct RecipeArg` — bildirimsel üretim
  tarifi (let bağlamaları + blok/check adımları)
- `validateRecipe(recipe, specs)` — statik analiz (bilinmeyen blok/parametre/let,
  döngüsel let, roleKey tekilliği + tür tutarlılığı, üst sınırlar)
- `runRecipe(recipe, specs, input)` — recipe → `GenerationPlan`; fail-closed
  (hata → `ok=false` + Türkçe mesaj)
- v0 taşları: `frame.member_end`, `frame.offset`, `frame.rotate`, `plate.rect`,
  `plate.polygon`, `bolts.grid`, `weld.fillet`, `cut.plane`, `endplate.placement`,
  `check`. (`frame.rotate`, `plate.polygon`, `plate.rect.chamfers`,
  `bolts.grid.grip/holeDepth` 1.7.0'da eklendi; gerekçe: taban levhası
  rijitleştiricileri ve ankraj bulonları.)
- İfade dili: `+ - * / ^`, `min max abs sqrt sin cos tan atan2 clamp if`,
  karşılaştırma, `and/or/not`, `param.*`, `main.*`, `secondary_N.*` (h,b,tw,tf,length).
  Şema/semantik: `libs/mm_contract/RECIPE.md`.

### Haunch 40 taşları (`haunch40.hpp`) — MM-3c
- `struct HaunchParams` — generateHaunch40'ın input.params'tan okuduğu TÜM
  parametreler (açık alanlar; gizli okuma yok).
- `struct Haunch40Group` — tek bulon grubunun ham parametreleri.
- `haunchParamsFromInput(input)` — `HaunchParams`ı input.params'tan doldurur
  (generator davranışı birebir).
- `runHaunch40Block(block, params, input, state&, plan)` — 8 blok:
  `frame.contact`, `frame.plumb_face`, `haunch.end_plate`, `haunch.web`,
  `haunch.flange`, `haunch.stiffeners`, `bolts.haunch_groups`, `haunch.welds`.
  `state` opaktır (`std::shared_ptr<void>`), adımlar arasında taşınır.
  Parametreler `HaunchParams` üzerinden gelir; aritmetik generateHaunch40 ile
  birebir, `planDigest` değişmez.
- Paket: `libs/mm_contract/packages/haunch40/recipe.json` (5 golden girdide
  `digest(runRecipe) == digest(generateHaunch40) == golden`).
- **Haunch'ın "alt tarafı" yerçekimine bağlıdır (MM-4b).** `generateHaunch40`
  haunch'ı (gövde/alt flanş) ve üst/alt bulon gruplarını kesit çerçevesinin
  işaretine veya `profileRotation`'a göre DEĞİL, dünya-yukarı (`worldUp`)
  vektörüne göre yerleştirir; kesit çerçevesinin yükseklik yönü aşağı bakarsa
  çerçeve (genişlik+yükseklik) ters çevrilir ki `−height` hep aşağıyı göstersin.
  Böylece simetrik kirişin 180° roll'ü planı değiştirmez; asimetrik kesitte de
  haunch yerçekimine göre altta kalır.

## Contract istekleri (Studio) — sürüm günlüğü

Her istek AYRI sürüm + AYRI commit. Kural: **mevcut planDigest'ler değişmez**
(yeni alanlar varsayılan değerdeyken kanonik JSON'a ve dolayısıyla digest'e
GİRMEZ).

### 1.12.0 — (a) `manifest.json` kanonik anahtar sırası

`manifest.json` (ve diğer paket JSON'ları) `packageJsonText` ile yazılır;
nlohmann `json` nesnesi `std::map` tabanlıdır → anahtarlar **alfabetik** sırada
serileşir. Depodaki örnek paket (`libs/mm_contract/packages/end_plate_pkg`)
bu kurala uymuyordu (`minContractVersion` yanlış yerdeydi); yeniden üretildi.
Paket yükleyici anahtar sırasına DUYARLI DEĞİLDİR (sıra yalnız bayt-aynı
üretim ve diff gürültüsü içindir).

### 1.13.0 — (b) `writePackageJsonFile`: geniş/UTF-8 yol + sonda tek LF

- Yeni imza `bool writePackageJsonFile(const std::filesystem::path&, const std::string&)`
  TERCİH EDİLİR; Windows'ta yerel geniş yol kullanılır, ANSI kod sayfası devrede
  değildir → Türkçe karakterli klasör adı (`...	ürkçe\...`) kırılmaz.
  Eski `const std::string&` imzası KORUNUR: yol UTF-8 kabul edilir.
- Metin **sonda tek LF** ile bitmek zorunda: CR içeren, LF ile bitmeyen ya da
  çift LF ile biten metin YAZILMAZ (`false`). Böylece paket dosyalarında
  biçim bozulması yazma anında yakalanır.
- Test: `contract_requests_test` bölüm (b) — geçici klasör adı Türkçe
  karakterli; geçerli yazım + üç ret durumu.

### 1.14.0 — (c) `validateRecipeJsonHeader` PUBLIC

`bool validateRecipeJsonHeader(const nlohmann::json&, std::wstring& error)` artık
`model_maker/component_package.hpp` içinde **PUBLIC**. Studio aynı doğrulayıcıyı
dışa aktarmadan ÖNCE koşar (tek kaynak, iki taraf aynı kuralı uygular):
`recipeVersion` zorunlu + semver + major **1**; `let` nesnesi; adım argümanları
metin ifade ya da sayı (JSON `true/false` REDDEDİLİR). Ret mesajı Türkçedir.

### 1.15.0 — (g) yinelenen `roleKey` + birbirini dışlayan `when`

Bir `roleKey` artık **birden fazla adımda** kullanılabilir; koşul:
adımların `when` ifadeleri **boş olmayacak ve farklı** olacak (pahlı/pahsız
alternatifler). Kurallar:

- `when` boş ya da iki adımda AYNI → **statik RED** (`yinelenen roleKey`).
- Farklı `when` → statik KABUL.
- Çalışma anında **ikisi birden üretilirse** plan REDDEDİLİR:
  `roleKey iki kez üretildi (when kosullari dislayici degil): <roleKey>`.
  Denetim levha, bulon, kesim ve kaynak roleKey listelerinin her biri için
  yapılır; sessiz çift nesne üretilmez.
- `roleKey` yine tür değiştiremez (`roleKey farkli ture baglandi`).

### 1.16.0 — (d) `frame.member_end {end: lower|upper}` (HR-12)

`end` argümanı dünya koordinatına göre de seçilebilir:

| değer | anlam |
|---|---|
| `start` / `from` | `axisFrom` ucu (eski davranış, değişmedi) |
| `end` / `to` | `axisTo` ucu (eski davranış, değişmedi) |
| `lower` | dünya **Z**'si küçük olan uç |
| `upper` | dünya **Z**'si büyük olan uç |

Gerekçe: yukarıdan aşağı çizilmiş kolonda `axisFrom` üstte kalır; taban levhası
`lower` ile doğru uca gider. Başka bir metin AÇIK ret ile reddedilir
(`gecersiz end '<x>' (start|from|end|to|lower|upper)`).

### 1.17.0 — (i) `frame.member_face {face: ...}` (HR-16)

`frame.member_face` artık yüzü **adıyla** seçebilir; `toward` gerekmez:

| `face` | normal | ofset | `u` |
|---|---|---|---|
| `web_pos` / `web_neg` | üyenin kesit **genişlik** yönü ± | gövde kalınlığı/2 | yükseklik yönü |
| `flange_pos` / `flange_neg` | üyenin kesit **yükseklik** yönü ± | kesit yüksekliği/2 | genişlik yönü |

- `u` = ilgili kesit ekseni, `v` = üye ekseni, `w` = yüz normali. Normal, üyenin
  KENDİ kesit çerçevesinden gelir (dünya yönüne göre değil) → döndürülmüş
  profilde de doğru yüz.
- `at` (sayı, mm, varsayılan 0): eksen boyunca `axisFrom`'dan uzaklık; çerçeve
  oraya taşınır.
- `face` ile `toward` **birlikte verilemez** (belirsizlik → açık ret). Geçersiz
  `face` değeri reddedilir: `(flange_pos|flange_neg|web_pos|web_neg)`.

## Sürüm denetimi DIŞINDA olanlar (serbest)

- `.cpp` iç kod (header'daki imzayı değiştirmediği sürece)
- `component_glyph.hpp/.cpp` (Document'a bağlı; contract'a girmez)
- Qt widget'lar (`component_dialog_widget.cpp`, `component_live_preview_widget.cpp`)
- OCCT tarafı (`component_materializer.cpp`, `occ_geometry.cpp`, ...)
- Win32 uygulama katmanı (`application.cpp`, `renderer.cpp`, `qt_main_window.cpp`)

## Kural

Yukarıdaki "Anahtar PUBLIC tipler ve fonksiyonlar" listesindeki **bir imza/alan
değişirse** minor veya major artar (ekleme minor, kırma major). Bu listedeki bir
işlevin yalnızca gövdesi değişirse (imza aynı) patch yeterlidir. Listede olmayan
her şey serbestçe değişebilir.
