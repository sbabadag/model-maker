# COMPONENT-PACKAGE-01 — Studio'dan Model Maker'a komponent paketi

**Tarih:** 1 Kasım 2026
**Kapsam:** `sbabadag/model-maker` (Hermes) + `sbabadag/model-maker-component-studio` (Claude Code)
**İnceleme tabanı (Model Maker):** `feature/drawing-hidden-lines-20261028` @ `2a89c51`. `main` 25 Ekim'de (`43a6b79`) kalmış; temel dal ayrıca netleştirilmeli (§12). (MM-1b sonrası temel dal: `feature/component-contract`.)
**Durum:** Tasarım önerisi — kod yok. Her iki tarafın onayından sonra görevlere bölünür.

---

## 0. Karar özeti

1. **Ortak sözleşme Model Maker'ın mevcut tipleridir.** `CompParamSpec`, `ComponentDialogSchema`, `GenerationPlan` yeniden icat edilmez.
2. **Komponent = veri paketi.** Parametre şeması, diyalog/şematik, recipe, preset ve altın testler JSON olarak taşınır.
3. **Recipe yorumlayıcısı ortak, OCCT'siz bir kütüphanededir.** Studio ve Model Maker aynı kütüphaneyi aynı commit'ten derler; aynı girdi için **birebir aynı** `GenerationPlan` üretilir.
4. **Recipe = ifadeler + yapı taşları.** Basit geometri ifadelerle, alana özgü karmaşık mantık (eğik kiriş, kolon tepesi kırpma vb.) sürümlü C++ yapı taşlarıyla yapılır. Eksik yapı taşı ender durumda çekirdeğe eklenir; komponentin kendisi için Model Maker yeniden derlenmez.
5. **Kabul kanıtı:** Mevcut `generateHaunch40` bir paket olarak yeniden ifade edilir ve yorumlayıcı çıktısı test girdilerinde C++ generator ile 1e-6 mm içinde eşleşir.

---

## 1. Mevcut durum

### 1.1 Model Maker'da olanlar (yeniden yazılmaz)

| Yetenek | Kaynak | Not |
|---|---|---|
| Tipli parametre şeması | `include/model_maker/component_engine.hpp` → `CompParamSpec`, `CompParamValue`, `alignParamsToSpecs` | Length/Angle/Integer/Boolean/Enum/String/Profile/Material/EntityRef; `optional` (Tekla "Default"), `documentedOnly` |
| Üretim sözleşmesi | `GenerationPlan` (`PlateSpec`, `BoltGroup`, `CutModifier`, `WeldSpec`) | Poligon kontur (`outline`), pahlar, **kararlı roleKey** (`plateKeys` …) |
| Registry | `ComponentRegistry`, `ComponentType`, `ComponentCatalogMeta` | Bugün tip + generator **C++ fonksiyonu** olarak kaydediliyor |
| Diyalog + şematik | `include/model_maker/component_dialog.hpp` | **Saf veri**: `DiagramPrimitive`, `DiagramHotspot` ↔ parametre, sekmeler, kaynak tablosu, canvas düzeni, `validateDialogSchema` |
| Canlı önizleme (mock view) | `component_live_preview_widget` | Generator'ı canlı çalıştırır, callout düzenleme, Iso/Front/Side/Top |
| Kalıcılık | `ComponentInstanceRecord`, `ComponentChildRecord`, MMW8 | roleKey → EntityId; `orphan` = tip registry'de yok |
| Referanslar | `geometry_reference_resolver.hpp`, `magnetic_frame.hpp` | OCCT'siz arayüz; fail-closed UNRESOLVED |
| Paket okuma deneyimi | `bridge_package.hpp` | Sürümlü JSON, `checksums.json` SHA-256 — **aynı desen burada da kullanılır** |
| Hazır tipler | `generateEndPlate`, `generateFinPlate`, `generateShearPlateSimple146`, `generateHaunch40` | Haunch 40 eğik mertek destekli |

`model_maker_core` (CMake) OCCT'siz; `component_engine.cpp` ve `component_dialog.cpp` bu kütüphanede. Materializer OCCT tarafında (`model_maker_occ`).

