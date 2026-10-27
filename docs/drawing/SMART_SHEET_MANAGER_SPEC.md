# Model Maker Drawing — Smart Sheet Manager gereksinimleri

**Durum:** Sonraki geliştirme aşaması için kabul edilmiş ürün gereksinimleri (27 Ekim 2026). Bu belge özelliğin şimdiden geliştirildiği anlamına gelmez.

## Kesinleşen varsayılanlar

| Seçenek | Değer | Kural |
| --- | --- | --- |
| `sheetSize` | `auto` | Gereken tüm görünüş, ölçü, poz, not, antet ve boşlukları dikkate alarak uygun ISO A-serisi pafta seç. |
| `drawingMode` | `single` | Öncelik, imalat için eksiksiz ve okunaklı **tek pafta**. |
| `autoMultiSheetFallback` | `true` | Tek pafta kalite koşullarını sağlayamıyorsa **ana paftaya bağlı çoklu pafta** üret. |

Kullanıcının seçtiği manuel pafta boyutu varsa bu sınırları aşma; geçerli tek-pafta düzeni yoksa izin verilen çoklu-pafta seçeneğini değerlendir veya açık hata ver. Kullanıcı tercihleri çizimle kaydedilmeli.

## Temel hedef

3B model seçildiğinde **kullanıcının elle pafta düzeltmesi gerekmeyecek**, doğrulanabilir imalat ölçülerine sahip, baskıda okunaklı pafta üretmek. Sonradan eklenecek 2D editör, otomatik yerleşimin kusur düzeltme zorunluluğu değil opsiyonel kişiselleştirme aracı olacak.

Ölçüler 3B modelin gerçek imalat özelliklerine dayanır: profillerin boy/kesim bilgisi, levha ebatları ve kalınlığı, kaynak/bulon bilgisi, delik merkezleri ve çapları, düşey/yatay delik dizileri, delik-kenar net mesafeleri, üyelerin yerel datumuna göre levha X/Y/Z konumları. Eksik veya güvenilir biçimde çıkarılamayan zorunlu bilgi **INCOMPLETE** olarak işaretlenir; üretim hazır damgası vurulmaz.

## Çözüm algoritması (karar sırası)

1. Önce imalat özelliklerini ve tüm zorunlu ölçü/poz/etiketleri çıkar. Yerel 3B koordinat ve gerçek ölçüleri sabitle; pafta algoritması bunları değiştiremez.
2. Görünüş geometrisiyle birlikte **kağıt mm cinsinden** yazı kutuları, ölçü çizgisi/uzantıları, oklar, kaynak sembolleri, poz etiketleri, leader yolları, notlar ve antet için çarpışma geometrisi üret.
3. **Tek pafta araması:** standart ölçekleri, görünüş dizilişlerini, ölçü taraflarını ve etiket konumlarını değerlendir; sabit projeksiyon ilişkilerine uy. `auto` boyutta A3'ten başlayıp ihtiyaç halinde A2/A1/A0 boyutlarını dene. Uygunsa tek pafta kazanır.
4. Tek pafta, gerekli tüm ölçülerle ve min. yazı yüksekliğiyle çözülmüyorsa `autoMultiSheetFallback=true` için `DrawingSet` üret: bir ana pafta, ilgili devam/detay paftaları.
5. **Final kalite geçidi:** geçersiz çakışma = 0; zorunlu ölçü atlama = 0; minimum baskı yazı boyutu korunmuş; sınır/antet taşması = 0; detay referansları çözülmüş. Koşulları sağlamayan çıktı `READY` sayılamaz. Çıktı deterministik ve tekrar üretilebilir olmalı.

**Hard constraints:** Mühendislik ölçü doğruluğu, üretim veri kapsamı veya INCOMPLETE uyarısı, ölçü yazısı okunurluğu (örnek konfigürasyon: ölçü yazısı >=3.0 mm, not >=3.5 mm), kritik çakışmasızlık, pafta sınırları, view projection relations, kaynak EntityId / geometry version bağlantıları ve pafta referans bütünlüğü.

