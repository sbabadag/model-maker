# Keyboard And Tp — model-maker

> 2026-10-19 tarihinde SKILL.md gövdesinden taşındı; içerik birebir korunmuştur.
> Tetikleyici ve kısa kural SKILL.md'deki referans dizininde.

## Klavye: F-tuslari ve SISTEM TUSLARI (F10 tuzagi)

- **F10 (polar tracking) ve Alt+<tus> SISTEM TUSLARIDIR** (MSDN: "the function keys other
  than F10" tek istisna). Windows bunlari **WM_KEYDOWN ile GONDERMEZ** — yalnizca
  `WM_SYSKEYDOWN`/`WM_SYSKEYUP` uretir. Bir F10 dalini `case WM_KEYDOWN:` icine yazarsan o
  dal HIC calismaz; tus menu aktivatörune gider ve sessizce kaybolur. Olculen regresyon
  buydu: kurdele dugmesinin araç ipucu "Polar F10" diyordu ama F10 hic ulasmiyordu.
- Bu yuzden F10 **hem** `handleCanvasMessage` (canvas proc) **hem** `handleMessage` (dis
  pencere proc'u) icindeki `case WM_SYSKEYDOWN:` icinde yakalanir ve `return 0` ile menuye
  BIRAKILMAZ (F10 menu acmasin, polar acilsin — AutoCAD davranisi).
- **Tek kaynak: `Application::togglePolarTracking()`** — kurdele dugmesi (`CmdPolarTracking`),
  F10 ve dis proc ayni yeri cagirir. Ici: toggle + acilinca `orthoEnabled_ = false`,
  kapaninca `clearTemporaryTracking()`, sonra hover/kontroller/durum/tuval yenilemesi.
- Tus haritasi: F2 = gorsel stil dongusu, F3 = OSNAP, F5 = render benchmark, F6 = GDI/GL,
  **F7 = calisma duzlemi hayaleti**, F8 = ortho, F9 = grid snap, F10 = polar tracking,
  F11 = performans katmani, F12 = dinamik giris, Ctrl+1..7 = standart gorunusler,
  Ctrl+Shift+1..4 / Alt+1..4 = stil, **TAB = TP noktasi kaydet**, Esc = TP'leri temizle.
- Test: `tests/run_polar_f10.py` (ctest `model_maker_polar_f10`) iki katman: (1) kaynak-sekli
  — VK_F10 `WM_SYSKEYDOWN` blogunda olmali, dis proc'ta da olmali, WM_KEYDOWN'da satir ici
  kopya kalmamali; (2) `togglePolarTracking()` govdesi VERBATIM cikarilip sahte Application
  ile derlenir (ortho ezme / kapanista temizleme / 4 yenileme cagrisi). Mutasyonla dogrulandi.
  Yeni bir F-tusu eklerken ayni sekle uy: sistem tusu ise SYSKEYDOWN'a.

---

## Gecici izleme (TP) katmani: TP'ler TAB ile KONUR (dwell YOK)

KULLANICI SARTI: "TP noktalarini tab tusu ile koyalim, esc tp leri sifirlar".
TP kaydi OTOMATIK DEGILDIR: eski 450 ms BEKLEME (dwell, `WM_TIMER wParam==5` +
`SetTimer(window_, 5, 450, ...)`) mekanigi KALDIRILDI — nokta kullanicinin
iradesi disinda olusuyordu. Yerine:

- TEK KAYNAK: `Application::acquireTemporaryTrackingPoint()` (application.cpp).
  Imlecin altindaki **HAM snap adayi** (`temporaryAcquireCandidate_`) kullanilir;
  `hover_->point` KULLANMA — hover polar/ortho kisitiyla degismis olabilir.
  Aday `updateHover`'da tazelenir (polar acik + obje snap'i varken; `Grid` /
  `None` aday SAYILMAZ). Ayni nokta tekrar yazilmaz; 8 nokta sinirinda en eski
  duser; kayit sonrasi `updateHover` cagrilir ki turetilmis noktalar
  (orta/dikme/kose) hemen hesaplansin.
- TUS: `VK_TAB` **hem** canvas proc `WM_KEYDOWN`'unda **hem** dis pencere proc
  `WM_KEYDOWN`'unda (odak canvas'ta degilken tus kaybolmasin). Canvas proc
  `WM_GETDLGCODE` -> `DLGC_WANTTAB` dondurur (TAB odak degistirmeye gitmesin) ve
  `onCharacter` `L'\t'` karakterini yok sayar — `TranslateMessage` WM_KEYDOWN'un
  yaninda bir de WM_CHAR kopyasi uretir; yok sayilmazsa metin girisini bozar.
