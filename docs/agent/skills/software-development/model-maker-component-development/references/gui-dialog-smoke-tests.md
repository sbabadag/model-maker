# GUI / dialog widget smoke tests (offscreen Qt) — model-maker

Bu dosya, bir bağlantı tipinin parametre diyaloğuna / önizleme paneline dokunduğunda
geçerli kuralları taşır. Diyalog işi `ComponentDialogWidget` + `ComponentLivePreviewWidget`
çiftinde yapılır; testleri offscreen koşar.

## 1. Linux derlemesi GUI değişikliğini KAPSAMAZ

`src/application.cpp` ve `src/qt_main_window.cpp` yalnızca Win32 `model-maker` hedefinde
derlenir. `cmake --build ... --target model_maker_core` "ninja: no work to do" derse bu
başarı değil, dosyanın o ağaçta hiç olmamasıdır (object dosyası da yoktur).

Doğrulama:

```bash
tools/win_syntax_check.sh src/application.cpp src/qt_main_window.cpp src/component_dialog_widget.cpp
# beklenen son satır: WIN32 SOZDIZIMI: YESIL
```

Shim modu her zaman koşar (host g++, `tools/winshim/windows.h`, host Qt6 başlıkları); mingw
modu Qt-for-mingw sysroot yoksa atlanır — atlanması kırmızı sayılmaz. Kapı yeşil olmadan push
yok; pre-push hook aynı kapıyı yeniden koşar.

## 2. Aynı türden ikinci widget, türle arayan testi sessizce çevirir

`findChild<QTableWidget*>()` oluşturma sırasındaki İLK çocuğu döndürür. Constructor'da
kurulan yeni bir tablo (checks/önizleme), testin kaynak (“Welds”) tablosu yerine seçilir;
eski assertionlar yanlış tabloda `item()` null iken okunur ve `QTableWidgetItem::text()`
üzerinde SIGSEGV alınır — yığın izinde üst kare `main()` olur, yani suç prodüksiyon kodunda
değil testin aramasindadir.

Kural: her tabloya `setObjectName()` ver (`checksTable`, `weldTable`) ve testte adla ara:

```cpp
auto* t = w146.findChild<QTableWidget*>(QStringLiteral("weldTable"));
```

Yeni tablo eklerken depodaki tüm `findChild<QTableWidget*>` / `findChildren<QTableWidget*>`
çağrılarını grep'le — yalnız koştuğun testte değil.

## 3. Çökmede önce backtrace, sonra bisect

```bash
QT_QPA_PLATFORM=offscreen gdb -batch -ex run -ex 'bt 20' ./build-*/model_maker_component_dialog_widget_test
```

