# Model Maker

Windows için sınıf tabanlı C++20 2B/3B wireframe CAD çizim programı. Harici grafik kütüphanesi kullanmaz; Win32 ve GDI ile çizilir.

## Özellikler

- AutoCAD tarzı 2B/3B çizim araçları: Çizgi, Polyline, Dikdörtgen ve Daire
- İşleve göre gruplanmış kompakt üst ribbon: `Dosya`, `Çizim`, `Düzenle`, `Görünüm` ve `Yardımcılar` sekmeleri, küçük ikonlu komut düğmeleri, tam genişlikte GDI canvas ve durum çubuğu
- Tam AutoCAD tarzı nesne yakalama (OSNAP): endpoint, midpoint, center, geometric center, node, quadrant, intersection, apparent intersection, extension, insertion, perpendicular, tangent, nearest ve parallel; ayrıca bağımsız grid snap
- İmleç yanında dinamik koordinat / mesafe-açı gösterimi ve klavyeden hassas giriş
- `F8` Ortho Mode ile 2B'de yatay/dikey, 3B'de global X/Y/Z eksen kilidi
- AutoCAD tarzı nesne seçimi, baz noktası ve ikinci nokta akışına sahip Move ve çoklu Copy komutları
- Çizim sırasında canlı çizgi, dikdörtgen ve daire önizlemesi
- 3B küp ve piramit ekleme
- AutoCAD tarzı etkileşimli ViewCube ile üst, ön, sağ, sol, arka, alt ve izometrik görünüşler
- Boş tuvalde de çalışan `3B Görünüm (V)` düğmesi ve üç noktayla tanımlanan, yönlendirilmiş grid gösteren çalışma düzlemi
- Fareyle 3B görünümü döndürme ve tekerlekle yakınlaştırma
- 2B görünümde orta fare tuşuyla AutoCAD tarzı pan; `Zoom Extents` ile tüm çizimi ekrana sığdırma ve görünür pencere önizlemeli `Zoom Pencere`
- Wireframe dosyalarını `.mmw` biçiminde kaydetme/yükleme
- İlerleme çubuklu, iptal edilebilir ve arayüzü kilitlemeyen ASCII DXF açma/yazma; LINE, POINT, CIRCLE, ARC, LWPOLYLINE, POLYLINE/VERTEX, 3DFACE, SOLID/TRACE, TEXT/MTEXT ve BLOCKS/INSERT desteği. `DIMENSION` nesnelerinin üretilmiş anonim blokları açılarak ölçü çizgileri, ok uçları ve ölçü yazıları gösterilir. Blok taban noktası, ekleme noktası, XYZ ölçeği, Z dönüşü, satır/sütun dizileri ve iç içe bloklar açılır
- DXF katman adı ve katman görünürlüğü, BYLAYER/BYBLOCK ve ACI 1–255 renkleri, true color, lineweight, linetype/linetype scale, transparency ve entity visibility özelliklerini okuma; renk, çizgi kalınlığı ve kesik/merkez/noktalı çizgi tiplerini GDI üzerinde gösterme
- Sınıf tabanlı mimari: `Application`, `Renderer`, `Document`, `Camera`, `SnapEngine`, `WireframeModel`
- Geometri, snap, dynamic input, kamera ve dosya işlemleri için otomatik testler

## Derleme

PowerShell'de:

    cd C:\Users\Asus\Documents\MEGA\APPS\model-maker
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ctest --test-dir build --output-on-failure
    .\build\model-maker.exe

## 2B kullanım

