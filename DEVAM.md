# DEVAM — Hermes AWS'ten yerel Windows'a taşıma noktası

Son oturum: 2026-11-06

## Mevcut durum
- Aktif dal: feature/component-contract @ c2862ae (origin-ssh'e push'ta, HEAD == uzak).
- Ağaç temiz; yarım (commit'lenmemiş) iş YOK. Bu dal (wip/hermes-aws-son) yalnız bu DEVAM.md'yi taşır.

## Tamamlanan (hepsi feature/component-contract'ta, push'ta)
- A  — panel rotasyon odak-kaybı veri bozulması (38fa0e0)
- B1 — End Plate katalogda + hızlı arama (d439cc5)
- B2 — belge değişikliği görünümleri tazeler (ae94975)
- B3 — gumball seçim boyutundan bağımsız ekran-sabit (a55de69)
- B4 — aks midpoint kesişimlerde parçalara bölünür (0f63a01)
- B5 — view cube tıklaması seçimi/aktif komutu silmiyor (88099d5)
- C (UI-6a) — üç commit:
  - 116b85c — snap marker/pen/hold/aperture ayarlardan değiştirilebilir + kalıcı
  - 78ba545 — Ctrl+Tab / Ctrl+Shift+Tab görünüm pencereleri arası geçiş (view.next/view.previous)
  - c2862ae — snap tekilleştirme: Qt menüsü = Win32 paneli, tek kaynak 15 tip

## Durduğu yer — UI-3 başlamadı
UI-3 = "özellikler paneli + profilsiz çizgi yok kuralı".
- "profilsiz çizgi yok" kuralı TAM TARİFLİ (MESAJ-16): profilsiz seçenek kalkar;
  Çizgi/Polyline/Dikdörtgen/Daire/3DFACE yalnız yardımcı obje üretir;
  Çizgi+profil=kiriş eşdeğerliği kalkar; eski dosya migrasyonu; Çizim sekmesi adı sorusu.
- "YENİ ÖZELLİKLER PANELİ" SPEKİ YOK. Panelin alanları/düzeni/ne yapacağı hiçbir
  mesajda yazılı değil. A düzeltmesi (38fa0e0) "UI-3'ün (yeni panel) önkoşolu"
  diye geçiyor ama panelin kendisi tanımsız. Kullanıcıya soruldu, cevap bekleniyor.
  SPEK GELMEDEN panel icat edilmemeli.

## Kalan plan (MESAJ-16, sırayla)
1. UI-3 — özellikler paneli (SPEK GEREKLİ) + profilsiz çizgi yok kuralı
2. UI-4 — profil seçim penceresi
3. UI-5 — resimli komponent dock'u + paketler hızlı aramada
   (NOT: yerleşik 4 tip + yüklenen .mmcpkg paketler AYNI indeksten; ayrı iş açma)
4. UI-6b — seçim maskesi + filtre

## Çalışma düzeni (kullanıcının son talimatı)
- C → UI-3 → UI-4 → UI-5 ARA ONAY BEKLEMEDEN art arda; yalnız iki durak: UI-3 sonu, UI-5 sonu.
- Tam ctest her commit'te DEĞİL; commit arası yalnız ilgili testler.
  Tam derleme + tam suite + contract + Win32 kapısı PUSH ÖNCESİ bir kez.
- Push her UI adımının sonunda.
- Veri bozan hata gelirse hemen (istisna); diğer hatalar kullanıcı listede biriktirip duraklarda verir.

## Windows doğrulama
- A, B1–B4, B5, C doğrulaması kullanıcıda SÜRÜYOR; sonuç ayrıca bildirilecek.

## Windows build yolu (hatırlatma)
- C:\work\model-maker-p2d\build-occ, PowerShell.
- /c/mm-build-p2d YOKTUR (eski yanlış yol).
