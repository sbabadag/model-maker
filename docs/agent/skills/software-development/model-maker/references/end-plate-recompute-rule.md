# End Plate Recompute Kuralı (Slice 2d-2b)

Kolon/kiris degisince mevcut End Plate'in levha+bulon+deliklerinin GERCEK B-Rep'te
guncellenmesi. Yeniden hesap icin bu proseduru izle — sil-yeniden-ekle yapma.

## Tek kaynak: resolveEndPlatePlacement
- `Application::resolveEndPlatePlacement(mainIdx, secIdx, params, placement, err)`
  hem CREATE (applyEndPlateFromParts) hem RECOMPUTE (rebuildEndPlateConnection)
  icin TEK yerlesim kaynagi. Cift kod yazma (drift riski).
- Icerik: kiris ekseni/ucu (kalici axis + kose fallback) -> kolon cercevesi/kesit
  (profileSectionFrame + makeMemberLocalFrame) -> OCCT temas (resolveOcctContact)
  oncelikli, lokal kesit (resolveContactPlacement) fallback -> computeEndPlatePlacementOnFace.
- ACIK hata (paralel eksen / yuz disi / belirsiz) err + false doner; cagiran
  belgeyi DEGISTIRMEZ (eski gecerli geometri korunur).

## Kararlı kimlik: setEndPlateConnection
- `Document::setEndPlateConnection(index, conn, plateModel, boltDisplayModel)`
  recompute'un TEMEL primitive'i. Kimlikleri KORUR (plateEntityId / bolts.id /
  displayEntityId eski degerlerle ezilir) ve EndPlateReplace + BoltReplace +
  CutReplace + model Replace op'larini ACIK undo kaydina yazar.
- `setBoltGroup` icinde delikler yeniden uretilir: eski delikler owner==bolt.id
  ile SILINIR, yenileri eklenir -> delik birikmesi YOK (idempotent).
- rebuildEndPlateConnection sirasi: rebuildCutBaseSolid(main) -> resolveEndPlatePlacement
  -> buildEndPlateFromPlacement + buildBoltGroupAssemblyShape (GECICI dogrula) ->
  setEndPlateConnection -> putOccSolid(plate+display) -> recomputeCutTarget(main+plate).

## Atomik undo (tek transaction)
- Tetikleyici (Move komutu / gumball end / profil atamasi) mutasyondan ONCE TEK
  pushUndoSnapshot() acar; recompute ayni kayda op ekler -> kolon hareketi + levha
  + bulon + delik TEK Ctrl+Z adiminda geri alinir.
- Profil degisimi icin `ProfileReplace` undo op'u GEREKLIDIR (setModelProfile onu
  kaydeder). setModelProfile recordUndoOp kullanir -> kayit YOKSA sessizce duser;
  cagiran profil atamasindan ONCE pushUndoSnapshot() acmali.
- Gumball: drag basinda bir kez snapshot (gumballUndoStarted_ guard), end-drag'da
  recompute -> tum surukleme tek kayit.

## Geometri gercegi: "top of section"
- extrudeProfileSolid kesiti "top of section" yerlestirir: eksen cizgisi kesitin
  EN USTUNDEN (ust flans DIS yuzunden) gecer, kesit asagi sarkar. Bu yuzden
  HEB300->HEB400 (ayni eksen) ust flans yuzunu Y=0'da SABIT birakir -> levha
  KONUMU degismez, kolon sadece derinlesir (B-Rep hacmi artar).
- Levha KONUMU su durumlarda tepki verir: kolon 30° donerse (C), kiris +Z
  tasinirsa (D). Profil degisiminde dogru dogrulama: yeniden cozum BASARILI +
  temas yuzu dogru + delik sayisi sabit (birikme yok).

## OCC kabul testi (Linux'ta gercek dogrulama)
- tests/end_plate_recompute_test.cpp — dogrudan g++ ile derlenir (CMake OCC yolu
  BOZUK; register etme). A/B/C/D/E/L senaryolari.
- Yerlesimin BoltGroup'u PARCA listelemez (parts BOS); Document parts={levha,kolon}
  doldurur. Testte delik saymak icin once `bolt.parts = {1,2}` doldur.

## Kaynak-sekil kontrat testleri (naive regex)
- run_profile_section_reference.py + run_occ_solid_ids.py application.cpp'yi
  regex'le tarar. extrudeProfileSolid cagrisinin argumanlarinda IC ICE parantez
  (`m.properties().x`) OLMAZ — regex `[^;]*?)` ilk `)`'da durur ve virgul sayimini
  bozar; once local degiskene cikar.
- run_occ_solid_ids.py profil atamasinin setModelProfile'ten ONCE pushUndoSnapshot
  acmasini kilitler (snapshot yeri degistiyse bu kontrati guncelle).
