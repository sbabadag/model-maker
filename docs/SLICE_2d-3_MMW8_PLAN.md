# Slice 2d-3 Planı — MMW8 Kalıcılık (Component Parametrik Durum)

## Amaç
Component'lerin (End Plate + gelecekteki Base Plate / Fin Plate / Gusset / Splice)
TÜM parametrik durumunu MMW8'e kaydetmek ve eski belgeleri açarken geriye uyumlu
davranmak. Kaydedilen component tekrar PARAMETRIK düzenlenebilmeli (param değiştir
→ recompute → aynı geometri yeniden üretilir).

## Şu anki durum (zaten kalıcı)
- MMW8 'I' bloğu `ComponentInstanceRecord`'u serileştiriyor (Phase C2): id,
  typeId, schemaVersion, mainPart, secondaryParts, params (CompParam = name +
  variant<…>), origin/u/v/axis (Vec3), ownedPlateIds, ownedBoltGroupIds, dirty,
  orphan, enabled.
- 'E' bloğu EndPlateConnection, 'G' BoltGroup, 'C' CutModifier, 'B'/'V' B-Rep
  blobları zaten kalıcı.
- EntityProperties zaten sectionNormalX/Y/Z (MMW6) taşıyor.

## Eksikler (Slice 2d-3 kapsamı)
Aşağıdaki tipler HENÜZ ne ComponentInstanceRecord'da ne de MMW8'de:
1. `FaceReference` / `SemanticFaceRole` (reference_geometry.hpp) — kullanıcının
   "SemanticFaceReference" dediği şey: source (EntityId) + role (ComponentInputRole)
   + faceRole (SemanticFaceRole enum) + opsiyonel localFrame + selector (string).
2. `DistanceConstraint` (constraint_system.hpp): handle (string) + target
   (FaceReference) + signedOffset + enabled.
3. `MagneticFrame` / `ComponentLocalFrame` (magnetic_frame.hpp): MemberLocalFrame
   (origin + u/v/w) + children (MagneticChild: id string + LocalPlacement).
4. `SectionAnchor` (profile_database.hpp enum Top/Center/Bottom) — EntityProperties'e
   yeni alan + MMW versiyon kapısı.
5. `ConstraintSystem`'ın kalıcı tanım kısmı (handles + constraints + dependencies);
   solve() sonuçları (PositionSolveResult) kalıcı DEĞİL (her açılışta yeniden türetilir).

## Tasarım kararları

### A. ComponentInstanceRecord genişletme
- `ComponentLocalFrame frame;` ekle (MagneticFrame). Mevcut origin/u/v/axis
  Vec3'lerini DEĞİŞTİRİR; eski dosyalarda origin/u/v/axis'tan frame türetilir
  (legacy migration).
- `std::vector<FaceReference> faceRefs;` + `std::vector<DistanceConstraint>
  constraints;` + handle listesi ekle (component başına kalıcı kısıt tanımı).
- ownedPlateIds/ownedBoltGroupIds zaten var; MagneticChild.id (string) → EntityId
  eşlemesi kararlı alt nesne kimliği olarak tutulur.

### B. MMW8 blok stratejisi
- 'I' bloğunun İÇİNE alan eklemek yerine İLERİYE-UYUMLU ayrı bloklar (eski
  okuyucu bilinmeyen bloğu unknownBlocks'a koyup kayıpsız korur):
  - 'J' bloğu: FaceReference listesi (component id ile ilişkili).
  - 'D' bloğu: DistanceConstraint + Handle tanımları.
  - 'M' bloğu: MagneticFrame / ComponentLocalFrame (children dahil).
- Her blok "opsiyonel" etiketli: yoksa → o component legacy (frame origin/u/v/axis'tan,
  refs/kısıtlar boş) — recompute yine çalışır.

### C. Versiyonlama / migration
- ComponentInstanceRecord.schemaVersion ZATEN var; alignParamsToSpecs migrasyonu
  hazır. Yeni alanlar eski dosyada yoksa varsayılan değerle okunur.
- 'J'/'D'/'M' blokları 'I' bloğunun component id'siyle ilişkilendirilir (EntityId).
- Eski dosyada blok yoksa o component için boş (legacy) → magnetic frame
  origin/u/v/axis'tan türetilir; refs/kısıtlar boş kalır.

### D. SectionAnchor kalıcılığı
- EntityProperties'e `SectionAnchor sectionAnchor` alanı (default Top) + MMW
  versiyon kapısı (MMW7 ve öncesinde okunmaz → Top).
- sectionNormal ile AYNI desen: alan + default + versiyon + geriye uyumluluk testi
  (elle eski MMW dosyası yazıp yükle, Top'a düştüğünü doğrula).

### E. Re-editability (kritik şart)
- Kaydet: typeId + schemaVersion + params + refs + constraints + frame + owned
  sub-object ids TAMAMEN yazılır.
- Aç: typeId registry'de yoksa → orphan (parametreler KORUNUR, düzenleme kapalı,
  geometri korunur). Varsa → alignParamsToSpecs ile migrate + recompute mümkün.
- Test: component kaydet → aç → param değiştir → recompute → geometri değişikliği
  doğrulanır.

## Uygulama adımları (sıralı)
1. EntityProperties + MMW: SectionAnchor alanı + round-trip testi (section_anchor
   testi zaten var; MMW round-trip eklenir).
2. ComponentInstanceRecord + MMW: FaceReference, DistanceConstraint,
   ComponentLocalFrame alanları + 'J'/'D'/'M' blokları + round-trip.
3. Document add/set/remove component instance → yeni alanları da serileştirir.
4. Re-editability: load sonrası recompute akışı (registry lookup + migrate).
5. Backward-compat testleri: elle yazılmış eski MMW3/5/6/7 + yeni alanlarsız 'I'
   bloğu dosyaları.

## Riskler / tuzaklar
- variant (CompParamValue) serileştirmesi: tip etiketi + değer; wstring/EntityId
  ayrımı net olmalı.
- FaceReference'taki optional<MemberLocalFrame> + selector (string): opsiyonel alan
  yazımı (var/yok bayrağı).
- readMmw8 SIKI doğrular (bilinmeyen ZORUNLU sürüm fırlatır) — yeni bloklar
  "opsiyonel" etiketli olmalı ki eski dosyalar açılabilsin.
- Handle string anahtarları deterministik sıralanmalı (map yerine vector + sıralı
  yazım) — aksi halde round-trip kararsız.
- MagneticFrame u/v/w ortonormallik doğrulaması (isRigidLocalPlacement) yüklemede
  yeniden doğrulanmalı; bozuksa legacy fallback.

## Test stratejisi
- Core (OCC'siz) round-trip: her yeni blok için write → read → eşitlik.
- Mutation: alan yazılmazsa/yanlış yazılırsa yakalanır.
- Backward-compat: eski dosya → açılır → varsayılanlar (Top, boş refs, legacy frame).
- OCCT: component kaydet → aç → recompute → geometri eşitliği (end_plate_recompute_test
  genişletilir).
