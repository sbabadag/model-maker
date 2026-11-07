# Snap turleri — yeni tur ekleme + SANAL PROFIL MERKEZI

## SANAL PROFIL MERKEZI (SnapType::ProfileCenter)

KULLANICI SARTI: "profilin tam merkezinde bir sanal snap noktasi olustur".

- Aday uretimi `src/drafting.cpp` `objectCandidates` icinde, model dongusunun
  SONUNDA:
  `if (!model.properties().profileName.empty() && modelIndex < modelBounds.size())`
  -> `addCandidate(candidates, (bounds.minimum + bounds.maximum) * 0.5,
  SnapType::ProfileCenter, tolerance, metric);`
- Merkez kaynagi `Document::modelBounds()` — **ONBELLEKLI AABB**, model
  indeksiyle paralel (`ensureSpatialIndex()` cagirir). Donguden ONCE bir kez
  `const auto& modelBounds = document.modelBounds();` al. IKINCI bir sinir
  kutusu hesabi YAZMA (mutasyon M10 bunu hedefler).
- Yalniz PROFIL elemanlari (`profileName` dolu). Duz cizgi/kutu/silindir
  etkilenmez.
- Gercek kose/kenar DEGILDIR -> `visibleEdges` (Solid gorunurluk filtresi) ve
  vertex/edge listelerinden BAGIMSIZ; 2B `snap()` ve 3B `snap3D()` ayni sekilde
  uretir; Wireframe/Solid ayni sonucu verir.
- Simetrik kesitte merkez = kesit agirlik merkezi + boyuna orta nokta (profil
  ust yuzunden oturur, govde asagi sarkar; AABB merkezi tam kesit ortasidir).
- TUZAK: DUSEY kolonda PLAN gorunuste merkez, ust yuz kenar orta noktasiyla
  ekranda CAKISIR; oncelik (Midpoint=4 < ProfileCenter=6) + derinlik (yakin
  kazanir) yuzunden Midpoint kazanir. Merkezi ayirt etmek icin testte
  ELEVASYON gorunusu kullan (`WorkPlane::fromViewDirection`).
- Tur tablolari: `priority()` = 6 (merkez ailesi), etiket `L"PROF CENTER"`,
  marker `CircleCross`, renk `0x00FF40FF` (parlak magenta; GeometricCenter'dan
  FARKLI olmali).

## YENI SNAP TURU EKLEME — 7 DOKUNMA NOKTASI (biri atlanirsa sessiz kalir)

1. `include/model_maker/drafting.hpp`: `enum class SnapType` **SONUNA** ekle
   (araya SOKMA — mevcut turlerin indeksleri ve sabit maskeler kayar).
2. Ayni dosya: `using SnapTypeMask = std::array<bool,
   static_cast<std::size_t>(SnapType::EN_SON) + 1>;` — boyut EN SON turden
   turetilir, ciplak sayi yazma.
3. `src/drafting.cpp` `priority()`.
4. `snapTypeLabel()`.
5. `snapMarkerSymbol()` — yeni sembol gerekmiyorsa mevcut birini PAYLAS
   (CircleCross). YENI sembol eklersen ek olarak `src/renderer.cpp`
   `drawSnapMarkerShape()`, `tests/snap_marker_cases.cpp` sablonu ve
   `tests/run_snap_marker_scale.py` sembol listesi guncellenmeli.
6. `snapMarkerColorRgb()` — COLORREF duzeni `0x00BBGGRR`; ayirt edilebilir renk.
7. UI: `src/application.cpp` `snapChoices` (Win32 OSNAP listesi) +
   `Application::kSnapChoiceCount` (`application.hpp`; **PUBLIC** olmali —
   `application.cpp`'deki serbest fonksiyon static_assert'i private uyeye
   erisemez, capraz kontrol bunu yakaladi) + `src/qt_main_window.cpp`
   `addSnapItem(...)`. Sabit + liste `static_assert` ile DERLEME ZAMANINDA
   kilitli.

Aday uretimini ilgili yere ekle; `choose(enabledTypes)` maskesinin kapi
oldugunu unutma (tur kapatilinca aday uretilmez).

## Test deseni