**Eksik olan tek kritik parça:** Tipler ve diyaloglar derlenmiş C++ fonksiyonları (`generateHaunch40()`, `haunch40DialogDefinition()`). Dışarıdan veri yükleme yok → yeni komponent = Model Maker'ı yeniden derlemek.

### 1.2 Studio'da olanlar (korunur)

- AI → tipli, doğrulanmış plan; tahmini koordinat ve sabit nesne korumaları (HOTFIX-03/04/05).
- OCCT tabanlı salt okunur inceleme (temas, taşma, açı, hacimsel çakışma).
- Mini CAD: gizmo, snap, sayısal dönüşüm, Undo/Redo, `.mmcomp` kaydı.

Studio'nun kendi `ComponentGraph`'ı sahne/yazarlık modeli olarak kalır; **paket çıktısı** Model Maker tipleriyle yazılır.

---

## 2. Hedef akış

```text
 STUDIO (yazarlık)                                   MODEL MAKER (kullanım)
 ─────────────────                                   ──────────────────────
 Türkçe tarif / CAD düzenleme
        │  AI: yalnız yapı taşı + parametre seçer
        ▼
 recipe + params + dialog  ──►  mm_component_contract  ◄──  paket yükleyici
        │                       (ortak, OCCT'siz, pinned)        │
        ▼                         • JSON codec                   ▼
 interpreter → GenerationPlan     • recipe interpreter     ComponentRegistry'ye
        │                         • yapı taşları             generic generator
        ▼                                                         │
 OCCT materialize + inceleme                                      ▼
 altın test üret                                     diyalog + canlı mock view
        │                                            + materializer + MMW8
        ▼
   .mmcpkg  ─────────────────────────────────────────────────►  yükle
```

---

## 3. Ortak çekirdek: `mm_component_contract`

**Sahibi Model Maker deposudur (Hermes).** Studio yalnızca tüketir, değiştirmez.

İçerik (hepsi OCCT'siz, `windows.h`'siz, Linux'ta test edilir):
- Veri tipleri: `CompParamSpec`, `CompParam`, `ComponentType`, `ComponentCatalogMeta`, `GenerationPlan` ve alt tipleri, `ComponentDialogSchema` ve alt tipleri, `MemberGeometry`, `ProfileSectionFrame`.
- JSON codec: yukarıdaki tiplerin kayıpsız gidiş-dönüşü.
- Recipe yorumlayıcısı ve yapı taşı kütüphanesi (§5).
- Paket doğrulayıcı (§4.4).

Paylaşım yöntemi:
- Model Maker CMake'te ayrı hedef olarak (`model_maker_core`'dan ayrıştırılarak) derlenir.
- Studio bunu **git submodule** olarak, belirli bir commit SHA'sına sabitlenmiş şekilde alır. Sürüm yükseltme bilinçli bir commit'tir.
- Çekirdek kendi `contractVersion`'ını (semver) dışa verir; paketler `minContractVersion` bildirir.

**Derleyici zinciri:** Model Maker CMake varsayılanı `C:/Qt/6.9.3/mingw_64`, Studio MSYS2 UCRT64. Çekirdek Qt'ye bağımlı olmadığı için engel değil; yine de iki uygulamanın aynı zincirde (öneri: UCRT64) derlenmesi hedeflenmeli (§12).

---

## 4. Paket formatı (`.mmcpkg`)

