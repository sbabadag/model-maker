# Weld OCC kati (Phase W3) — dersler

- Kaynak katisi ucgen kesitli prism'dir: bacaklar iki yuzun, kaynak cizgisine dik
  tegetlerine (tm=d×nm, ts=d×ns) yerlestirilir; koseye (acisortay nm+ns tarafina)
  bakmasi icin nokta-carpim isaretiyle yonlendirilir. Hacim (90°) = 0.5·leg²·length.
- Yuz normalleri DISA yonlu olmali (kaynak kose tarafini dogru secer): planar yuz icin
  BRep_Tool::Surface + Geom_Plane ekseni alinir, face.Orientation()==TopAbs_REVERSED ise
  ters cevrilir. Bu standard ve guvenilirdir; nokta±ε mesafe kiyasi KIRILGANDIR, kullanma.
- KOSE YUZU SECIMI: temas noktasi birden fazla yuzde olabilir (levha yuzu + temas yuzu).
  Kaynak yuzu, ana yuz normaline DIK olan yuzdur; refNormal'e paralel (|dot|>0.99) yuzler
  atlanir. Boylece T-joint'te levhanin YAN yuzu (kose) bulunur, temas yuzu degil.
- BRepPrimAPI_MakeBox kenarlari seam nedeniyle IKI KEZ rapor edilebilir (ayni dogru parcasi
  yuz siniri + seam cakismasi). findWeldJunctions dedup yapmali: start/end tol icinde esitse
  yineleme say (yon tersi dahil). Dedup yapilmazsa cift-tarafli kaynak iki OZDES kati uretir.
- Weld display modeli NORMAL modeldir (addModel) -> MMW8 'B' blogu onu otomatik serilestirir
  (buildMmw8 modelIds_ uzerinde gezer). Weld kaydi displayEntityId'yi 'W' blogunda saklar;
  yeni alanlar SONA eklenir ve okuyucu kalan-token-yoksa-varsayilan yapar (geriye uyumlu).
- Recompte: editWeld pushSnapshot -> setWeld -> recomputeWeldGeometry; display modeli
  replaceModel(1:1 kimlik korunur) + store->set + recordGeometryDelta. Gecersizlesen kati
  removeModelLeaf (PUBLIC; eraseModelLeaf private'tir, application.cpp'den cagrilamaz).
- Butt/egrisel/ayrik parcalar: kati URETILMEZ (geometrySupported=false + neden); sessiz
  yanlis kati yok. sizeIsThroat=false = bacak; throat istenirse leg=throat·sqrt(2) donusumu.