- ESC: TP'leri komut iptalinden BAGIMSIZ sifirlar — Esc yolunda
  `clearTemporaryTracking()` acikca cagrilir (ayni metot Esc'ten bagimsiz olarak
  araclar tarafindan da kullanilir).
- Test: `tests/run_tp_tab.py` (ctest `model_maker_tp_tab`): sekil kontrolu
  (dwell tamamen kalkmis mi, TAB dali iki proc'ta mi, Esc temizligi, `\t`
  filtresi, DLGC_WANTTAB) + gercek `acquire`/`clear` govdelerinin sahte
  Application ile davranisi (polar kapali / snap yok / tekrar / 8 siniri / Esc).
  Mutasyonla dogrulandi (TAB dali, polar kapisi, Esc temizligi, dwell geri
  gelmesi → test duser). Harness tuzaklari: (a) govdeleri struct'in ICINE
  degil, kapanisindan SONRA (ayni namespace icinde) koy; (b) `CHECK` makrosunun
  argumaninda `Vec3{1,2,3}` YAZMA — suslu parantez virgulu korumaz, makro 4
  arguman sanip derleme hatasi verir; degeri once yerel degiskene al.

---

## Gecici izleme (TP) katmani: yeni bir turetilmis nokta turu eklerken

Iki izleme noktasi (TP) biriktiginde `resolveTemporaryPointTracking` (src/drafting.cpp)
TUREL noktalar uretir: orta nokta, sanal kose (Intersection), eksen izdusumu (Extension)
ve **dikme ayagi (Perpendicular)**. Yeni bir tur eklemek icin DORT yeri birlikte degistir;
biri atlanirsa nokta hesaplanir ama EKRANDA GORUNMEZ:
1. `TemporaryTrackingResult` (drafting.hpp) — ayri bir `std::vector<Vec3>` alani;
2. `Application::temporary<X>Points_` (application.hpp) — clear/`std::move` noktalari:
   `clearTemporaryTracking()`, `updateHover` basi (snap kapali erken cikis),
   `updateHover` takip blogu, pasif viewport temizligi;
3. `draftToView`: `view.temporary<X>Points = temporary<X>Points_;`
4. `DraftView` alani (renderer.hpp) + `renderer.cpp` cizimi (renderer.cpp cizerse GL
   kompozitinin ustune de duser; GL backend'de ayrica karsiligi YOK).

Kurallar: (a) adaylar `bestDistance` ile mesafeye gore YARISIR — esitlikte ONCE
hesaplanan kazanmali (`<`, `<=` degil), yoksa orta nokta gibi standart snap'ler ezilir;
(b) `explicitObjectSnap` true ise (acik nesne snap'i var) sonucu DEVRALMA, yalnizca
goster; (c) gorunurluk yaricapi snap yaricapindan genis olsun (4x) — nokta yaklasirken
belirir, uzakta ekran kirlenmez; (d) kilitlenen nokta her zaman GOSTERILEN nokta olsun
(snap noktasi ile isaret ayni yerde); (e) TP'ler farkli kotlarda olsa da PLANDA bir dogru tanimlar — dikme/kose hesabini koplanarlik
`continue`'sundan ONCE yap; KOSE uretimini kot farkina baglama ("ayni kot" kapisi kaldirildi,
yoksa 3B cercevede farkli yukseklikteki TP'ler kose isareti vermez). Dikme ayagi ve kose,
IMLECIN duzlemine (`cursorNormal`) konur ve snap mesafesi PLAN uzakligidir — boylece imlecin
altinda kalir ve plan snap'i calisir.
Test sekli: `tests/perpendicular_tracking_test.cpp` gibi model_maker_core'a baglanan saf
C++ testi (Python harness'a gerek yok); cagrilan fonksiyon GERCEK olmali, kopya mantik degil.