Klasör veya ZIP. Tüm metin dosyaları UTF-8 (BOM'suz), LF.

```text
haunch40.mmcpkg/
├── manifest.json        kimlik, sürümler, giriş rolleri, dosya listesi
├── parameters.json      CompParamSpec[]  (+ grup/sekme bilgisi dialog'da)
├── dialog.json          ComponentDialogSchema (şematik + hotspot + sekmeler)
├── recipe.json          bildirimsel üretim tarifi (§5)
├── presets.json         adlandırılmış parametre setleri (opsiyonel)
├── migrations.json      eski şema sürümlerinden dönüşüm (opsiyonel)
├── tests/               altın testler (§7)
│   └── *.json
├── assets/              şematik/sekme görselleri (opsiyonel, §8.3)
└── checksums.json       SHA-256 (bridge_package ile aynı desen)
```

### 4.1 `manifest.json`

```json
{
  "format": "mmcpkg",
  "formatVersion": "1.0.0",
  "minContractVersion": "1.0.0",
  "typeId": "mm_haunch_eaves",
  "schemaVersion": 1,
  "packageVersion": "1.0.0",
  "displayName": { "tr": "Mahya/saçak haunch", "en": "Eaves haunch" },
  "catalog": {
    "category": "Beam to Column",
    "primaryMemberType": "kolon",
    "secondaryMemberType": "mertek",
    "pickCount": 2,
    "catalogVisible": true,
    "coverageStatus": "IMPLEMENTED"
  },
  "inputs": [
    { "role": "main",        "kind": "member", "hint": "column" },
    { "role": "secondary_0", "kind": "member", "hint": "rafter"  }
  ],
  "author": { "tool": "component-studio", "toolSha": "<sha>" }
}
```

- `typeId`: `[a-z0-9_]+`, kalıcı. Yerleşik tiplerle çakışırsa paket **reddedilir**.
- `schemaVersion`: `ComponentType::schemaVersion` ile aynı anlam; değişirse `migrations.json` zorunlu.

### 4.2 `parameters.json`

`CompParamSpec`'in birebir JSON'u. Örnek:

```json
[
  { "name": "haunchLength", "type": "Length", "unit": "mm", "default": 450,
    "min": 80, "description": "Haunch boyu" },
  { "name": "upperBoltCountX", "type": "Integer", "default": 2, "min": 1, "max": 4 },
  { "name": "plateMaterial", "type": "Material", "default": "S235JR" },
  { "name": "grade", "type": "Enum", "default": "8.8", "choices": ["8.8", "10.9"] },
  { "name": "endPlateHeight", "type": "Length", "unit": "mm", "default": 0,
    "optional": true, "description": "0 = otomatik" }
]
```

### 4.3 `dialog.json`

`ComponentDialogSchema`'nın birebir JSON'u (items/primitives, hotspots, groups, weldRows, useTabs). `validateDialogSchema` yükleme anında çalışır; sorun varsa paket reddedilir.

### 4.4 Yükleme ve doğrulama (fail-closed)

Sıra: checksums → manifest → contract sürümü → parameters → dialog (`validateDialogSchema`) → recipe (statik analiz, §5.5) → altın testler. **Tek bir adım başarısızsa tip kaydedilmez**; Model Maker'daki mevcut örnekler `orphan` olarak işaretlenir (mevcut davranış), veri kaybolmaz.

---

## 5. Recipe dili

### 5.1 İlke

- **Bildirimsel, deterministik, yan etkisiz.** Dosya/ağ erişimi yok, rastgelelik yok, saat yok.
- Her işlem bir **roleKey** üretir (`[A-Za-z0-9_]`, paket içinde tekil) → `GenerationPlan` anahtarları.
- Koordinatlar her zaman bir **çerçeveye göre yereldir**. Dünya koordinatı yazılamaz.
- Hata → `GenerationPlan.ok = false` + Türkçe mesaj; belge değişmez (mevcut sözleşme).

### 5.2 İfadeler

```text
sayılar, + - * / ^, ( ), min max abs sqrt sin cos tan atan2, clamp,
if(koşul, a, b), karşılaştırmalar, and/or/not
param.<ad>                      parametre değeri
main.h main.b main.tw main.tf   kesit ölçüleri (MemberGeometry'den)
secondary_0.h …                 aynı
frame.<ad>.slope / .cos / .tan  çerçeve türetilmiş değerleri
```

Döngü yok. Tekrar, yalnızca sınırları sabit olan `grid`/`repeat` yapı taşlarıyla yapılır (üst sınır: §5.5).

### 5.3 Yapı taşları — v1 kataloğu

Taşlar C++'ta, çekirdekte, testli ve sürümlüdür. Mevcut generator'lardaki kanıtlanmış mantık **taşınarak** taşa dönüşür (yeniden yazılmaz).

| Taş | Çıktı | Kaynak mantık |
|---|---|---|
| `frame.contact(main, secondary)` | bağlantı çerçevesi: rol tespiti, temas noktası, kesit çerçeveleri, profil dönüşü | `resolveShearTabPlacement` |
| `frame.plumb_face(contact)` | eğik kirişte düşey kolon yüzü çerçevesi (`ax`, `tanT`, `cosT`) | `generateHaunch40` eğik blok |
| `frame.offset(f, du, dv, dw)` | ötelenmiş çerçeve | — |
| `frame.member_end(m, end)` | üye uç çerçevesi | `GeometryReferenceKind::MemberEnd` |
| `frame.bisector(a, b)` | iki üyenin açıortay düzlemi (apex) | yeni |
| `plate.rect(frame, w, h, t)` | `PlateSpec` | — |
| `plate.outline(frame, pts[], t)` | serbest kontur | `PlateSpec::outline` |
| `plate.chamfer(...)` | pah | `PlateSpec::Chamfer` |
| `plate.clip_to_member_top(p, member)` | kolon tepesinde kırpma (otomatik boyda) | Haunch 40 eaves kuralı |
| `bolts.grid(frame, nx, ny, px, py, d, hole, through[])` | `BoltGroup` | mevcut `BoltGroup` |
| `cut.plane(target, frame, keep)` | `CutModifier::PlaneCut` | mevcut |
| `weld.fillet(roleA, roleB, size, double)` | `WeldSpec` | mevcut |
| `stiffener.between_flanges(member, level, t, chamfer)` | kolon rijitleştiricisi | yeni (Studio CONN-02 hesapları) |
| `check(expr, mesaj)` | doğrulama | generator'lardaki `plan.ok=false` blokları |

### 5.4 Örnek (kısaltılmış)

```json
{
  "recipeVersion": "1.0.0",
  "let": {
    "W":  "max(secondary_0.b + 20, upperSpanX + param.boltEdgeLeft + param.boltEdgeRight)",
    "upperSpanX": "(param.upperBoltCountX - 1) * param.upperBoltPitchX"
  },
  "steps": [
    { "id": "c",   "use": "frame.contact",    "args": { "main": "main", "secondary": "secondary_0" } },
    { "id": "f",   "use": "frame.plumb_face", "args": { "contact": "c", "gap": "param.gap" } },
    { "check": "param.haunchDepth > param.haunchTipDepth + 25",
      "message": "Haunch derinliği uç derinliğinden en az 25 mm büyük olmalı" },
    { "role": "haunch_beam_end_cut", "use": "cut.plane",
      "args": { "target": "secondary_0", "frame": "f", "offsetW": "param.gap + param.endPlateThickness", "keep": "positive" } },
    { "role": "haunch_end_plate", "use": "plate.rect",
      "args": { "frame": "f", "w": "if(param.endPlateWidth > 0, param.endPlateWidth, W)",
                "h": "…", "t": "param.endPlateThickness", "material": "param.plateMaterial" } },
    { "role": "haunch_end_plate", "use": "plate.clip_to_member_top",
      "when": "param.endPlateHeight <= 0", "args": { "member": "main" } },
    { "role": "haunch_upper_bolts", "use": "bolts.grid",
      "args": { "frame": "f", "nx": "param.upperBoltCountX", "…": "…",
                "through": ["haunch_end_plate", "main"] } }
  ]
}
```

### 5.5 Statik analiz ve sınırlar

Yükleme anında:
- Bilinmeyen taş, bilinmeyen parametre, tanımsız `let`, döngüsel `let` → ret.
- roleKey tekilliği; aynı roleKey'in farklı türe bağlanması → ret (mevcut `ComponentChildTable` kuralı).
- Üst sınırlar: adım ≤ 256, bulon/grup ≤ 64, toplam bulon ≤ 512, kontur noktası ≤ 64, ifade derinliği ≤ 32.
- Taşlar `since` sürümü taşır; paket `minContractVersion`'dan yeni taş kullanamaz.

---

## 6. AI'ın rolü (Studio)

AI recipe yazmaz; **mevcut taşlardan seçer ve parametre doldurur.** Studio'daki kurallar aynen geçerli:
- Koordinat üretemez; yalnızca çerçeve + ifade + parametre.
- Her AI önerisi yorumlayıcıdan geçer → `GenerationPlan` → OCCT materialize → salt okunur inceleme → kullanıcı onayı.
- Desteklenmeyen istek (taş yok) → "unsupported" raporu; uydurma yok.

---

## 7. Altın testler (uyum kanıtı)

`tests/<ad>.json`:

```json
{
  "name": "inclined_10deg_auto_height",
  "inputs": {
    "main":        { "axisFrom": [0,0,0], "axisTo": [0,0,6000], "section": "HEA300", "profileRotation": 0 },
    "secondary_0": { "axisFrom": [150,0,5800], "axisTo": [6150,0,6858], "section": "IPE300" }
  },
  "params": { "haunchLength": 450 },
  "expect": {
    "ok": true,
    "planDigest": "sha256:…",
    "plates": { "haunch_end_plate": { "width": "<generateHaunch40 çıktısından>", "thickness": 20.0 } },
    "tolerance_mm": 1e-6
  }
}
```

- `planDigest`: `GenerationPlan`'ın kanonik JSON'unun SHA-256'sı (sayılar 1e-6'ya yuvarlanmış). Studio ve Model Maker **aynı digest'i** üretmeli.
- Model Maker yükleyicisi paketi kaydetmeden önce testleri çalıştırır; başarısızsa ret.