gdb yoksa `sudo apt-get install -y gdb` (bu host'ta parolasız sudo). Yığın izni okumadan
yorum satırına alarak deneme yapmak zaman kaybı: çökme çoğu zaman testin kendi widget
erişiminde çıkar.

## 4. Şüpheli widget'ı iki koşuda ayır

1. Widget oluşturmayı yorum satırına al, yeniden derle, koş. Test geçiyorsa tetikleyici
   oluşturmanın kendisidir.
2. Oluşturmayı geri koy, tazeleme fonksiyonunun (ör. `refreshChecks`) başına geçici `return;`
   ekle, koş. Geçiyorsa hata tazeleme gövdesindedir.

Bu iki koşu "oluşturma" ile "tazeleme"yi ayırır; GUI çökmelerinin iki ana kaynağı bunlardır.
Deneme değişikliklerini geri almayı unutma — kalıcı bir `return;` fail-silent bırakır.

## 5. Fail-closed ret GÖRÜNÜR olmalı

Kullanıcının göremediği ret bug'dır. `checks[]` ihlalinde:

- komut sessizce dönmez; plan reddedilse bile diyalog açılır (Application bir
  `rejectDialogRequest` callback'i yayınlar, Qt katmanı `QTimer::singleShot(0, …)` ile açar —
  komut yürütmesinin içinden modal açma),
- önizleme panelindeki checks tablosu ihlal satırlarını kırmızı, geçenleri yeşil gösterir
  (4 sütun: Kontrol / Değer / Limit / Mesaj),
- OK tüm kontroller geçene kadar kapalıdır (`hasFailingChecks()` → `refreshChecks()`),
- durum çubuğu + startup log satırı her rette yazılır.

Geometrik redler (eğik kiriş, parça hatası) parametreyle düzeltilemez: bunlarda diyalog
AÇILMAZ, yalnız uyarı kalır. Bu ayrımı koru — "her redde diyalog" yanlıştır.

### Üç durumlu checks satırı (kırmızı / sarı / yeşil)

`ComponentCheck` üç durum taşır: `pass=false` → KIRMIZI (ihlal, OK kapalı);
`pass=true + warning=true` → SARI (bilgi/uyarı, OK AÇIK); `pass=true` → YEŞİL.
`warning` alanı UI ipucudur (digest'e girmez; checks[] zaten canonical digest'ten
`j.erase("checks")` ile çıkarılır) ama codec round-trip'inde korunur. `hasFailingChecks()`
yalnız `!pass`'e bakar — sarı uyarı OK'u kilitlemez. Örnek: varsayılan yerleşim satir
sayısını 2→1 azaltınca `bolt_rows_reduced` (pass=true, warning=true).

### boltCountY = 0 = "otomatik" sentinel (MM-7)

End Plate'te `boltCountY` artık `0` = otomatik (etkin 2 satır; küçük kesitte 1'e
azaltılır + SARI uyarı), `>0` = AÇIK (azaltma YOK, sığmazsa fail-closed).
`makeEndPlateBolts` içinde `ny = boltCountY > 0 ? boltCountY : 2`. `EndPlatePlacement`
`boltRowsReduced/Requested/Actual` taşır; generator + recipe aynı uyarı check'ini üretir
(gen == rec korunur). Kullanıcı açık satır istediyse asla sessizce azaltma.

### Reddedilen plan ÇİZİLİR + ihlal eden öğe kırmızı

