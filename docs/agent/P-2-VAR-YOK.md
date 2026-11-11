# P-2 — VAR / YOK listesi

Planlayıcı P-2 maddesinin ilk adımı: "Önce 'var / yok' listesi, sonra her eksik
ayrı commit + kapı + GUI senaryosu." Bu belge, `P-1-STUDIO-PAKETLERI.md` → P-2
bölümündeki "taş → MM nesnesi" tablosunun **kanıtlı** durumudur.

Kanıt sütunu: `dosya:satır` kod kanıtı, `test` adı kayıtlı test kanıtı,
`GT` GUI koşumu. **"kod kanıtı"** = davranış kodda var, ayrı testi yok.

## Taş → MM nesnesi

| # | Recipe çıktısı | Olması gereken | Durum | Kanıt |
|---|---|---|---|---|
| 1 | `plate.rect` (+`chamfers`) | levha parçası: gri, pah konturu, malzeme/ad/posNo; seçilir, listelenir | **VAR** | `src/component_materializer.cpp:177-215` (pah konturu), `:45` `kPlateColor` gri, `:52-64` name/material/finish/posNo→description · test: `component_plate_color_test`, `component_materializer_test` |
| 2 | `plate.polygon` | aynı; kontur levhası | **VAR** (P-2/1) | `src/recipe.cpp` `blockPlatePolygon` (sınır kutusu `width/height`), `src/component_materializer.cpp:165-175` poligon yüzü · test: `studio_package_golden_test` bölüm 2 (26 PASS, `tests/golden/studio_base_plate.json` `92134f6a…c85e`) · GT: `t25` `ok=true` 17 model |
| 2b | `plate.polygon` — **dışbükey olmayan** kontur | sınır kutusu + poligon; desteklenmiyorsa açık RED | **VAR** (P-2/4) | `component_materializer_test` "icbuke (L) kontur": `outline` 6 nokta, `width=height=300`, ölçülen hacim **675000 mm³** (= (300×300 − 150×150) × 10), ters sarımda da aynı; sessiz boş katı yok |
| 3 | `bolts.grid` (through üyeler) | bulon grubu: kalite, delik, grip; üyelerde ve levhalarda delik açar | **VAR** | `src/bolt.cpp:223` `boltGroupHoleModifiers`, `src/document.cpp:390,2662,2718,2785` delik zinciri, `src/component_materializer.cpp:903` (levha delikleri + kesmeler) · test: `component_materializer_test`, `component_bolt_rows_reduced_test` |
| 4 | `bolts.grid` ankraj (`through: []`, aşağı) | üyeye delik AÇMAZ; yalnız taban levhasında delik | **VAR** (P-2/2) | `component_anchor_bolt_test` A: ankraj grubunun `parts`'ında üye YOK → `boltGroupHoleModifiers` yalnız levhaları hedefler, kolonun B-Rep sürümü değişmez (delik açılmaz); B: normal grup `parts={main}` → ana üyede delik modifier'ı VAR |
| 5 | `weld.fillet` | kaynak nesnesi: görünür, seçilir, boyut ve iki taraf doğru | **VAR** | `src/component_materializer.cpp:846,1224` (`doubleSided`), `:47` `kWeldColor`; kaynak katısı + display modeli, desteklenmiyorsa `unsupportedReason` (sessiz yanlış katı yok) · test: `component_materializer_test` |
| 6 | `cut.plane` (fitting dahil) | üye katısı kesilir; kesim nesnesi görünür ve seçilir | **KISMI** | `src/recipe.cpp:1844` `cut.plane` bloğu, `:1368` hedef çözümü; kesme uygulanıyor · kopyalanan kesim düzlemi çizilmiyor (INBOX B8, açık) → P-2/3 |
| 7 | `check` başarısız | komponent kurulmaz; Türkçe mesaj; belge değişmez | **VAR** | `src/application.cpp` `recomputeComponentInstancesFor` (`prepWhy`, `CR-PREP-FAIL`) + `atomicRecompute` rollback · test: `component_recompute_atomic_test`, `component_move_test` bölüm B (ana üye yok → hazırlık REDDEDER, belge değişmez), GT `t23` (iki dal, 23 adım, `ok=true`) |
| 8 | Malzeme listesi / rapor satırları (levha, bulon, kaynak) | raporda üç satır türü | **VAR** (P-2/5) | `include/model_maker/report.hpp` + `src/report.cpp`: `mm::buildMaterialList` / `materialListReport` (saf fonksiyon) → ÜYELER / LEVHALAR / BULON GRUPLARI / KAYNAKLAR + TOPLAM; aynı görünen satırlar `adet` ile toplanır; bulon/kaynak **görsel** modelleri (`displayEntityId`) sayılmaz · UI: `report.materialList` + `QtMainWindow::showMaterialList()` (Panoya Kopyala) · test: `material_list_test` · GT: `t26` (`malzeme-once.txt`/`malzeme-sonra.txt`) |

