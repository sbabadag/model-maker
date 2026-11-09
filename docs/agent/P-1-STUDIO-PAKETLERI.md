# P-1 — Studio paketlerinin Model Maker'da açılması ve kullanılması

Planlayıcı (Claude), 2026-11-10. Model Maker ajanı (Hermes) için bağlam.
INBOX ile çelişirse INBOX geçerlidir.

## Amaç

MM Studio, bağlantıyı `<typeId>.mmcpkg/` klasörü olarak dışa aktarıyor
(manifest, parameters, dialog, recipe, checksums). MM bu paketi **yeniden
derleme olmadan** yüklemeli: recipe çalışma anında contract yorumlayıcısıyla
koşar. Paket katalogda görünür, üyeler seçilerek uygulanır, diyalogdan
parametresi değişir, `.mmw`'ye kaydedilir ve yeniden açıldığında aynı plan
(aynı `planDigest`) üretilir. Studio'da paket güncellenince (Ctrl+S) MM bunu
alır.

## Bugün olan (kodda doğrula, Durum'a "var / yok" listesi yaz)

- MM-5 yükleyici: `loadComponentPackagesFrom` açılışta
  `%APPDATA%\ModelMaker\components` ve proje açılınca `<proje>\components`
  klasörünü tarıyor (`application.cpp`, `component_package.cpp`).
  Fail-closed: bozuk paket atlanıyor ve durum çubuğuna yazılıyor.
- Studio'nun ürettiği örnek paket: Studio deposu `feature/ai-command-mvp`,
  S-5f kabul paketi (`studio_draft.mmcpkg`: 31 parametre, 3 sekme).

## Yapılacaklar (her biri ayrı commit; kapı + GUI senaryosu)