- **Davranis:** `tests/<konu>_test.cpp` (`model_maker_core` linkli, OCC'siz,
  Linux'ta kosar) — GERCEK `SnapEngine::snap/snap3D` cagirir, kopya mantik yok.
  Kapsam: aday var/yok, profil kapisi, maske kapisi, uc onceligi, tolerans
  siniri, coklu nesnede EN YAKIN, 2B + 3B, Wireframe/Solid esdesligi, tur
  tablolari, maske boyutu, "AABB mi vertex ortalamasi mi" kaniti.
- **Kaynak sekli + UI:** `tests/run_<konu>.py` — derleme gerektirmez; enum
  sirasi, tek-kaynak maske boyutu, static_assert, Win32 listesi, Qt menusu.
- **Mutasyon:** `tests/mutate_<konu>.py` — her mutasyon icin denetleyici sec
  (`cpp` = hedefi derle + kos, `py` = hizli kaynak kontrolu). Yakalanmasi
  gerekenler: aday uretimini kapatmak, AABB yerine vertex ortalamasi, profil
  kapisini kaldirmak, etiketi bosaltmak, rengi GeometricCenter ile ayni yapmak,
  maske boyutunu `Parallel + 1`'e dondurmek, ikinci bir bbox hesabi eklemek.
  Sonda dosyalari BAYT-BIREBIR geri yaz ve dogrula.

## SNAP MOTORU — 2026-10 REGRESYONLARI (hepsi olculdu + testle kilitli)

- **Grid kesisim yedegi TOLERANS KAPILI olmali.** `snap()`/`snap3D` sonundaki
  "en yakin kavsak" bloklari toleranssizdi: imlec 3,9 m uzakta bile Grid
  donduruyordu (kullanici: "cok uzaktan aliyor, diger snapleri override
  ediyor"). Kapi: 2B `bestDist <= objectTolerance`, 3B `bestPx <=
  objectTolerancePixels`. Blogu SILME (silinirse "grid hic yapismiyor").
  Test: `grid_snap_distance_test`.
- **Obje + grid adaylari TEK listede.** Ayri "obje bulunduysa hemen don"
  yapisi, tolerans icindeki uzak obje ucunun (49 mm) tam uzerinde durulan
  grid aks ucunu (7 mm) ezmesine yol acar. `addGridLineCandidates` ayni
  vektore eklenir, tek `choose` (mesafe -> oncelik -> derinlik).
- **YAN/ON gorunuste snap TAMAMEN olur** eger is-duzlemi bakis isinina
  paralel kalirsa: `snap3D` once `unprojectToPlane` yapar, kesismez, erken
  `None` (tek bir tur bile uretilmez). `updateHover` 3B dalinda
  |dot(plane.normal, camera.gridViewDirection())| < 0.05 ise
  `WorkPlane::fromViewDirection(origin, viewDir)` kullan. Test:
  `side_view_snap_test` (yan+on, tuzak dahil).
- **UCS (3 nokta) sonrasi profil yonelimi:** yeni uyenin kesit referans
  normali = is-duzlemi NORMALI; ekseni duzleme DIK olan uyede (kolon)
  duzlem-ici `u`. `addStyledModel` `sectionNormal*` alanlarini EZMEMELI
  (sadece katman/renk gibi stil alanlari gelir). Test:
  `workplane_member_orientation_test` + `haunch_portal_face_test`.

---

<!-- SKILL.md'den taşındı 2026-10-19 (gövde sadeleştirme) -->

## Ortho + OSNAP (F8) — obje snap'i ortho'yu EZER

KURAL (AutoCAD davranisi, kullanici sarti): F8 ortho acikken obje snap'i (uc/orta/kesisim/dikme)
KAZANIR; nokta snap'e oturur, `hover_->type` snap tipini KORUR. F8 yalnizca SERBEST imleci ve
**grid snap'ini** (grid noktasi eksene tasinir) en yakin eksene kilitler.

- TEK KAYNAK: `Application::applyOrthoConstraint(anchor, candidate, x, y)` (application.cpp).
  `updateHover` icinde SATIR ICI `applyOrtho`/`applyOrtho3D` cagrisi YAZMA; uc dal (2B /
  3B-dunya eksenleri / 3B-is-duzlemi(UCS)+transform) hepsi bu metottan gecer ve HEPSI
  `preserveObjectSnaps=true` gecirir.
- OLCULEN REGRESYON (kullanici: "ortho mode acikken snap tutmuyor"): 3B'deki UC dal da
  `applyOrtho3D(..., false)` geciyordu; `preserve=false` bulunan snap'i EZIP noktayi eksene
  tasiyor ve `type=None` yapiyordu. 2B'de de transform sirasinda `transformCommand_ == None`
  ifadesi false uretiyordu. Cozum: her dalda `true`.
- Ayrim gercek: `preserve=true` → `Endpoint` korunur; `preserve=false` → tip `None` + nokta
  eksene kayar. `Grid` snap tipi obje snap'i SAYILMAZ, ortho onu kisitlar.
- Testler: `tests/ortho_snap_core_test.cpp` (ctest `model_maker_ortho_snap_core_test`, 13 kontrol,
  GERCEK `applyOrtho`/`applyOrtho3D`'yi cagirir, OCC'siz/platformdan bagimsiz) ve
  `tests/run_ortho_snap.py` (ctest `model_maker_ortho_snap`; kaynak-sekli + `applyOrthoConstraint`
  govdesi VERBATIM cikarilip sahte Win32 sinirinda 4 dalin dordunde de `preserve=true` oldugu
  dogrulanir). Mutasyon: bir dalda `false` → iki test de duser.

---

## SNAP secimi: EKRAN MESAFESI -> ONCELIK -> DERINLIK (plan gorunusu tuzagi)

Kullanici: "plan goruntulerinde ... mid point snap bazen calismiyor". IKI bagimsiz kok neden
(gercek `SnapEngine` + OpenCASCADE ile olculdu; tahmin degil):

1. **`choose()` derinlik bakmiyordu.** Plan/ust gorunuste GORUNUR ust kenar ile ALTINDAKI gizli
   kenar AYNI ekran noktasina iz dusurulur; ekran mesafesi birebir esit cikar ve eski kod ilk gelen
   adaya gidiyordu → kenar listesi sirasina bagli olarak GIZLI kenar kazaniyordu. OLCUM (IPE200,
   plan): ust kenar ortasi `z=0` yerine `z=-8.5` / `-200` donuyordu. FIX: karsilastirici artik
   `mesafe -> priority -> DERINLIK`; esit oncelikte `camera.viewTransform(point).z` BUYUK (izleyiciye
   YAKIN) olan kazanir. Konvansiyon satir ~558'deki kati-kati cakisma kuraliyla AYNI.
   `choose(candidates, raw, enabledTypes, const Camera* depthCamera = nullptr)` — 2B `snap()`
   `nullptr` gecer (davranis degismez), `snap3D` `&camera` gecer.
2. **Solid gorunurluk filtresi kenarlari YANLIS eliyordu.** (a) Yuzu OLMAYAN modelde (duz cizgi,
   polyline — varsayilan Solid stilde snap'in TEMEL durumu) `visibleEdges` BOS kalir; eski kosul
   `!visibleEdges.empty() || (screenSpace && visibleEdgesOnly)` bu durumda TUM kenarlari eliyordu →
   orta nokta/nearest/perp snap'i KAYBOLUR. FIX: filtre yalniz `!visibleEdges.empty()` ise uygulanir
   (siniflandirma yoksa filtre yok). (b) **OCC tesselasyonunun normal yonu GARANTI DEGIL** — olcum:
   IPE200'un UST yuzu `-Z` normalli geliyordu, gorunur yuz "gizli" sayilip kenarlari eleniyordu.
   FIX: normal, yuz merkezinden MODEL merkezine gore DISA yonlendirilir.
- Testler: `tests/snap_depth_pick_test.cpp` (ctest `model_maker_snap_depth_pick_test`; 14 kontrol,
  OCC'siz, GERCEK `snap3D`; yuzsuz Wireframe/Solid, duz cizgi, ters-normal kati, oncelik korunmasi,
  yan gorunus silueti) + `tests/mutate_snap_depth_pick.py` (5/5 yakalar).
- **Test kurarken iki tuzak:** (i) `snap3D` once `camera.unprojectToPlane` yapar; YATAY bakista
  YATAY is duzlemi (`WorkPlane::world()`, z=0) bakisa PARALELdir → isin kesismez → erken `None`.
  Yan/on gorunus testlerinde `WorkPlane::fromViewDirection(origin, viewDir)` kullan. (ii) Kamerayi
  kurmak yetmez: test edecegin noktanin ekranda KALMASI icin `setViewBasis` merkezini o noktaya
  yakin sec (yoksa nokta ekran disina duser ve snap bos doner).

---

## SANAL PROFIL MERKEZI SNAP (ProfileCenter) — "profilin tam merkezi"

KULLANICI SARTI: "profilin tam merkezinde bir sanal snap noktasi olustur".
**DETAY + "YENI SNAP TURU EKLEME — 7 DOKUNMA NOKTASI" listesi:
`references/snap-types.md` (OKU).**

- Aday: `profileName` dolu her model icin AABB merkezi; kaynak
  `Document::modelBounds()` (ONBELLEKLI) — ikinci bbox hesabi YAZMA. Gercek
  kose/kenar DEGIL: `visibleEdges` filtresinden bagimsiz, 2B/3B ve
  Wireframe/Solid ayni. DUSEY kolonda plan gorunuste Midpoint ile cakisir
  (merkezi ayirt etmek icin ELEVASYON gorunusu kullan).
- `Application::kSnapChoiceCount` **PUBLIC** olmali: `application.cpp`'deki
  serbest fonksiyon static_assert'i private uyeye erisemez (capraz kontrol
  yakaladi; Linux ctest bunu GORMEZ).
- Testler: `profile_center_snap_test.cpp` (26 kontrol),
  `run_profile_center_snap.py` (kaynak sekli + UI),
  `mutate_profile_center_snap.py` (10/10).

---

## Snap isaretcileri (marker) — TEK kaynak + olcek sabiti

- Marker cizimi renderer.cpp'de IKI KEZ kopyalanmisti (motion overlay yolu + normal drafting yolu);
  ikisi de `switch (snapMarkerSymbol(draft.snapType))` ile ayni 15 sembolu ciziyordu. Boyut/renk
  degistirirken IKISINI de duzeltmek gerekir — tek kaynak kurali: `drawSnapMarkerShape(dc, p, symbol)`
  (anonim namespace, `line()` hemen ardinda) ve iki cagri yeri yalniz onu cagirir.
- Boyut tek sabitten gelir: `constexpr int kSnapMarkerScale` + `constexpr int sm(int v)`. Kullanicinin
  "buyuklugunu iki katina buyutelim" istegi = `kSnapMarkerScale = 2` (taban yaricap 7 px → 14 px).
  Kalem kalinligi (CreatePen 2) BOYUT degil; kullanici ayrica istemedikce dokunma.
- Etiket ofseti de olcege bagli olmali (`p.x + sm(10), p.y + sm(8)`); sabit birakilirsa buyuyen marker
  etiketi ezer.
- Test: `tests/run_snap_marker_scale.py` (kaynak-sekil + gercek yardimci govdesi kayit tutan GDI
  sinirinda) `tests/snap_marker_cases.cpp` sablonunu kullanir. Sablon, TARIHI 1x geometriyi literal
  olarak tutar ve beklenen geometriyi `x2` ile TURETIR — "iki kat" iddiasi boylece helper'a guvenmeden
  dogrulanir. Sembol basina aciklik (span) kontrolu de tam 2x olmali; "merkezde kalmali" iddiasi
  YANLIS olur (RightAngle glifi tasarim geregi ortalanmis degil).
- Harness'a enum'u tanit: `using mm::SnapMarkerSymbol;` cagrisi `@HELPER_BLOCK@` marker'INDAN ONCE
  gelmeli, yoksa "'SnapMarkerSymbol' has not been declared".
- Mutasyon kosucusu: `tests/mutate_snap_marker_scale.py` (M1 olcek=1, M2 sm=kimlik, M3 bir yol
  yardimciyi cagirmiyor, M4 etiket olceksiz) — 4/4 yakaliyor.