**Haunch 40 eşdeğerlik testi (kabul kriteri):** En az şu girdilerde paket çıktısı = `generateHaunch40` çıktısı (1e-6 mm, roleKey'ler dahil):
yatay; 5° ve 10° eğik; 90° döndürülmüş kolon; `endPlateHeight` elle verilmiş ve kolon tepesini aşan; üst/alt bulon grupları farklı.

---

## 8. Diyalog ve mock view

### 8.1 Parametre diyaloğu
Model Maker'ın mevcut dialog widget'ı `dialog.json` + `parameters.json`'dan çalışır; komponente özel kod yok.

### 8.2 Canlı mock view
`ComponentLivePreviewWidget` generator'ı `ComponentRegistry::generate` üzerinden çağırıyor, ancak bugün **`tekla_haunch_40` tipine sabit kodlu** (`component_live_preview_widget.cpp`, `findType(L"tekla_haunch_40")`; rijitleştirici "ghost" mantığı da Haunch 40'a özgü). MM-6'da widget `typeId` alacak şekilde genelleştirilir; Haunch 40'a özgü etkileşimler (ör. rijitleştirici aç/kapa) paketteki Boolean parametrelere ve roleKey'lere bağlanan genel bir mekanizmaya dönüşür. Temsilî girdi `manifest.json` → `"previewInput"` (bir altın test adı) ile seçilir.

