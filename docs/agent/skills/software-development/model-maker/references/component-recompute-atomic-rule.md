# Component Recompute — Atomiklik + Generic Adapter (Slice 2d-2c)

## Amaç
Kolon/kiriş değişince bağlı component'lerin (End Plate, Base Plate, Fin Plate, Gusset, Splice)
yeniden hesaplanması GÜVENLİ (ya hep ya hiç) ve GELECEKTE her component tipinin kullanabileceği
ortak bir hattan geçmeli. Document içine component tipine ÖZEL hard-coded recompute yolu EKLEME.

## Temel kalıp: prepare-then-commit (atomik)

`mm::atomicRecompute<Prepared>` (include/model_maker/component_recompute.hpp) — taşınabilir, OCCT'siz.

- `prepare(componentId) -> std::optional<Prepared>`: TÜM fallible iş burada (temas çözümü, B-Rep üretimi,
  tel kafes, doğrulama). nullopt = hata.
- `commit(componentId, prepared)`: yalnızca infallible document yazımları (setEndPlateConnection,
  putOccSolid, recomputeCutTarget). HATA ATMAMALI.
- Semantik: etkilenen id'ler sıralanır (artan, deterministic), HEPSİ prepare edilir; BİRİ bile
  başarısızsa HİÇBİR commit çağrılmaz (kısmi uygulama YOK).

`Prepared` tipi çağıran katmanda (OCCT tipleri buraya SIZMAZ). End Plate için Application içinde
`Application::PreparedEndPlate` (TopoDS_Shape + WireframeModel + güncellenmiş EndPlateConnection).

## Gerçek rollback: Document::rollbackOpenRecord

`undo()`'nun redoStack_'e İTEN davranışından farklı: acık kaydı geri uygular VE KAYDI ATAR
(redo'ya eklenmez). Basarısız işlem belge + geometry version + undo/redo yığınını tutarsız bırakmaz.

- Faz 1 doğrula (geometryBefore version'ları mevcut mu), Faz 2 geri uygula (applyUndoOp false +
  restoreActive + layers_ = layersBefore + rebuildDerivedState).
- KULLANIM: tetikleyici (profil atama / move / gumball end) TEK pushSnapshot açar → kolon değişimini
  commit eder → recomputeEndPlateConnectionsFor prepare-all → hepsi başarılıysa commit-all (aynı açık
  kayda); biri başarısızsa rollbackOpenRecord() ile KOLON DEĞİŞİMİ DAHİL hepsi geri alınır.

## Generic adapter: ComponentRefs

`ComponentRefs { EntityId id; std::vector<EntityId> parts; }` + `componentRefs(EndPlateConnection)` /
`componentRefs(ComponentInstanceRecord)`. ComponentDependencyGraph::build(ComponentRefs) ve
RecomputePlanner::planRefs(...) bu ortak temsili kullanır. Yeni component tipi eklerken yalnızca
bir adapter fonksiyonu + prepare/commit çifti yaz; graf/planner'a DOKUNMA.

## SectionAnchor (P3)

`profileSectionAnchorOffset(profile, SectionAnchor)` Top/Center/Bottom. DEFAULT Top = eski
`-profileSectionTopY` ile BİREBİR aynı (davranış değişmez). Mevcut modeller SESSİZCE başka anchor'a
dönüştürülmez. extrudeProfileSolid(..., anchor) parametresi ekli; varsayılan Top.

## Tuzaklar

1. prepare'ı SAF tut: geometri üretimi dışında belge mutasyonu YAPMA. Kolon tabanını tazeleyen
   rebuildCutBaseSolid, prepare'dan ÖNCE (recomputeEndPlateConnectionsFor başında, de-dupe edilmiş)
   çağrılır — açık transaction içinde olduğundan başarısızlıkta rollback ile geri alınır.
2. commit'te `setEndPlateConnection` kimlikleri korur (old.id/old.bolts.id); `placement.bolts`'ın
   boş id'si commit'te eski id'yle doldurulur — kararlı EntityId.
3. rollbackOpenRecord, undo()'nun aksine redoStack_ yığınını bozmaz; ama pushSnapshot zaten redoStack_
   temizlediği için ekstra bir şey gerekmez.
4. atomicRecompute şablonu `std::to_wstring` kullanır (EntityId=uint64_t); hata mesajı id'yi taşır.
5. Scale gumball'da recompute BİLİNÇLİ atlanır (P9: scale+cut desteklenmiyor; olceklenmiş katıyı
   bozardı). Rotate/transform gumball end'de `didTransform && rigidTransform` ile tetiklenir.
6. Undo/redo Application::undo()/redo() recompute TETİKLEMEZ — UndoRecord (EndPlateReplace + geometry
   version restore) durumu bütünüyle geri getirir; çift recompute YOK.
7. **Recompute HER görsel kanalı yenilemeli — store tek başına yetmez.** `OccSolidStore::set()`
   yalnız OCC katı/versiyonunu değiştirir; ekranda çizilen `WireframeModel` Document'te ayrı durur.
   Recompute'ta `replaceModel` çağrılmazsa parametre değişimi görünmez: delikler yeni sayıya göre
   açılır ama ESKİ bulon/levha çizili kalır (sahada: "önceki bulon aynen kalıyor, diğerlerinin deliği
   açılıyor"). Levha dalında replaceModel vardı, bulon grubu dalında YOKTU — yeni component dalı
   eklerken `displayEntityId` için de `doc_.replaceModel(*doc_.indexOfEntity(displayEntityId), {display})`
   yaz. Doğrulama: display wireframe köşe sayısı değişmeli (`vertices().size()`), yalnız bbox'a bakma —
   bulon sayısı değişince z-span sabit kalabilir.
