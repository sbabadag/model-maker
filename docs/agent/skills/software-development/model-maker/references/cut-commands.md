# Kesme komutlari (Fitting/Plane/Line/Polygon/Part Cut) — P1..P11

- `CutModifier` (include/model_maker/cut_modifier.hpp): `kind` (PlaneCut/Fitting/
  PolygonCut/BooleanCut/LineCut), `target`/`source`/`tool` EntityId, `keepPositive`,
  `invertSide`, `enabled`, `name`, `planeOrigin/Normal`, `polygon`. Kesme motoru
  `src/occ_geometry.cpp` `applyCutModifier` (Fitting artik UZATIYOR: duzlem ekseni
  kesmiyorsa en yakin uctan, I-kesitin TUM kapak yuzleri (flanş/govde/flanş ayrı yuz)
  extrude edilip kaynakla birleştirilir sonra kesilir).

- **Kaynak referanslanir, KOPYALANMAZ (P6):** `modifier.source = document_.modelId(planeIdx)`;
  `resolveCutModifierSource(document)` duzlemin GUNCEL geometrisini cozer. Boylece kopya
  sari duzlem olusmaz, duzlem tasininca/donunce kesme izler, silinince kaskad kaldirilir.

- **Renderer mesh guncellemesi sart:** `recomputeCutTarget` yalniz `putOccSolid` yazarsa kesme
  GORUNMEZ (renderer belge mesh'ini `model.faces()`'ten cizer). Kesme sonucu
  `shapeToWireframeWithFaces` + `replaceModel` ile belge mesh'ine de yazilmali.

- **Acilan .mmw8 dosyasi katiyi saklamaz** (yalniz mesh + ozellik) — kesmeden once
  `ensureCutSolid(target)` hedefi profil ozelliklerinden (eksen+rotasyon+sectionNormal)
  yeniden uretir; ilkel katilar (kutu/piramit) tasiyici bilgi yoksa kesilemez.

- **Gumball/undo sonrasi B-Rep drift:** profili gumball/undo ile tasiyinca OCC katisi
  guncellenmez (moveModels/undo yalniz mesh+eksen). `recomputeCutTarget` taban katiyi HER
  SEFERINDE guncel eksen/rotasyondan yeniden uretmeli; yoksa fitting profili eski konumuna
  ziplatir. `gumballEndDrag`'da `gumballTransformSet_` recompute'dan ONCE temizlenirse
  recompute BOS kume alir (move sonrasi kesme guncellenmez) — once yerel kopya, sonra temizle.

## VERB-NOUN kesme komutu (P11) — Fitting/Plane Cut

KULLANICI SARTI: komut girilince once PROFIL sonra DUZLEM secilir (sol tik), SAG TIK isletir,
Esc iptal. Eski davranis noun-verb idi (once sec, sonra komut). Desen:

- `enterPendingCutCommand(kind)` (startFittingCommand/startPlaneCutCommand): aktif cizim/
  transform/secim kapatilir, `pendingCutKind_/Target_/Plane_` kurulur. Sol tik
  `onLeftButtonDown`'da `pickPendingCutAt` ile islenir (once profil, sonra duzlem;
  `modelIndexAt` = hitTestModel2D/3D ortak esik). Sag tik `WM_RBUTTONDOWN`'da
  `executePendingCutCommand` -> `applyCutFromPair(kind, target, plane)`. Esc (VK_ESCAPE)
  `cancelPendingCutCommand`.

- **KRITIK TUZAK:** `cancelPendingCutCommand` beklemede DEGILKEN NO-OP olmali
  (`if (!pendingCutKind_) return;`). startTransformCommand/selectTool/deactivateAllCommands
  BASINDA cagrilir; NO-OP degilse her transform/cizim baslangicinda noun-verb secimini
  sessizce SILER (Move/Copy once sec sonra komut akisi kirilir).

- `applyCutFromSelection` (noun-verb, yalniz resolution) + `applyCutFromPair` (ortak uygulama)
  diye ayrildi; ikisi de ayni `recomputeCutTarget` yoluna duser. Yeni komut state'i
  `pendingCut*` uyeleri (application.hpp, `selectedModels_` yaninda).