### 8.3 Görseller
`assets/` yalnızca paketi yazanın kendi şematikleri için. Tekla diyalog ekran görüntüleri (bugün 146 canvas düzeninde kullanılan `:/tekla146/…`) Trimble'ın görselleridir; **dağıtılan paketlere konmamalı**. Paketler `DiagramPrimitive` tabanlı kendi şematiklerini kullanır.

---

## 9. Sürüm ve geçiş

- `packageVersion` (semver): yama = davranış değişmeyen düzeltme; minor = yeni opsiyonel parametre; major = geometri değişimi.
- `schemaVersion` artarsa `migrations.json`: ad değişikliği, varsayılan atama, birim dönüşümü (`migrateHaunch40Edges` gibi mantık bildirimsel olarak).
- Kayıtlı örnekler `typeId` + `schemaVersion` + `packageVersion` saklar. Daha yeni paket yüklenince örnek yeniden üretilir ve roleKey'ler korunur (mevcut `ComponentChildRecord` kuralı).

---

## 10. Görev dağılımı

### Model Maker (Hermes) — sahibi olduğu çekirdek

| # | Görev | Bağımlılık |
|---|---|---|
| MM-1 | `mm_component_contract` hedefini ayrıştır (OCCT'siz, Qt'siz), `contractVersion` | — |
| MM-2 | JSON codec: `CompParamSpec`, `ComponentDialogSchema`, `GenerationPlan` kayıpsız gidiş-dönüş + kanonik digest | MM-1 |
| MM-3 | Recipe yorumlayıcısı + statik analiz + v1 taşları (§5.3), mevcut generator mantığından taşıyarak | MM-2 |
| MM-4 | Haunch 40 paketini yaz; §7 eşdeğerlik testleri | MM-3 |
| MM-5 | Paket yükleyici: klasör tarama, doğrulama, generic generator ile registry kaydı, orphan davranışı | MM-3 |
| MM-6 | Diyalog + canlı önizleme paketten yüklenen tiplerle; önizleme widget'ını `tekla_haunch_40` sabitinden kurtar (§8.2); gerçek GUI testi | MM-5 |

