# Tekla Structures 2026 referans kurali (kalici)

## KURAL

Her CAD ozelligini gelistirmeden ONCE Tekla Structures 2026'nin resmi dokumantasyonunu incele.
Dokumani incelemeden yeni bir CAD aracinin etkilesim akisini rastgele tasarlama. Tekla'nin kapali
kaynak kodunu, lisansli varliklarini veya arayuzunu birebir kopyalama - resmi dokumantasyondaki
KULLANICI DAVRANISLARINI referans alarak Model Maker icin ozgun ve uyumlu cozumler gelistir.

## Ana kaynak

https://support.tekla.com/doc/tekla-structures/2026

## Oncelikli referans sayfalari

1. View olusturma: https://support.tekla.com/doc/tekla-structures/2026/mod_creating_a_view
2. Construction Plane: https://support.tekla.com/doc/tekla-structures/2026/mod_creating_a_construction_plane
3. Fitting / Part End: https://support.tekla.com/doc/tekla-structures/2026/mod_fit_part_ends
4. Custom Components: https://support.tekla.com/doc/tekla-structures/2026/det_what_is_custom_component
5. Assembly Drawings: https://support.tekla.com/doc/tekla-structures/2026/dra_assembly_drawings
6. Drawing Views / Paper Space: https://support.tekla.com/doc/tekla-structures/2026/dra_views

## Her ozellik icin incelenecek 8 soru

1. Tekla'da kullanici islemi nasil baslatiyor?
2. Hangi noktalar, yuzeyler, parcalar veya duzlemler seciliyor?
3. Islem sirasinda hangi onizleme ve snap davranislari var?
4. Olusturulan nesne nasil duzenleniyor veya siliniyor?
5. Hangi parametreler ve ozellik pencereleri mevcut?
6. Islem Undo/Redo ve kaydet/ac sonrasinda nasil davraniyor?
7. Olusturulan nesnenin diger parcalarla iliskisi ne?
8. Bu davranisi mevcut Model Maker C++/OCCT mimarisinde en dogru sekilde nasil uygulariz?

## Semantik farklar (bozma)

- Fitting, Line Cut, Polygon Cut ve Part Cut arasindaki GEOMETRIK ve PARAMETRIK farkliliklari
  koru; hepsini yalnizca basit Boolean Cut olarak ele alma.
- Component gelistirmelerinde: Main Part, Secondary Part, parametre bagimliliklari ve otomatik
  yeniden hesaplama davranisi.
- Drawing calismalarinda: Model View ile Drawing View ayrimini koru; gorunus, kesit,
  olculendirme ve pafta yerlesimini ayri ele al.

## Gorev raporu - "Tekla Reference" basligi (her ozellik)

- Incelenen resmi sayfalar ve URL'leri
- Tekla'daki referans davranis
- Model Maker'da uygulanan davranis
- Bilincli farkliliklar
- Eksik ozellikler
- Dogrulama durumu

Sayfaya erisilemediyse erisilmis gibi raporlama; tahminleri dogrulanmis davranislardan ayir.