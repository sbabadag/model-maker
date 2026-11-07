END PLATE CONNECTION (Phase C1) — mimari notlar

Dosyalar:
- include/model_maker/end_plate.hpp + src/end_plate.cpp (saf veri + yerlesim)
- include/model_maker/end_plate_geometry.hpp + src/end_plate_geometry.cpp (OCC)
- Document: endPlateConnections_ vektoru + add/remove/set + UndoOp EndPlateAdd/
  EndPlateRemove/EndPlateReplace + deleteModels kaskadi.
- MMW8 'E' blogu (EndPlateConnection tam serilestirme, bolts dahil).

MIMARI (kritik):
- EndPlateConnection = kalici kayit (kendi EntityId). Uyeler: id, mainPart(kolon),
  secondaryPart(kiris), plateEntityId(levha), params(EndPlateParams), bolts(BoltGroup),
  enabled.
- Bulon yerlesimi BoltGroup'un VAR OLAN pattern mekanizmasina devredilir:
  bolts.origin = levhanin sol-alt kosesi (edge iceri), bolts.xDir = levha u yonu,
  bolts.axis = kiris yonu, countX/countY/spacing parametrelerden turetilir.
  Boylece boltGroupHoleModifiers + buildBoltGroupAssemblyShape SIFIR degisiklikle
  calisir.
- Bulonlar levha + KOLONU deler (kirisi DEGIL): bolts.parts = {plateEntityId,
  mainPart}. Levha kirise kaynakli varsayilir.
- addEndPlateConnection: levha modeli ekler (plateEntityId), bulon grubu ekler
  (BoltAdd + delikler owner=bolts.id + montaj display), baglanti kaydi ekler
  (EndPlateAdd). conn.bolts = boltGroups_.back() ile id esitlenir.
- removeEndPlateConnection: kayit + bulon grubu (removeBoltGroup) + levha
  (eraseModelLeaf) silinir; main/secondary KORUNUR.
- deleteModels kaskadi: main/secondary/plateEntityId/bolts.displayEntityId
  silinirse baglanti kaldirilir. Bu kaskad BULON kaskadindan ONCE calismali
  (removeEndPlateConnection zaten bolt grubunu siler — cift silmeyi onler).

PITFALL:
- EndPlatePlacement struct'inda plateThickness alani VAR (computeEndPlatePlacement
  doldurur) — buildEndPlateFromPlacement bunu kullanir, params'tan okumaz.
- endPlateFrame: referenceNormal ~ axis ise (|dot|>0.9) farkli ref dene, yoksa u
  dejenere olur. u = ref x axis, v = axis x u (sonra yeniden normalize edilir).
- MMW8 'E' blogu bolts'u G blogu formatinda tek satirda gomer; okuma da simetrik.
  kMmw8MaxEndPlates = 100000.

DURUM: cekirdek + OCC + GUI + R1 temas yuzeyi + R2a constraint solver + R2b contact
resolver TAMAM ve testli. 66/66 ctest gecer.

Phase R2a (Constraint System — `constraint_system.hpp/cpp`):
- `HandleKey` (kararli string anahtar), `HandleKind` (Point/Axis/Plane),
  `DistanceConstraint` (handle + hedef FaceReference + signedOffset + enabled),
  `ReferenceDistance` (salt okunur olcum), `ConstraintState`
  (Free/UnderConstrained/Solved/Conflict/Unresolved/Cyclic).
- Rank-tabanli DUZLEM konum cozucu: Gram G=A^T A + 3x3 Jacobi eigen; rank =
  sigma>1e-10*sigmaMax (BAGIL); tutarlilik MUTLAK esik 1e-6 mm (bagil esik buyuk
  koordinatta 1mm celiskiyi maskelerdi — reviewer bulgusu). Kismi kisitli handle
  icin null-space izdusumu, izdusum sonrasi artik esigi asarsa min-norm fallback.
  Dongu = Tarjan SCC. Non-finite giris + mukerrer anahtar reddedilir.
- Test `constraint_solver_test` (50 check; reviewer 5 bulgusu dahil).

Phase R2b (Contact Resolver — `contact_resolver.hpp/cpp`):
- `SectionDims{h,b,tw,tf}`, `makeMemberLocalFrame` (ProfileSectionFrame->u/v/w),
  `isOrthonormalRightHanded`, `resolveSemanticFacePlane` (I-kesit ofsetleri:
  FlangeOuter ±h/2 v, Web ±tw/2 u, FlangeEdge ±b/2 u, MemberStart/End ±w).
  DİKKAT: Web = ±tw/2 (b/2-tw/2 DEGIL).
- `resolveContactPlacement`: temas NOKTASI = kiris ekseninin duzlemle KESIMI
  (yuz merkezi DEGIL — kiris kotu korunur); sonlu yuz sinirlari dogrulanir;
  ParallelAxis/OutsideFace/AmbiguousFace/NoContact acik hata doner (rastgele
  yerlestirme YOK). Dondurulmus kolonda lokal cerceve uyeyle doner.
- End Plate entegrasyonu (`applyEndPlateFromParts`): bbox cozucu yerine contact
  resolver; bbox yalnizca ACIK fallback. Test `contact_resolver_test` (26 check).

PENDING (sonraki dilim): gercek OCCT B-Rep face adapter (sinirli yuz esleme +
kesim dogrulama; face index kalici kimlik DEGIL), egik kiris fitting, MagneticPlane
+ AutoBind, MMW8 kaliciligi, Windows GUI dogrulama.

Phase R1 (Temas yuzeyi + referans geometri baslangici):
- `reference_geometry.hpp/cpp`: `ReferencePlane` (origin + birim normal) +
  `resolveBoundaryPlaneFacing(bmin,bmax,outwardDir)` — Tekla "Boundary plane"
  karsiligi: outwardDir yonune bakan bbox yuzu (en paralel normal).
- `EndPlateParams.gap` eklendi (varsayilan 0; Tekla 101'de 2 mm).
- `computeEndPlatePlacementOnFace(face,gap,secW,secH,params)`: levha kolon
  tarafi yuzu `face.origin + gap*normal`; kalinlik `face.normal` yonunde (kirise
  dogru) acilir. Eski `computeEndPlatePlacement` (beamEnd-relative) GERIYE UYUMLU
  kalir; bulon kurulumu `makeEndPlateBolts` yardimcisina cekildi (tek kaynak).
- GUI `applyEndPlateFromParts`: kolon bbox yuzunu cozer (-beamAxis yonunde) +
  `computeEndPlatePlacementOnFace` kullanir. Kolon icine girme/rastgele offset
  giderildi.
- Test: `end_plate_contact_test` (17 check; gap=0 cakisma, gap=10, -X yuzu,
  bulon ekseni dik). Egik kiris adaptasyonu + gercek B-Rep face cozumu SONRAKI dilim.

DURUM (eski): cekirdek + OCC + GUI tamam ve testli (end_plate_test 38 check, occ_end_plate
_acceptance 17 check). GUI (Phase C1): `startEndPlateCommand` verb-noun — once
kolon (main) sonra kiris (secondary) secilir, sag tik uygular, Esc iptal;
`applyEndPlateFromParts` materialize eder. PENDING: cift tik parametre diyalogu
(kalinlik/cap/adet) + tasima recompute — Windows-only.