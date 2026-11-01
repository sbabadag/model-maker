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
| `component_framework.hpp` | component çatı tipleri |
| `component_catalog.hpp` | katalog tipleri |

Ayrıca `libs/mm_contract/include/mm_contract/contract_version.hpp`:
`kContractVersion*`, `mm::contractSupports()`.

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
- `struct ComponentInput` — `{main, secondary[], params[]}`
- `struct PlateSpec` — poligon `outline`, pahlar, kalınlık, malzeme
- `struct GenerationPlan` — `{ok, message, plates[], bolts[], cuts[], welds[],
  roleKey'ler (plateKeys, boltKeys, ...)}`; `ok == false` ise belge değişmez

### Registry ve kayıt
- `struct ComponentCatalogMeta`, `struct ComponentType`
- `class ComponentRegistry` — tip + generator kaydı
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
