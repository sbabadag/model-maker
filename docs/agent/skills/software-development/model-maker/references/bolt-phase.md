# Bolt Phase (B1) — mimari dersleri

## Delik = CutModifier yeniden kullanımı (yeni tip DEĞİL)
- Delikler ayrı bir "HoleModifier" tipi olarak değil, mevcut CutModifier zincirine
  `CutKind::Hole` + `double diameter` alanı eklenerek uygulanır.
- Hole semantiği: `planeNormal` = delik ekseni (birim), `planeOrigin` = merkez,
  `diameter` = delik çapı; silindir parçayı boydan boya keser (through-cut).
- OCC tarafı (occ_geometry.cpp `applyCutModifier` Hole case):
  `BRepPrimAPI_MakeCylinder(gp_Ax2(center, axisDir), diameter/2, 2*half)` + `booleanCutShape`.
  `half = bbox köşegeni + diameter` (silindir iki ucu da katıyı aşsın diye).
  `diameter<=0` veya eksen ~0 ise base'e dokunulmaz (güvenli no-op).

## MMW8 geriye uyumlu alan ekleme örüntüsü
- 'C' bloğu satır-tabanlı; yeni alanı SATIR SONUNA ekle (diameter, invertSide'dan sonra),
  okurken `if (ls >> diameter)` ile opsiyonel oku. Eski dosyalar bozulmaz.
- `CutKind` enum'una YENİ değer SONA ekle (Hole=5) — mevcut int değerler kaymaz.
- `mmw8_codec.cpp`'deki `kind > 4` doğrulamasını yeni üst sınıra (`kind > 5`) güncelle.
- CutKind üzerinde exhaustive switch varsa -Wswitch uyarısı verir: document.cpp
  `resolveCutModifierSource`'a `case CutKind::Hole: break;` ekle (kaynak çözme yok).

## BoltGroup veri modeli
- `bolt.hpp`: BoltGroup (id=EntityId, parts=vector<EntityId>, origin, axis, xDir,
  BoltParams, displayEntityId). `makeHoleCutModifier(part, origin, axis, cap)` delik üretir;
  `boltGroupHoleModifiers(bolt)` her parça için bir delik döndürür.
- Görsel bulon geometrisi (baş+gövde+somun+pul) ayrı displayEntityId varlığıdır;
  bağlantı verisi (hangi parçalar, eksen, çap) BoltGroup'ta taşınır.
- Bulon boyutları ISO yaklaşık: baş/somun köşegeni s=1.5*d, baş k=0.7*d, somun m=0.8*d,
  pul D=2*d, t=0.15*d (`boltDimensions`).

## Test düzeni
- Çekirdek (OCC'siz): tests/bolt_test.cpp — CMakeLists'te model_maker_bolt_test.
- Gerçek OCCT: tests/occ_bolt_acceptance.cpp — elle g++ ile derlenir (bkz. dosya başı).
  İki levha + M20/Ø22: delik hacmi = pi*11^2*10 her levha; taban B-Rep değişmez;
  levha DIŞINA konan delik kesmez (silindir origin'e yerleştiğini kanıtlar).

## Slice 2 — bulon montaj geometrisi (bolt_geometry.hpp/cpp, MM_HAS_OCC)
- `buildBoltAssemblyShape(bolt, gripLength)` altigen bas + govde + altigen somun + 2 pul
  tek fused TopoDS_Shape döndürür; delik ekseniyle TAM eşmerkezli (origin merkez,
  axis birim yön). grip = bağlı levha yığınının eksen boyunca kalınlığı.
- Yerleşim simetriktir: govde ±grip/2, bas -yönde (k=0.7d + pul), somun +yönde
  (m=0.8d + pul). Bileşenler AYRIK (pul govdeyle bitişik, çakışmaz) — hacim testi
  head+nut+shank+2*washer toplamına eşittir.
- gp_Dir::SquareModulus() YOK; `gp_Pnt - gp_Vec` YOK → `gp_Pnt::Translated(gp_Vec)`
  kullan. Altigen prizma: R = acrossFlats/√3, BRepBuilderAPI_MakePolygon + MakeFace +
  MakePrism(face, z*height).
- Standard katalog: M12→14, M16→18, M20→22, M24→26, M27→30, M30→33 delik;
  kalite 8.8/10.9 (isBoltGradeValid).

## Slice 3a — belge entegrasyonu (document.cpp)
- `CutModifier.owner` = bulon grubu EntityId (Hole'ların sahibi). MMW8'de SATIR SONU
  opsiyonel alan (diameter'dan sonra `<< mod.owner`, okurken `if (ls >> owner)`).
- BoltGroup `boltGroups_` vektöründe belge-sahipli KAYITTIR (grids/cutModifiers gibi),
  models_ içinde DEĞİL. MMW8 'G' bloğu (satır-başına-grup, grade putString).
- addBoltGroup ALT SEVİYE op'lardan KURULUR (tek undo kaydı): Add(display) + N×CutAdd
  (delik, owner=id) + BoltAdd(kayıt). Undo ters sırada doğal geri alır. id geçersizse
  allocateEntityIdRequired; display model addModel ile eklenip kimliği displayEntityId'ye.
- removeBoltGroup SIRA ÖNEMLİ: önce BoltRemove(kayıt sil — kaskad yeniden tetiklemesin),
  sonra delikler (yüksekten düşüğe), sonra display (eraseModelLeaf — deleteModels DEĞİL,
  yoksa bulon kaskadını yeniden tetikler → recursion).
- setBoltGroup: BoltReplace + eski delikleri sil + yenileri ekle; id ve displayEntityId
  KORUNUR (girdideki yok sayılır).
- deleteModels kaskadı: parçası silinen bulon grubu da kaldırılır (ters sırada
  removeBoltGroup).
- UndoOp'a BoltAdd/BoltRemove/BoltReplace eklendi (boltIndex/boltBefore/boltAfter).
- run_occ_solid_ids.py CORE_SOURCES'a src/bolt.cpp EKLE (document.cpp artık
  boltGroupHoleModifiers kullanır; eklenmezse link hatası).
- Gerçek OCCT round-trip: tests/occ_bolt_group_roundtrip_test.cpp — kayıt+delik(owner)+
  display B-Rep'i save/load sonrası da duzenlenebilir (CMakeLists model_maker_occ_bolt_group_roundtrip).

## Slice 3b — GUI (application.cpp + qt_main_window.cpp, Windows-only)
- Bulon display'i NORMAL modeldir → secilebilir/tasinabilir/silinebilir; deleteModels
  kaskadı bulon kaydını da siler (ek GUI silme kodu GEREKMEZ).
- Move/gumball senkronu: `syncBoltGroupsForMoved(indices, displacement)` — tasınan
  display'in bulonunu bul (displayEntityId eşleşmesi), origin += displacement,
  setBoltGroup (delikler yeniden), rebuildBoltDisplay (B-Rep+mesh), recomputeCutTarget
  (her parça). Move komutunda `displacement` hazır; gumballEndDrag'da
  `gumballAppliedDelta_` RESET'ten ÖNCE kopyalanmalı.
- `rebuildBoltDisplay(bolt)`: grip → buildBoltAssemblyShape → putOccSolid(display) +
  replaceModel(shapeToWireframeWithFaces). putOccSolid geometri deltasını kaydeder.
- Noun-verb: 2 secili parça → origin = birleşik bbox ortası, eksen = workPlane_.normal.
- Windows'ta derlenemez (Qt/Win32); GUI davranışı Asus'ta doğrulanır — PENDING raporla.
