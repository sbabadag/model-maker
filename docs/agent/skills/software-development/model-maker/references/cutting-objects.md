# Construction + Cutting nesneleri (Phase 4.1/4.2) — durum

## Zaten var (overnight, portatif + Linux testli)

- `ConstructionKind{None,Point,Line,Plane}` + `WireframeModel::setConstructionKind`;
  `Document::addConstructionPoint/Line/Plane` + `constructionObjects()/isConstruction()/`
  `setConstructionVisible()`. Test: `model_maker_construction_objects_test`.
- `CutModifier`/`CutKind{PlaneCut,Fitting,PolygonCut,BooleanCut}` (include/model_maker/cut_modifier.hpp)
  + `Document::addCutModifier/removeCutModifier/setCutModifier` (undo CutAdd/CutRemove/CutReplace).
  Test: `model_maker_cut_modifier_test`. Hedef/arac parcalar EntityId ile referanslanir — kesme
  parcanin KIMLIGINI degistirmez, yalniz geometri surumunu ilerletir.

## YENI kesme UYGULAMA motoru (commit 14bb961)

- `fittingKeepPositive(axisFrom, axisTo, origin, normal)` (cut_modifier.cpp, PORTATIF):
  Tekla "Fit part end" — kesen duzlemde KISA taraf otomatik atilir (eksen uclarindan isaretli
  uzakligi mutlak BUYUK olan taraf korunur); kesmeyen duzlem (uzatma/kisaltma, iki uc ayni
  tarafta) nullopt. Prototip yalniz KESEN duzlemi destekler.
- `applyCutModifier(base, mod, axisFrom, axisTo)` / `applyCutModifierChain(base, mods, axisFrom,
  axisTo)` (occ_geometry.cpp, MM_HAS_OCC): CutModifier -> B-Rep. PlaneCut = cutSolidByPlane
  (keepPositive); Fitting = fittingKeepPositive sonra cutSolidByPlane; PolygonCut/BooleanCut
  SONRAKI faz (base aynen doner). Zincir SIRA ILE uygular; etkin-degil modifier atlanir; base
  DEGISMEZ (immutabilite) — orijinal B-Rep korunur, yeniden hesaplama taban katidan yapilir.

## TUZAK (yasandi)

`occ_geometry.cpp` artik `cut_modifier.hpp` iceriyor ve `fittingKeepPositive` CAGIRIYOR →
occ_geometry.cpp'yi TEK BASINA derleyen herhangi bir OCC harness'i (or. tests/run_occ_solid_ids.py
CORE_SOURCES) `src/cut_modifier.cpp`'yi de LINK ETMELI. model_maker_occ kutuphanesi
model_maker_core uzerinden zaten bagli (sorun yok); yalniz Python harness'leri etkilenir.

**RENDER TUZAGI (kullanici: "kesim objeleri gorunuyor ama kesim olmuyor"):**
`recomputeCutTarget` baslangicta YALNIZ `putOccSolid(index, result)` cagiriyordu — OCC
magazasini guncelliyor ama renderer `model.faces()` (document mesh'i) cizdigi icin ekranda
kesme GORUNMUYORDU (kesme objesi `drawCutPlanes`'ten cizildigi icin gorunuyor, kati kesilmemis
kalıyordu). KURAL: kesme/trim gibi geometri DEGISTIREN her recompute, `putOccSolid`'un
YANINDA sonucu `shapeToWireframeWithFaces(result, 0.15)` ile yeniden tesselatlayip
`document_.replaceModel(index, {model})` ile belge mesh'ini de guncellemeli (trim yolunun
aynisi). `replaceModel` `revision_`'u artirir -> GL face batch de yenilenir. C++/OCC acceptance
testleri bunu YAKALAMAZ (yalniz OCC magazasini dogrular) — render baglantisi Windows GUI'de
elle dogrulanmali.

## Tekla semantigi (resmi dokumandan, birebir)

- Fitting (Fit part end): parca sec -> 2 nokta (fitting line) -> duzlem GORUNUS DUZLEMINE DIK,
  iki noktadan gecer. Tekla OTOMATIK taraf secer (kisa taraf kesilir). Gross length etkiler
  (NC dosyasina yansir). Bir uc icin BIR fitting; ayni uca ikinci fitting ilkini EZER. Uzatma/
  kisaltma yapabilir (asiri degil).
- Line Cut: elle taraf secimi gerekir; gross length ETKILEMEZ; uzatamaz. Fitting + Line Cut
  ayni ucta birlikte kullanilabilir (egik + cift yonlu).
- Construction Plane: Edit -> Construction object -> Plane -> 3 NOKTA sec -> orta fare tusu.
  Cift tik = property pane (name + magnetic).

## OCC kabul probe'u

`tests/occ_cut_probe.cpp` (12 kontrol: fitting kisa-taraf hacim/bbox, kesmeyen duzlem, PlaneCut
keepPositive, coklu modifier zinciri, etkin-degil atlama, base immutabilite). Derleme komutu dosya
basliginda: occ_geometry + geometry + cut_modifier + profile_database + OCCT libleri
(-std=gnu++20 -DMM_HAS_OCC). CMake'de `MODELMAKER_OCC_READY` altinda ctest'e kayitli
(model_maker_occ_cut_probe); Linux OCC-disabled build'de kurulmaz — elle g++ ile kosulur.

## PENDING (Windows GUI dogrulanmadi)

- Construction objesi + kesme modifier CIZIMI (renderer), secim/snap, ozellik paneli.
- 3-noktali construction plane + Fit part end MENU/ribbon komutlari (application.cpp + qt_main_window).
- `Application::recomputeCutTarget` VAR (occ store taban surumden + zincir uygulama + mesh
  guncelleme); kesme UYGULAMASI (Fitting/PlaneCut/LineCut/PolygonCut/PartCut) GUI'de calisir.
- HENUZ YOK: kesme objesini (konstr. duzlem / cizgi / 3DFACE) TASIMAK hedefi yeniden
  hesaplamiyor — `recomputeTargetsUsingTool` yalniz Part Cut ARAÇ katisindan (reExtrudeProfileGrip)
  cagriliyor. Kesme objesi tasima -> ilgili hedeflerin yeniden hesaplanmasi eklenecek.
- MMW8 kaliciligi: cutModifiers VE constructionKind henuz SERILESTIRILMIYOR (mmw8_codec'te yok).
