# ROADMAP-UI — C onaylandı + UI-3/4/5/6b'nin TAM tarifi (MESAJ-14'ten)

> Kaynak: kullanıcı MESAJ-19 (`C:\work\HERMES-MESAJ-19.md`); içerik aslına sadık.

C onaylandı (116b85c, 78ba545, c2862ae). Windows'ta doğrulayacağım.

Spek var: MESAJ-14'te yazılıydı, oturumun onu görmemiş. Aşağıda UI-3, UI-4,
UI-5, UI-6b'nin tam metni var; ayrıca MESAJ-18'deki K1/K2 ve "profilsiz çizgi
yok" kuralı. Bunu docs/agent/ROADMAP-UI.md olarak repoya commit'le ki bir daha
oturum değişince kaybolmasın (ayrı commit). Sonra UI-3'ü tam yap.

Genel kurallar (MESAJ-14): Tekla'ya benzer iş akışı, ama Tekla ikonları/
resimleri/marka adları YOK; ikonlar bizim; widget'lar platformdan bağımsız
ayrı dosyalarda, ICommandHost + UiSnapshot ile; qt_main_window yalnız ince
bağlantı; yeni widget .cpp'leri exe hedefine (kapı testi zaten var).
Çalışma düzeni MESAJ-18'deki gibi: DURAK 1 = UI-3 sonu, DURAK 2 = UI-5 sonu.

## UI-3 — Özellikler paneli (sağda, yerleşik)
- Başlık: "<nesne tipi> (N seçili)". Seçim değişince güncellenir.
- Daraltılabilir bölümler. Kiriş/kolon için:
  - **Genel:** Ad, Profil [metin + "..."], Malzeme [metin + "..."], Sınıf
    (renk kutulu açılır liste), Yüzey işlemi
  - **Numaralandırma:** parça öneki + başlangıç no, montaj öneki + başlangıç
    no, mevcut numara (salt okunur)
  - **Konum:** Düzlemde (Orta/Sol/Sağ) + mm, Dönüş (Üst/Ön/Alt/Arka) + derece,
    Derinlik (Önde/Ortada/Arkada) + mm — mevcut üye konum alanlarıyla eşleşen
    neyse; olmayanı rapora yaz, uydurma
  - **Uç ofseti:** Başlangıç / Bitiş için Dx Dy Dz
  - "Daha fazla / daha az özellik göster" bağlantısı
- Alt çubuk: **Uygula/Değiştir** düğmesi (değişiklik yoksa pasif). Değişiklik
  tek geri alma adımı.
- **Çoklu seçim:** değerleri farklı olan alan boş görünür; yalnız
  değiştirilen alan tüm seçime uygulanır.
- **Profil alanı elle yazılır:** katalogda ad eşleşmesi büyük/küçük harf
  duyarsız (`hea300` → `HEA300`), yazarken otomatik tamamlama. Katalogda
  yoksa alan kırmızı + mesaj, uygulanmaz. Malzeme alanı aynı kural.
- Levha, bulon, kaynak, komponent için kendi bölümleri (mevcut özellik
  diyaloglarındaki alanlar). Mevcut ayrı özellik diyalogları bu panele taşınır;
  çift tıklama paneli açar.
- Testler: tek/çoklu seçim, geçersiz profil reddi, uygula → tek geri alma,
  profil değişince geometri güncelleniyor.


Ek olarak:

### K1 — Tüm değiştirme (modify) pencereleri modsuz
- Özellik / değiştirme / komut diyalogları (Taşı-Δ, Kopyala-Δ, Doğrusal
  dizi, Dairesel dizi, komponent diyaloğu, ızgara, profil seçimi vb.)
  açıkken ana pencereye müdahale edilebilsin: seçim, zoom/pan/döndürme,
  nokta alma, görünüm değiştirme.
