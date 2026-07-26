# Model Maker

Windows için sınıf tabanlı C++20 2B/3B wireframe CAD çizim programı. Harici grafik kütüphanesi kullanmaz; Win32 ve GDI ile çizilir.

## Özellikler

- AutoCAD tarzı 2B/3B çizim araçları: Çizgi, Polyline, Dikdörtgen ve Daire
- Profesyonel ayrık arayüz: yerel Windows kontrollerinden oluşan sol araç paneli, bağımsız GDI çizim canvas'ı ve durum çubuğu
- Nesne yakalama (OSNAP): endpoint, midpoint ve grid snap
- İmleç yanında dinamik koordinat / mesafe-açı gösterimi ve klavyeden hassas giriş
- Çizim sırasında canlı çizgi, dikdörtgen ve daire önizlemesi
- 3B küp ve piramit ekleme
- AutoCAD tarzı etkileşimli ViewCube ile üst, ön, sağ, sol, arka, alt ve izometrik görünüşler
- Fareyle 3B görünümü döndürme ve tekerlekle yakınlaştırma
- Wireframe dosyalarını `.mmw` biçiminde kaydetme/yükleme
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

- Sol kontrol panelinden `Çizgi`, `Polyline`, `Dikdörtgen` veya `Daire` seçin. Çizimler yalnızca sağdaki bağımsız canvas üzerinde gösterilir.
- Noktaları fareyle seçin. SNAP açıkken grid, mevcut çizgilerin uç ve orta noktaları otomatik yakalanır.
- Hassas mutlak nokta için `X,Y` yazıp Enter'a basın; örnek: `12.5,-3.25`.
- Bir başlangıç noktası seçildikten sonra polar giriş için `mesafe<açı` yazın; örnek: `10<30`.
- Polyline'ı bitirmek veya aktif işlemi iptal etmek için sağ tıklayın ya da Esc'e basın.
- `F3`: SNAP aç/kapat.
- `F9`: GRID SNAP aç/kapat.
- `F12`: Dynamic Input aç/kapat.

## Kısayollar

- `L`: Çizgi
- `P`: Polyline
- `A`: Dikdörtgen
- `C`: Daire
- `B`: 3B küp
- `Y`: 3B piramit
- `R`: 3B görünümü sıfırla
- `Delete`: tuvali temizle
- `Ctrl+S`: kaydet
- `Ctrl+O`: aç

## 3B kullanım

- `Küp` veya `Piramit` düğmesi 3B nesne ekleyip 3B görünüme geçirir.
- 3B görünümde `Çizgi`, `Polyline`, `Dikdörtgen` veya `Daire` aracını seçerek paralel (ortografik) çalışma düzlemi üzerinde doğrudan çizebilirsiniz.
- 3B görünüm perspektif içermez: farklı derinliklerdeki eşit uzunluklar ekranda eşit ölçekte görünür.
- Fare noktaları aktif XY çalışma düzlemine izdüşürülür. Bir 3B endpoint/midpoint yakalandığında çizgi ve polyline gerçek `Z` koordinatına bağlanır.
- Dynamic Input ile kesin 3B nokta girişi için `X,Y,Z` yazın; örnek: `2.5,-1,4`.
- Dikdörtgen ve daireler seçilen başlangıç noktasının `Z` yüksekliğindeki XY düzleminde oluşturulur.
- Sağ üstteki ViewCube'un `ÜST`, `ÖN` ve `SAĞ` yüzlerine veya çevresindeki yön oklarına tıklayarak standart görünüşe geçin. `ISO` izometrik görünüşü açar.
- Çizim komutu kapalıyken sol tuşla, çizim sırasında ise orta fare tuşuyla sürükleyerek kamerayı döndürün.
- Fare tekerleğiyle yakınlaşın veya uzaklaşın.
- `Esc` veya sağ tık aktif 3B çizim komutunu bitirir; görünüm 3B kalır.
