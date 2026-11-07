# Gumball — model-maker

> 2026-10-19 tarihinde SKILL.md gövdesinden taşındı; içerik birebir korunmuştur.
> Tetikleyici ve kısa kural SKILL.md'deki referans dizininde.

## Gumball (Rhino tarzi tasima tutamaclari)

- Secili nesne(ler)in sinir kutusu merkezinde, **EKRAN-OLCEKLI (~110 px)** gumball.
  `Application::updateGumball()` gorunurluk + origin hesaplar (updateHover'dan her fare
  hareketinde cagrilir; `gumballDrag_` aktifken dokunmaz). `gumballWorldLength()`
  110 px'e karsilik gelen dunya uzunlugunu projeksiyondan olcer.
- Tutamaclar `GumballHandle` enum (renderer.hpp): AxisX/Y/Z, PlaneXY/YZ/ZX, Center.
  Ekran uzayinda `gumballHitTest` secer: once merkez (<=9px), sonra duzlem dortgenleri
  (isaretli alan testi), sonra eksen segmentleri (t>0.35, d<=9px).
- `gumballDragMove`: eksen icin imlecin ekran-eksen izdusumu + piksel/dunya -> delta =
  eksen*dist; duzlem/merkez icin ray-duzlem kesisimi (`camera_.unprojectToPlane`,
  merkezde kameraya bakan duzlem = `viewTransform({0,0,1})`). Duzlem SABIT: drag
  baslangic origin'inden gecer.
- Surukleme CANLI: her karede `document_.moveModels(secim, step)` (step = toplam delta -
  uygulanan). `pushUndoSnapshot()` SADECE baslangicta -> TEK undo adimi (recordUndoOp
  acik kayda ekler; pushSnapshot yeni kayit acar). Birakinca `gumballEndDrag`.