- Diyalog o anki seçim üzerinde çalışır (Uygula anında okunur); seçim
  değişince içerik kendiliğinden ezilmez (A kuralı: yalnız Uygula'da yazar).
- Bir diyalog türünden aynı anda tek pencere; tekrar açılırsa öne gelir.
- Test: diyalog açıkken seçim değişiyor, zoom çalışıyor; Uygula yeni seçime
  uygulanıyor; iki kez Uygula → iki ayrı geri alma adımı.

### K2 — Taşı / Kopyala / dizi diyaloglarında "Temizle"
- Taşı-Δ, Kopyala-Δ, Doğrusal dizi, Dairesel dizi: "Temizle" düğmesi tüm
  değer alanlarını varsayılana döndürür (dX dY dZ = 0, adet = 1 vb.; her
  alanın varsayılanı belgelensin).
- Alan elle tamamen silinip boş bırakılırsa o alan sıfır/varsayılan
  anlamına gelir (hata değil, önceki değer de değil).
- Test: Temizle → tüm alanlar varsayılan; dY boş → Uygula dY = 0; boş adet
  → 1.

**→ DURAK 1: UI-3 sonunda rapor, Windows doğrulamamı bekle.**

### Profilsiz çizgi yok (UI-3 içinde ayrı commit; MESAJ-16'daki tarif)
- Profil listesinde "Profilsiz (düz çizgi)" seçeneği yok; Kolon/Kiriş her
  zaman katalog profili ister.
- Çizgi, Polyline, Dikdörtgen, Daire, 3DFACE yalnız YARDIMCI OBJE (B6 komut
  tarafını yaptı): parça değil; numaralandırma, rapor, komponent girdisi ve
  Model Ağacı "Profiller"e girmez; ayrı görünür (ince kesikli, tek renk),
  ayrı gösterilip gizlenebilir.
- Eski dosyalar: profilli çizgiler üye kalır, profilsiz çizgiler yardımcı
  objeye dönüşür; sürümlü migration + test; geometri değişmez.
- Çizim sekmesi adı: "Yardımcı" öner/uygula (varsayım olarak işaretle).

## UI-4 — Profil seçim penceresi ("..." düğmesi)
- Sol: profil ağacı, aileye göre (HEA, HEB, HEM, IPE, UPN, kutu, boru, …);
  üstte **Filtre** (joker `*`, ör. `HE*3*`) + Filtre düğmesi.
- Üst: Profil adı alanı (yazılabilir, ağaçla senkron).
- Sağ: profil tipi/alt tipi, kesit alanı, **kesit resmi** (kendi şematik
  motorumuzla, ölçü harfleri h b tw tf r ile), özellik tablosu
  (Özellik / Sembol / Değer / Birim: h, b, tw, tf, r1, r2 …; katalogda ne
  varsa).
- Alt: "Tüm profilleri göster", "Ayrıntıları göster"; **Tamam / Uygula / İptal**.
  Uygula paneldeki alana yazar, pencere açık kalır.
- Malzeme "..." için aynı yapıda basit seçim penceresi (malzeme listesi).
- Testler: filtre, ağaç ↔ ad senkronu, Tamam/Uygula/İptal davranışı,
  tablodaki değerler katalogla aynı.


## UI-5 — Sağ dikey sekme şeridi + Uygulamalar ve komponentler paneli
- Sağ kenarda dikey ikon şeridi: Özellikler, Uygulamalar ve komponentler
  (ileride başka paneller). Tıklanınca ilgili panel açılır/kapanır.
- **Uygulamalar ve komponentler paneli:** arama kutusu; gruplar
  (Son kullanılanlar, Bağlantılar, Detaylar, Paketler — yerleşik tipler +
  `components` klasöründen yüklenen paketler); öğe seçilince komponent
  diyaloğu (mock view'li) açılır. "Gizli öğeleri göster" onay kutusu.
- **Özellikler paneli gibi yerleşik (dock) panel:** aynı dock sistemi;
  sabitlenebilir, yüzdürülebilir, genişliği ve açık/kapalı durumu
  hatırlanır.
- **Komponent sembolleri:** her komponentin listede küçük resmi (simgesi)
  olsun. Simge elle çizilmeyecek; MM-8b şematik motoruyla komponentin
  temsili örnek girdisinden (8728f5a'daki katalog örneği) OTOMATİK
  üretilsin, önbelleğe alınsın (typeId + şema hash'i). Paketler de aynı
  yoldan simge alır. Tekla resmi kullanılmaz.
- **Görünüm:** ızgara (büyük simge + ad) ve liste (küçük simge + ad)
  arasında geçiş düğmesi.
- **Kullanım doğrudan panelden:** öğeye çift tıklama ya da "Uygula"
  → komponent yerleştirme komutu başlar (ana üye, ikincil üyeler seçimi;
  durum çubuğunda adım mesajı). Tek tıklama → komponent diyaloğu
  (mock view'li) panelde ya da yanında açılır, varsayılan parametreler
  oradan ayarlanır. Modeldeki bir komponent seçiliyken panel o tipi
  vurgular.
- Testler: arama, paket yükleyiciden gelen paketin listede simgesiyle
  görünmesi, simgenin otomatik üretilmesi (elle resim yok), çift tıkla
  yerleştirme komutunun başlaması, diyaloğun açılması, dock durumunun
  hatırlanması.


UI-5'e ek: komponent paneli araması ve şerit hızlı araması aynı indeks (yerleşik 4 tip + yüklenen paketler).

## UI-6b (UI-6a = C, bitti) — Alt çubuk: seçim, filtre ve snap
Pencerenin altında, şeridin tamamlayıcısı olan sabit bir araç çubuğu:
- **Seçim anahtarları** (aç/kapa ikonlar): her şeyi seç, parçalar, bulonlar,
  kaynaklar, komponentler, akslar, noktalar, kesimler vb. Mevcut seçim
  kodunda hangi nesne tipleri varsa onlar.
- **Montaj / parça seçim modu:** tıklama tek parçayı mı, parçanın ait
  olduğu montajı mı seçsin (ve komponent / komponent içi nesne). Bir
  geçiş düğmesi + kısayol; durum çubukta görünsün.
- **Seçim filtresi:** açılır liste (ör. "standart"); kayıtlı filtreler
  (profil adı, malzeme, sınıf, ad gibi özelliklere göre). Filtre seçimi ve
  kutu seçimini aynı anda kısıtlar. Filtre tanımı ilk adımda basit olabilir;
  ne yaptığını raporla.
- **Snap anahtarları:** uç, orta, kesişim, dik, en yakın, merkez, çizgi
  üzeri, aks noktası vb. — mevcut snap modlarının hepsi, her biri aç/kapa.
  Mevcut "SNAP OVERRIDE" sağ tık menüsü (f5a8e99'daki) aynı kayıttan
  beslenmeli; iki ayrı snap durumu olmasın.
- **Snap derinliği / düzlem:** "Otomatik", "Görünüm düzlemi" gibi mevcut
  seçenekler açılır listede.
- **Durum alanı:** "N nesne ve M tutamaç seçili", aktif komut mesajı, faz.
- Tüm anahtar durumları oturumlar arası hatırlansın.
- Testler: her seçim anahtarı ilgili tipin seçilebilirliğini değiştiriyor;
  montaj modunda tıklama montajı seçiyor; filtre kutu seçimini kısıtlıyor;
  snap anahtarı kapalıyken o snap noktası önerilmiyor; sağ tık snap menüsü
  ile çubuk senkron.

UI-1 raporunda mevcut seçim tiplerini, snap modlarını ve filtre
altyapısını da listele (ne var, ne yok). Sıra: UI-1 → … → UI-5 → UI-6;
UI-1 onayında UI-6'yı öne almaya karar verebilirim.
