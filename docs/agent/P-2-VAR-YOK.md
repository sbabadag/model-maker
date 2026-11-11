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
| 2b | `plate.polygon` — **dışbükey olmayan** kontur | sınır kutusu + poligon; desteklenmiyorsa açık RED | **KISMI** | `poly.Add` + `BRepBuilderAPI_MakeFace`; içbükey kontur için ayrı test YOK, davranış doğrulanmadı → P-2/4 |
| 3 | `bolts.grid` (through üyeler) | bulon grubu: kalite, delik, grip; üyelerde ve levhalarda delik açar | **VAR** | `src/bolt.cpp:223` `boltGroupHoleModifiers`, `src/document.cpp:390,2662,2718,2785` delik zinciri, `src/component_materializer.cpp:903` (levha delikleri + kesmeler) · test: `component_materializer_test`, `component_bolt_rows_reduced_test` |
| 4 | `bolts.grid` ankraj (`through: []`, aşağı) | üyeye delik AÇMAZ; yalnız taban levhasında delik | **DOĞRULANMADI** | Kodda ankraj ayrımı var (`holeDepth`/`through` boş), fakat "kolonda delik yok" iddiasını kilitleyen test YOK → P-2/2 |
| 5 | `weld.fillet` | kaynak nesnesi: görünür, seçilir, boyut ve iki taraf doğru | **VAR** | `src/component_materializer.cpp:846,1224` (`doubleSided`), `:47` `kWeldColor`; kaynak katısı + display modeli, desteklenmiyorsa `unsupportedReason` (sessiz yanlış katı yok) · test: `component_materializer_test` |
| 6 | `cut.plane` (fitting dahil) | üye katısı kesilir; kesim nesnesi görünür ve seçilir | **KISMI** | `src/recipe.cpp:1844` `cut.plane` bloğu, `:1368` hedef çözümü; kesme uygulanıyor · kopyalanan kesim düzlemi çizilmiyor (INBOX B8, açık) → P-2/3 |
| 7 | `check` başarısız | komponent kurulmaz; Türkçe mesaj; belge değişmez | **VAR** | `src/application.cpp` `recomputeComponentInstancesFor` (`prepWhy`, `CR-PREP-FAIL`) + `atomicRecompute` rollback · test: `component_recompute_atomic_test`, GT `t23` (iki dal, 23 adım, `ok=true`) |
| 8 | Malzeme listesi / rapor satırları (levha, bulon, kaynak) | raporda üç satır türü | **YOK** | Ayrı BOM/metraj özelliği yok; malzeme bilgisi parça özelliklerinde (`props.material`) ve `Pos_No: …` açıklamasında (`:62`) yaşıyor · `drawing.hpp` yalnız etiket `role`/`kind` taşıyor → P-2/5 |

## Komponent nesnesi davranışı

| Davranış | Durum | Kanıt |
|---|---|---|
| Seçim (sembol + parçalar, Parça/Montaj modu) | **VAR** | `component_command_path_test`, `component_schematic_test`, `component_glyph_test` |
| Değiştir: paket diyaloğu modsuz (K1), Uygula → yeniden üret + tek Undo | **VAR** | `component_dialog_test`, `component_dialog_widget_test`, `component_recompute_test` |
| Kopyala / Taşı (üyelerle birlikte, kopya yeniden üretilir) | **VAR** (kopya) / Taşı kapsamı doğrulanmadı | `component_copy_test`; "üyelerle birlikte taşıma" için ayrı test YOK → P-2/6 |
| Sil: bütün çocuklar gider, Undo hepsini geri getirir | **VAR** | `component_cascade_delete_test` |
| Üye değişince yeniden üret; başarısızsa eski hâl + uyarı (fail-closed) | **VAR** | `component_recompute_atomic_test`, GT `t23`, `component_rebind_test` |
| Kaydet/aç → aynı `planDigest` | **VAR** (MM tarafı) | `component_serialize_test`; Studio paket üretimi `planDigest` **raporlamıyor** → karşılaştırma REPIN'e kadar yapılamıyor (STATE notu) |
| Malzeme listesi / rapor satırları | **YOK** | satır 8 ile aynı → P-2/5 |

## Sıradaki commit'ler (her biri ayrı commit + kapı + GUI senaryosu)

1. **P-2/2 — ankraj bulonu üyeye delik açmıyor.** `t25` (taban levhası, 8 ankraj)
   çıktısında kolon katısında delik olmadığını kilitleyen GT/unit kontrolü.
2. **P-2/3 — `cut.plane` nesnesi görünür + seçilir.** B8 ile aynı kök neden
   (kopya çizilmiyor) — birlikte çözülmeli.
3. **P-2/4 — `plate.polygon` dışbükey olmayan kontur.** Destekleniyorsa test,
   desteklenmiyorsa `frame`/kontur doğrulamasında açık RED (sessiz boş katı yok).
4. **P-2/5 — BOM/malzeme listesi:** levha + bulon + kaynak satırları (kalite,
   adet, ölçü, malzeme, Pos_No).
5. **P-2/6 — kopyala/taşı bütünlüğü:** komponent üyelerle taşınınca çocuk
   nesnelerin (levha/bulon/kaynak/kesim) yeni konumda yeniden üretilmesi.
6. **P-2/7 — GUI senaryosu (GT-1):** taban levhası paketini uygula → parça seç →
   malzeme satırında gör → kolonu taşı → taban levhası onunla gider.

Bitti sayılanlar: **P-2/1** (`plate.polygon` materyalizasyonu, `72cc0ef`) ve
**P-2/1b** (taban levhası golden'ı, `tests/golden/studio_base_plate.json`).