**Soft objectives:** tek pafta, mümkün olan en küçük uygun format, sade grup delik çağrıları, az leader kesişimi, dengeli görünüş yerleşimi, standart ölçek, az boşluk, simetri, kararlı yerleşim. Tek sayfa uğruna mikroskobik yazı, yanlış ölçü veya eksik detay **yasak**.

## Ana pafta ve bağlı çizimler

Var olan `mm::drawing::DrawingDocument` / `DrawingSheet` ile uyumlu bir `DrawingSet` veya eşdeğer veri modeli tasarlanacak; paralel, çelişen ikinci bir drawing modeli eklenmeyecek.

- Kalıcı `DrawingSetId`, ana pafta kimliği, sıralı bağlı paftalar, kaynak model EntityId'leri ve revizyon/üretim ayarları.
- Her paftada tip: `primary`, `continuation` veya `detail`. Bağlı pafta, ana paftanın numarası/revizyon politikası ve proje metadatasını paylaşır.
- Örnek numaralama: `A100` ana pafta; `A100-01` delik detayları; `A100-02` levha detayları (yalnız örnek, sabit format değil).
- Ana pafta bağlı sayfaların indeksini gösterir; detay çağrıları hedef pafta/görünüşe çift yönlü geçerli referans verir.
- Model değişince ilişkilendirilmiş ölçüler yeniden hesaplanır, görünüş/pafta bağlantıları doğrulanır; mümkün olduğu kadar kullanıcı onaylı yerleşim tercihleri korunur.
- JSON / proje kalıcılığı ileri-geri uyumluluğu ve tüm paftaları vektör kalitede PDF olarak dışa aktarma gerekir.

## Ölçü ve yerleşim motoru için kabul testleri

1. Varsayılanlar deserialize/serialize turunda tam korunur: `auto / single / true`.
2. Sade kiriş/levha/bağlantı çizimi tek okunaklı paftaya yerleşir; sayı ve konumlar doğru.
3. İki bağımsız bulon grubunun delik Ø, yatay/düşey aralıklar, üst/alt net kenar, levha X/Y offset ölçüleri doğru; hiçbir kritik ölçü yazısı üst üste gelmez.
4. Kalabalık bir montaj uygun standart ölçek ve daha büyük pafta ile tek sayfada çözülürse çoklu sayfa üretilmez.
5. Tek sayfada kalite koşulları sağlanamayan montaj otomatik olarak ana+detay paftalara ayrılır; ana pafta indeksi ve karşılıklı referanslar tutarlıdır.
6. Uzun metin, değişen font metrikleri, dar bölgeler, kenara yakın delikler, leader yoğunluğu, A3/A2/A1 yönleri ve baskı PDF'si çarpışma kontrollerinden geçer.
7. Yeniden oluşturma ve save/load işleminde mark/EntityId eşlemesi, çizim-set bağlantısı, kullanıcı tercihleri ve deterministik yerleşim korunur.
8. Desteklenmeyen gerekli imalat geometrisinde net INCOMPLETE bayrağı bulunur; çizim eksik bilgiyi uydurmaz.
9. Haunch 40 ve mevcut P1 Windows preview / Linux core testleri regresyonsuz çalışır.

## İş bölümü ve güvenli geliştirme

**P2A şu anda devam ediyor:** semantic fabrication feature -> ölçü üretimi -> collision-aware layout. Bu dokümanla kapsamına yeni kod ekleyip koşan oturumla yarışmayın.

**Sonraki aşama P2C — Smart Sheet Manager:** pafta seçim politikası, tek-pafta optimizasyonu, `DrawingSet`/bağlı sayfalar, otomatik multi-sheet fallback, kalite geçidi, testler.

**Daha sonra P2B — Optional 2D Editor:** ölçü/not/leader/görünüş ve 2D elemanları kağıtta elle düzenleme; ana ölçü değeri ve model geometrisi kullanıcı kağıtta taşıdı diye değişmez. Kağıt düzenleme zorunlu kalite tamiri olmayacak.

Koruma: `main` ve Haunch 40'a doğrudan müdahale yok. Aktif Hermes worktree'lerinde eşzamanlı yazanlar kontrol edilmeli. Her geliştirme ayrı feature branch/worktree ile commit/test edilmeli.