## Komponent nesnesi davranışı

| Davranış | Durum | Kanıt |
|---|---|---|
| Seçim (sembol + parçalar, Parça/Montaj modu) | **VAR** | `component_command_path_test`, `component_schematic_test`, `component_glyph_test` |
| Değiştir: paket diyaloğu modsuz (K1), Uygula → yeniden üret + tek Undo | **VAR** | `component_dialog_test`, `component_dialog_widget_test`, `component_recompute_test` |
| Kopyala / Taşı (üyelerle birlikte, kopya yeniden üretilir) | **VAR** (P-2/6) | `component_copy_test` (kopya) + `component_move_test` (taşıma): kolon +2000 mm → taban levhası 2000 mm birlikte gider, levha adedi sabit, kaynak görselleri 5/5 canlı, eski konumda stale model 0, sahipsiz model 0 · uygulama kancası `Application::translateModelsAndRecompute` · GT `t26` |
| Sil: bütün çocuklar gider, Undo hepsini geri getirir | **VAR** | `component_cascade_delete_test` |
| Üye değişince yeniden üret; başarısızsa eski hâl + uyarı (fail-closed) | **VAR** | `component_recompute_atomic_test`, GT `t23`, `component_rebind_test` |
| Kaydet/aç → aynı `planDigest` | **VAR** (MM tarafı) | `component_serialize_test`; Studio paket üretimi `planDigest` **raporlamıyor** → karşılaştırma REPIN'e kadar yapılamıyor (STATE notu) |
| Malzeme listesi / rapor satırları | **VAR** (P-2/5) | satır 8 ile aynı: `report.materialList` menü komutu + diyalog, `material_list_test`, GT `t26` |

## Madde durumu

| Madde | Durum | Kanıt |
|---|---|---|
| P-2/1 `plate.polygon` materyalizasyonu | **BİTTİ** (`72cc0ef`) | golden `92134f6a…c85e`, GT `t25` |
| P-2/1b taban levhası golden'ı | **BİTTİ** (`f9bb9fb`) | `tests/golden/studio_base_plate.json` |
| P-2/2 ankraj bulonu üyeye delik açmıyor | **BİTTİ** | `component_anchor_bolt_test` (A: ankraj → üye delinmez; B: normal grup → üye delinir) |
| P-2/3 `cut.plane` görünür + seçilir | **AÇIK** | yalnız kod izi çıkarıldı (`copySelection` ~8388, `document.cpp:358-520` `copyModels`, `renderer.cpp:940-1010/1055-1100`, `hitTestCutModifier*`); kopya kesim düzleminin çizilmemesi INBOX B8 ile aynı kök — düzeltme yapılmadı |
| P-2/4 dışbükey olmayan kontur | **BİTTİ** | `component_materializer_test` L kontur, hacim 675000 mm³ |
| P-2/5 BOM / malzeme listesi | **BİTTİ** | `report.hpp`/`report.cpp`, `material_list_test`, `report.materialList` + diyalog, GT `t26` |
| P-2/6 kopyala/taşı bütünlüğü | **BİTTİ** | `component_move_test`, `Application::translateModelsAndRecompute` |
| P-2/7 GT-1 GUI senaryosu | **BİTTİ** | `tests/gt1/t26_p2_obje_uyumlulugu_tasima.json`: 17 adım `ok=true`, `malzeme-once.txt` = `malzeme-sonra.txt` (5 levha, 4 bulon, 9 kaynak), taşımadan sonra tüm kutular X≈1888…2156 |

## Sıradaki commit'ler (her biri ayrı commit + kapı + GUI senaryosu)

1. **P-2/3 — `cut.plane` nesnesi görünür + seçilir.** B8 ile aynı kök neden
   (kopya çizilmiyor) — birlikte çözülmeli. **Kalan tek P-2 maddesi.**

Bitti sayılanlar: **P-2/1** (`plate.polygon` materyalizasyonu, `72cc0ef`),
**P-2/1b** (taban levhası golden'ı, `tests/golden/studio_base_plate.json`, `f9bb9fb`),
**P-2/2** (`component_anchor_bolt_test`), **P-2/4** (L kontur, `component_materializer_test`),
**P-2/5** (`report.cpp` + `material_list_test` + `report.materialList`),
**P-2/6** (`component_move_test` + `translateModelsAndRecompute`),
**P-2/7** (`tests/gt1/t26_p2_obje_uyumlulugu_tasima.json`).