1. **İçe aktarma komutu.** "Komponent paketi içe aktar…" (Komponent
   sekmesi + komut kaydı `component.importPackage` + hızlı arama):
   - Kullanıcı klasörü seçer ya da sürükle-bırakla verir.
   - Paket `loadComponentPackage` ile doğrulanır.
   - Geçerliyse `%APPDATA%\ModelMaker\components\<typeId>.mmcpkg\` altına
     atomik kopyalanır (geçici klasör + yeniden adlandırma).
   - Registry'ye hemen kaydolur; MM'i yeniden başlatmak gerekmez.
   - Seçenek "Projeye göm": paket `<proje>\components\` altına da kopyalanır,
     model başkasına verilince paket de gider.
   - Geçersiz pakette hiçbir şey değişmez; contract'ın Türkçe hatası aynen
     gösterilir.
   - Yerleşik bir tiple (End Plate, Haunch 40, Fin Plate, 146…) aynı
     `typeId` reddedilir.
2. **Geliştirme klasörü ve yeniden yükleme.**
   - "Paketleri yeniden yükle" komutu.
   - Ayarlarda "geliştirme paket klasörleri" listesi: Studio'nun bağlı paket
     klasörü buraya eklenir. `QFileSystemWatcher` ile izlenir; değişiklikten
     ~1 sn sonra yeniden yüklenir.
   - Studio bağlı paketi atomik yazıyor (geçici klasör → yerine koy); yarım
     yazılmış klasörü okuma.
3. **Sürüm ve yeniden üretim.** Aynı `typeId` daha yüksek `schemaVersion`
   ile gelirse:
   - Registry güncellenir.
   - Modeldeki örnekler yeni recipe ile yeniden üretilir.
   - Parametre eşleme ada göredir: aynı ad → değer korunur, yeni parametre →
     varsayılan, kalkan parametre → atılır. Hepsi raporda gösterilir.
   - Yeniden üretim başarısız olan örnek eski hâliyle kalır ve listelenir
     (fail-closed, veri kaybı yok).
   - Daha düşük ya da aynı sürüm farklı içerikle gelirse: uyarı ver, yükleme.
4. **Katalog ve seçim.**
   - Paketler B1'deki arama indeksinde ve komponent listesinde görünür.
     UI-5 gelince panelde otomatik ikonla.
   - `pickCount` 1 (yalnız kolon; taban levhası paketi bunu kullanacak) ve 2
     desteklenir. Seçim istemleri manifestteki
     `primaryMemberType`/`secondaryMemberType`'tan Türkçe.
   - `pickCount` 2'de H-9 temas kontrolü ve H-2 rol çözümü paketler için de
     geçerli.
5. **Diyalog.**
   - `dialog.json` sekmeleri, `autoSchematic`, `preview.dimensions`.
   - String/Enum parametreler (malzeme, kalite) çalışır.
   - `documentedOnly` parametreler salt okunur gösterilir.
   - `check` hataları Türkçe mesajla diyalogda görünür; plan `ok=false` ise
     belge değişmez.
   - Diyalog modsuzdur (K1).
6. **Kayıt.** `.mmw`'de bileşen kaydı şunları taşır: `typeId`,
   `schemaVersion`, parametre değerleri, paket checksum'ı.
   - Açılışta paket yoksa: nesneler son üretilmiş hâliyle kalır, "paket
     eksik: <typeId>" uyarısı verilir, düzenleme kilitlenir.
   - Paket var ama checksum farklıysa: madde 3'teki gibi yeniden üret ve
     raporla.
7. **Studio uyum testleri (golden).**
   - Studio'nun dışa aktardığı paketler MM deposuna test girdisi olarak
     KOPYALANIR: `tests/fixtures/studio_pkgs/`, LF bayt, kaynak Studio
     commit'i bir README'de.
   - İlki S-5'in `studio_draft.mmcpkg`'si; S-6 bitince
     `mm_stiffened_base_plate`.
   - Test: yükle → kanonik örnek model → çalıştır → `planDigest`, Studio'nun
     aynı girdi için raporladığı digest ile aynı. Diyalog şeması geçerli.
     Bir parametre değişince beklenen geometri değişir.
   - Golden'ı test geçsin diye yeniden üretme.
8. **GUI senaryosu (GT-1):** içe aktar → katalogda ara → kolon + kiriş seç
   → diyalog → uygula → parametre değiştir → kaydet → yeniden aç → aynı.
   Ayrıca geliştirme klasöründe paketi değiştir → MM yeniden yükler →
   örnek güncellenir.
9. **Kabul paketi** + `NELER-YENI.txt`: "Studio'da Paketi dışa aktar →
   MM'de Komponent → Paket içe aktar → katalogda ara → uygula → parametre
   değiştir". `ornekler\` altında Studio'nun örnek paketi ve bir `.mmw`.

## Contract istekleri (Studio'dan; contract sahibi sensin)

Maddeler 1–9'dan SONRA; her biri ayrı commit. CONTRACT.md'ye yaz, sürümü
artır (1.12.0 …). Mevcut digest'ler değişmemeli (golden yeniden
üretilmez). STATE'e "Studio REPIN gerekli: <hash>, contract <sürüm>" yaz;
Studio INBOX'ına planlayıcı yazar, Studio deposuna dokunma.

- a. `packages/end_plate_pkg/manifest.json` kanonik anahtar sırasında değil.
- b. `mm::writePackageJsonFile`: dar `std::string` yol (Windows'ta ANSI kod
  sayfası, Türkçe karakterli klasör kırılır) → geniş/UTF-8 yol; sondaki
  `"\n"` denetimi.
- c. `validateRecipeJsonHeader` (recipe JSON yapı doğrulayıcısı) public olsun.
- d. HR-12: `frame.member_end` için `end: lower|upper` (dünya Z'ye göre);
  yukarıdan aşağı çizilmiş kolonda taban levhası doğru uca gitsin.
- e. HR-4b: ankraj bulonu boyu, gömme boyu ve tipi (`bolts.grid` argümanı
  ya da `bolts.anchor`).
- f. HR-13: `member.fit {target, frame}`: profil ucunu düzleme kes ya da
  uzat (Studio'nun fitting aracı için).