- Üstteki `Çizim` sekmesinden küçük ikonlu `Çizgi`, `Polyline`, `Dikdört.` veya `Daire` düğmesini seçin. Çizimler ribbon altındaki tam genişlikte canvas üzerinde gösterilir.
- Noktaları fareyle seçin. ALL OSNAP açıkken mevcut geometrinin uç, orta, merkez, quadrant, kesişim, dik, teğet, en yakın, uzantı ve paralel noktaları otomatik yakalanır. GRID SNAP ayrı olarak kontrol edilir.
- Hassas mutlak nokta için `X,Y` yazıp Enter'a basın; örnek: `12.5,-3.25`.
- İlk noktadan sonra yalnızca bir uzunluk yazıp Enter'a basarsanız çizgi imlecin gösterdiği yönde tam o uzunlukta çizilir; örnek: `10`.
- Bir başlangıç noktası seçildikten sonra polar giriş için `mesafe<açı` yazın; örnek: `10<30`.
- Polyline'ı bitirmek veya aktif işlemi iptal etmek için sağ tıklayın ya da Esc'e basın.
- `Düzenle` sekmesindeki `Taşı (M)`: komutu açın, bir veya daha çok nesneye tıklayın ya da boş alanda iki köşe belirterek seçim penceresi çizin. Enter ile seçimi onaylayın, ardından baz noktasını ve ikinci noktayı belirtin.
- Soldan sağa çizilen mavi `WINDOW` penceresi yalnızca tamamen içeride kalan nesneleri seçer. Sağdan sola çizilen yeşil `CROSSING` penceresi içeride kalan veya pencere sınırına dokunan/kesişen nesneleri seçer.
- `Kopyala (K)`: aynı seçim/baz noktası akışını kullanır; her ikinci nokta yeni bir kopya üretir. Birden çok kopyadan sonra Enter ile bitirin.
- Move ve Copy ikinci noktalarında, baz noktasından imlece uzanan kesikli hareket yolu izleyicisi gösterilir; OSNAP, F8 Ortho ve tek sayı ile yönlü mesafe girişi kullanılabilir.
- `F3`: SNAP aç/kapat.
- `F8`: ORTHO eksen kilidini aç/kapat. 2B'de global X/Y, 3B'de kameranın yönünden bağımsız global X/Y/Z eksenlerinden imlece en yakın olanına kilitlenir. Aktif nesne yakalamaları kesin noktayı korur.
- `F9`: GRID SNAP aç/kapat.
- `F12`: Dynamic Input aç/kapat.
- 2B görünümde orta fare tuşunu basılı tutup sürükleyerek görünümü kaydırın; aktif çizim veya düzenleme komutu bozulmaz.
- `Görünüm > Extents`: çizimdeki tüm nesneleri kenar boşluğuyla tuvale sığdırır. Boş çizimde varsayılan 2B görünüme döner.
- `Görünüm > Pencere`: ilk ve karşı köşeyi seçerken mavi pencereyi gösterir, seçilen alanı tüm tuvale büyütür. `Esc` veya sağ tık komutu iptal eder.

## Kısayollar

- `L`: Çizgi
- `P`: Polyline
- `A`: Dikdörtgen
- `C`: Daire
- `M`: Move / Taşı
- `K`: Copy / Kopyala
- `B`: 3B küp
- `Y`: 3B piramit
- `R`: 3B görünümü sıfırla
- `V`: 2B / 3B görünüm arasında geçiş yap
- `W`: üç noktayla çalışma düzlemi tanımla
- `Delete`: tuvali temizle
- `Ctrl+S`: kaydet
- `Ctrl+O`: aç

## 3B kullanım

- `Görünüm` sekmesindeki `2B / 3B (V)` düğmesi herhangi bir nesne eklemeden görünüm arasında geçiş yapar. Aynı sekmedeki `Küp` veya `Piramit` düğmesi de nesne ekleyip 3B görünüme geçirir.
- `Görünüm > Düzlem (W)` komutunu açıp sırasıyla düzlem başlangıcını, yerel U eksenini belirleyen ikinci noktayı ve düzlem yönünü belirleyen üçüncü noktayı seçin. Noktalar OSNAP ile 3B nesne köşelerinden alınabilir; üç nokta doğrusal olamaz.
- Aktif çalışma düzlemi cyan çerçeveli yönlendirilmiş grid ve `WORK PLANE` etiketiyle görünür. Grid snap düzlemin yerel U/V koordinatlarında çalışır; çizgi, polyline, dikdörtgen ve daire bu düzlem üzerinde oluşturulur.
- 3B görünümde `Çizgi`, `Polyline`, `Dikdörtgen` veya `Daire` aracını seçerek paralel (ortografik) çalışma düzlemi üzerinde doğrudan çizebilirsiniz.
- 3B görünüm perspektif içermez: farklı derinliklerdeki eşit uzunluklar ekranda eşit ölçekte görünür.
- Fare noktaları aktif çalışma düzlemine izdüşürülür. Bir 3B endpoint/midpoint yakalandığında çizgi ve polyline gerçek 3B koordinatına bağlanır.
- Dynamic Input ile kesin 3B nokta girişi için `X,Y,Z` yazın; örnek: `2.5,-1,4`.
- Dikdörtgen ve daireler aktif çalışma düzleminin yerel U/V eksenlerinde oluşturulur.
- Sağ üstteki ViewCube, kamera ile birlikte döner ve her zaman global `X/Y/Z` eksenlerini gösterir. Küpü sol tuşla sürükleyerek görünümü orbit edin; görünür `ÜST`, `ALT`, `ÖN`, `ARKA`, `SOL` veya `SAĞ` yüzüne tıklayarak ilgili kesin standart görünüşe geçin. `ISO` izometrik görünüşe döner.
- Çizim komutu kapalıyken sol tuşla, çizim sırasında ise orta fare tuşuyla sürükleyerek kamerayı döndürün.
- Fare tekerleğiyle yakınlaşın veya uzaklaşın.
- `Esc` veya sağ tık aktif 3B çizim komutunu bitirir; görünüm 3B kalır.