- Kancalar: onLeftButtonDown (ViewCube kontrolunden SONRA, secim/transform'a dusmez),
  onMouseMove (drag ise erken donus), onLeftButtonUp; drag boyunca `SetCapture`.
- Gorsel: renderer.cpp GL kompozitinin USTUNE — eksen oklari (X kirmizi/Y yesil/Z mavi)
  + ok uclari, dolgulu duzlem kareleri, merkez daire; hover'da tutamac SARI/kalin.
  2B'de yalniz X/Y oklari + XY duzlemi.
- v2: **donusme yaylari + eksen/tek-bicim olcekleme + numerik giris + Escape iptal**.
- v3: **YEREL (object-local) eksenler** — gumball world ekseninde DEGIL nesnenin kendi
  cercevesinde calisir. KULLANICI SARTI; world eksenine donme.

- v4: **82 px kompakt**, eksen/yay 3px (hover 4-5), duzlem kenari 2px (hover 3), ok ucu 12x6,
  yaylar kapali (Polyline segs+1). Duzlem dolgu rengi NORMALE gore: YZ=kirmizi, ZX=yesil,
  XY=mavi. Hover rengi eksen tonunu korur (sari degil).
- v4 tikla-edit box: tutamaca basip SURUKLEMEDEN birakinca Qt edit box acilir: eksen = mesafe,
  yay = aci (derece), duzlem = 'a;b' iki yerel mesafe. Iptal/no-op undo gecmisini degistirmez
  (`gumballUndoStarted_` ertelenmis pushUndoSnapshot). Surukleme aynen calisir.
- v5: **SHIFT + tutamac = KOPYALA.** `gumballCopyMode_` basma aninda okunur
  (`GetKeyState(VK_SHIFT)`); `gumballCopyIfNeeded()` HER ZAMAN `pushUndoSnapshot()` SONRASI
  cagrilir (yoksa `recordUndoOp` op'u DUSER). Kopya SIFIR ofsetle uretilir
  (`document_.copyModels(orijinaller, Vec3{})`), `selectedModels_` KOPYALARA gecer; mevcut
  artimsal tasima/donusme/olcek kodu dokunulmadan kopyalari surukler. Kopya + donusum ayni
  kayitta → Ctrl+Z tek adimda kopyayi siler + hareketi geri alir. Escape:
  `gumballCancelDrag` kopyalari `document_.deleteModels(selectedModels_)` ile siler + secimi
  `gumballCopyOriginals_`e dondurur (ters donusum UYGULANMAZ; Add+Transform+Delete net sifir).
  Kapsam: etkilesim suruklemesi + tikla-sayisal edit box (eksen/duzlem/merkez/yay/olcek).
- Gumball tuzaklari (olculdu): (1) `Document::undo()` op'lari **TERSTEN** uygular, bu yuzden
  Add(kopya) → Transform → Delete zinciri net sifir verir; (2) `copyModels` Add indeksini
  `models_.size() + i` ile yazar (dogru); (3) **yerel X = kirisin kendi boyu**, uc o eksenin
  UZERINDE oldugu icin `RotX` ucu HAREKET ETTIRMEZ — donus testi `RotZ`/`RotY` ile yazilmali.
- Testler (Linux, gercek metot govdeleri cikarilarak): `python3 tests/run_gumball_render.py build`
  (sahte GDI kaydedici: renk/kalinlik/kapali yay) ve `python3 tests/run_gumball_interaction.py build`
  (135 kontrol; once `cmake --build build --target model_maker_core`).
- **Harness alan dilimi sarti:** `run_gumball_interaction.py` application.hpp'den
  `bool gumballVisible_` .. `std::optional<Vec3> anchor_` ARASINI ceker. Yeni gumball alani bu
  dilimin ICINE yazilmazsa test DERLENMEZ. Shift testi icin harness'a `VK_SHIFT`/`GetKeyState`
  stub'i + global `g_shiftDown` eklendi (0x8000 = Shift basili).
- Capraz sozdizimi kontrolunde `-DUNICODE -D_UNICODE` SART; yoksa LoadCursorW/ListView hatalari
  (sahte hata).

---

## Yerel cerceve (gumballUpdateFrame) — kritik

- `gumballFrame_[3]` (application.hpp) + `DraftView.gumballAxisX/Y/Z` (renderer.hpp).
  Renderer `gaxis` artik `draft.gumballAxis*`'tan gelir; application'daki TUM gumball
  matematigi (hit-test, beginDrag, planePoint normalleri, rotate ekseni) `gumballFrame_`
  kullanir. **`{{1,0,0},{0,1,0},{0,0,1}}` sabiti gumball kodunda YASAK.**
- Cerceve turetimi: tek profil kirisi -> ex = normalize(axisTo-axisFrom), ey/ez ise
  **`profileSectionFrame(ex)`** (TEK KAYNAK; bkz. "Profil kesit cercevesi") ve sonra
  `profileRotation` ile uye etrafinda dondurulur; tek nesne -> en uzun kenar (x) +
  ona dik en uzun kenar (y); coklu secim -> world fallback.
- **Yerel olcek**: `M = R*diag(f)*R^T` (`gumballLocalScale`, application.cpp'de file-static).
  World-eksenli `scaleModels(fac)` yerel eksende YANLIS — gumball'da kullanma. Ters =
  R*diag(1/f)*R^T, `transformModels(indices, pivot, M, Minv)` ile uygulanir.
- Dogrulama: `~/.hermes/cache/scratch/gumball_local_probe.cpp` (6/6: ortogonallik,
  rot=90 kesit, yerel X olcek yalniz uye boyunca, transformModels+undo, yerel tasima,
  KENDI ekseni etrafinda donus = uclar sabit).

---

## Gumball v2 ayrintilari

- Tutamaclar: AxisX/Y/Z, PlaneXY/YZ/ZX, Center, RotX/Y/Z, ScaleX/Y/Z, ScaleUniform.
  Renderer: yaylar 0.80L, olcek kareleri 0.55L, uniform kare merkezde, dis halka = Center.
- Hit-test onceligi: ScaleUniform(<=5px) -> Center(<=13px) -> olcek kareleri(<=7px) ->
  duzlem -> eksen oklari -> yaylar (izdusum orneklenerek; egik bakista dogru).
- Rotasyon: ekran aci unwrap (atan2 + ±pi duzeltme) + eksen isareti
  (`camera_.viewTransform(axis).z >= 0 ? +1 : -1`). Olcekleme: eksende t/|t0|, uniform
  ekran mesafe orani. Her karede ARTIMSAL adim (toplam - uygulanan) uygulanir.
- Numerik: drag surerken `onCharacter` rakam toplar; Enter `applyGumballNumeric`
  (mm / derece / faktor; 0.02..100). Duzlem/merkez tasimada yoksayilir.
- Escape: `gumballCancelDrag` ters donusumu uygular (undo kaydinda forward+inverse net 0).
- Matematik probe'u: `~/.hermes/cache/scratch/gumball_math_probe.cpp` (Camera ile ok
  110 px + 55 px surukleme = ekranda 55 px dogrulandi).
- Gorunurluk `drawingActive_`'e takilmaz (secim yeterli) — aksi halde 2B'de hic gorunmezdi.