### Studio (Claude Code) — tüketici ve yazarlık aracı

| # | Görev | Bağımlılık |
|---|---|---|
| ST-1 | Çekirdeği submodule olarak pinned SHA ile al, derle, testleri Studio CI'ına ekle | MM-1 |
| ST-2 | `GenerationPlan` → Studio sahnesi (OCCT materialize) + mevcut salt okunur inceleme | ST-1, MM-2 |
| ST-3 | Paket aç/kaydet; Studio'da yorumlayıcı önizlemesi | ST-2, MM-3 |
| ST-4 | AI: tarif → taş seçimi + parametre (koordinat yok); mevcut korumalar | ST-3 |
| ST-5 | Altın test üretici: sahnedeki girdilerden `tests/*.json` + digest | ST-3 |

### Mevcut Studio planıyla ilişki
- **CONNECTION-PLAN-01**'deki CONN-02/03/04 bu belgeyle **yeniden kapsamlanır**: rijitleştirici, bulon ve komponent modeli Studio'da ayrıca yazılmaz; çekirdek taşları olarak MM-3'e girer.
- CONN-01 `orient_profile_flange_toward`, Studio sahne yazarlığı için kalır; referans modeli Model Maker'ın `GeometryReference`'ı ile hizalanır.
- HOTFIX-06 bu karardan bağımsızdır; devam eder.

---

## 11. Kabul kriterleri (paket v1 tamam sayılır)

1. Haunch 40 paketi §7 eşdeğerlik testlerini Studio'da ve Model Maker'da **aynı digest** ile geçer.
2. Model Maker, paketi yeniden derleme olmadan yükler; diyalog, şematik, canlı önizleme, materialize, MMW8 kaydet/aç çalışır.
3. Bozuk paket (checksum, şema, bilinmeyen taş, başarısız altın test) yüklenmez; mevcut örnekler `orphan` olur, veri kaybolmaz.
4. Studio'da AI ile tarif edilen yeni bir varyant (ör. rijitleştiricili stiffened base plate) paket olarak dışa verilir ve Model Maker'da açılır.

---

## 12. Açık sorular (karar gerekli)

1. **Model Maker temel dalı:** `main` 25 Ekim'de kalmış; çalışma feature dallarında. Çekirdek hangi dalda ayrıştırılacak?
2. **Derleyici zinciri:** İki uygulama da UCRT64'e mi geçsin, yoksa çekirdek her iki zincirde mi test edilsin?
3. **Paket konumu:** Kullanıcı paketleri nerede dursun (`%APPDATA%\ModelMaker\components`, proje klasörü, ikisi)?
4. **İlk paket listesi:** Haunch 40'tan sonra sıra — stiffened base plate → apex haunch → purlin/rail conn → tube gusset önerilir.
5. **Tekla görselleri:** 146 canvas düzenindeki ekran görüntülerinin lisans durumu ayrıca değerlendirilmeli.