Checks reddi (geometrik red değil) plan.geometry VARSA: `ComponentLivePreviewWidget` hata
metni yerine geometriyi çizer; `ComponentCheck.targetRole` ile işaretlenen bulon grubu/levha
kırmızı vurgulanır, ret nedeni afiste görünür. `targetRole`/`boltRow`/`boltCol` UI ipucu
alanlarıdır — digest'e girmez, codec round-trip'ini bozmaz. `refreshChecks` ilk ihlalin
targetRole'unu `setViolationRole` ile önizlemeye verir; checks satırına tıklayınca ilgili öğe
seçilir (satırın `Qt::UserRole` verisine targetRole'u koy, `cellClicked` ile `selectRole` çağır).

Test tarafı: bir ihlal üretmek için varsayılana güvenme (varsayılan gauge EC3'e uygun
üretildiği için geçer); kullanıcının AÇIKÇA verdiği kötü değeri kur (ör. `boltSpacingX=40`),
ihlalde `!okButton->isEnabled()` ve `checksTable->rowCount() > 0`, düzeltince
`okButton->isEnabled()` assert et.

## 6. Önizleme paneli yapısı: mod/toggle + etiket yerleşimi

- **Mod düğmesi, gizlediği panelin İÇİNDE olmamalı.** "Şematik/3B" gibi bir geçiş düğmesi
  `previewPane_` içine konursa şematiğe geçince panel (ve düğme) gizlenir, kullanıcı 3B'ye geri
  dönemez. Düğmeyi `outer`/üst çubuk gibi HER İKİ modda görünen kalıcı bir yere koy; seçili modu
  oturum boyunca hatırla (dosya-static) ve başlangıç görünürlüğünü o moda göre uygula.
- **İki modda da görünmesi gereken şey (checks tablosu / ihlal göstergesi) toggled panelin dışında
  olmalı.** `refreshChecks` ihlali 3B'de `setViolationRole` ile vurgular; tablo toggled panelin
  içindeyse şematik modda ihlal görünmez.
- **Önizleme sayısı assertion'ları yapıya bağlıdır.** Testler
  `findChildren<ComponentLivePreviewWidget*>` sayısını ve `isCompact()` ile ana/thumbnail ayrımını
  assert eder. Alt ortografik şerit gibi widget'ları ekleyip/çıkarınca bu sayıları (`==3`, `==4`)
  güncelle; eskiler sessizce kırılır.
- **Çakışmasız etiket yerleşimi deterministik ve platformdan bağımsız olmalı.** Ölçü/etiket
  metinlerini QFontMetrics ile ölçüp yerleştirme: metrik Linux'ta ve mingw'de FARKLI olduğu için
  "aynı girdi → aynı yerleşim (Linux == mingw)" kuralı bozulur. Sabit metrik (charW/lineH×dpi) +
  aday konumlar (sağ/sol/üst/alt/çapraz) + öncelik sırası + halka halka kaydırma kullan; etiket
  kutuları birbiriyle VE ölçü çizgileriyle (segment kesimi) kesişmemeli, kaydırılana kılavuz çizgisi
  çiz. Yerleşim yardımcısını Qt'siz tut (ör. `label_layout.hpp`) ki 1.0/1.5/2.0 DPI çakışma testi
  offscreen Qt olmadan da koşsun; widget testinde `grab()` ile paintEvent'i zorla ve yayımlanan
  etiket kutularını (ör. `lastLabelRects()`) kesişim için karşılaştır.

## 7. Otomatik şematik + ölçü katmanı (elle çizim yok)

- **2B şematik ELLE ÇİZİLMEZ.** `component_schematic.hpp` `GenerationPlan + MemberGeometry`'den
  ortografik izdüşüm üretir: levhalar gri (`kPlateGreyRgb`), üyeler I-kesit (h/b/tw/tf), bulonlar
  gerçek ISO çaplarında (delik d0 / gövde d / baş-somun s / pul D; tablo tek kaynak
  `mm_contract/bolt_dimensions.hpp`), kesim uç kesimine yansır, tek ölçek (mm→px). Yeni bağlantı
  eklerken şema çizimi/hotspot KODU yazma; `DialogFigure` yalnız plan yokken override.
- **Ölçü katmanı `PreviewDimension`'dan (MM-6b):** `buildComponentSchematic(plan, input, view, dims,
  &ineffective)` her ölçü için uzatma + ölçü çizgisi + etiket üretir, `prim.role` = parametre adı.
  Grup başı küçük resim ve odak/hover vurgusu AYNI motordan gelir (ayrı çizim kodu yok).
- **Test iddiası iki tuzak taşır:** (a) ölçü ekseni ekrana dikse izdüşüm uzunluğu ~0 olur ve ölçü
  ATLANIR → "her Length/Angle için ölçü var" iddiasını ÜÇ kanonik görünümde (Front/Side/Top) birden
  ara; (b) MM-6b her parametre için ölçü ÜRETMEZ → doğru iddia "ölçü var VE iki anchor çözülüyor"
  YA DA parametre `ineffective` listesinde.
- **Etkisiz parametre**: editör devre dışı + "— etkisiz" uyarı; `applyAutoVisual` sonradan enable
  etmesin diye işaretleme plan geldikten sonra (`refreshSchematic` sonunda) uygulanır.
- **Grup başı resim**: `form->insertRow(0, thumb)` ile grubun üstüne QLabel; yalnız
  `form->rowCount()>0` ise ekle, yoksa boş sekme sayısı değişir ve sekme testleri kırılır.
- **Kullanıcıya PNG**: satır içi görüntü kullanıcıya görünmez. Offscreen Qt6Gui/Core programını
  build static lib'lerine linkle (`pkg-config --cflags/--libs Qt6Gui Qt6Core`), çizimi QImage'e bas,
  sabit dizine yaz ve dosya adlarını raporda listele. Binary'yi build dizinine koy,
  `./build-.../ad` ile çalıştır (`$HOME/...` yürütme yolu onay/tarama takılır).
