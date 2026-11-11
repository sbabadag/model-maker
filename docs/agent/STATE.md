# STATE — devir notu

- Aktif makine: ZBook (iş makinesi; model deepseek-v4-flash, deneme). Asus (ev)
  pasif. Senkron yalnız GitHub üzerinden; aynı anda tek makinede çalışılır.
- Son güncelleme: 2026-11-10
- **P-1 turu (2026-11-10, ZBook, gece gözetimsiz)** — kaynak:
  `docs/agent/P-1-STUDIO-PAKETLERI.md`. "Bugün olan" listesi kodda doğrulandı:
  - MM-5 yükleyici (`loadComponentPackagesFrom`, `%APPDATA%\ModelMaker\components`
    + `<proje>\components`, fail-closed): **VAR**.
  - Paket → registry + diyalog kaydı (`loadComponentPackages`): **VAR**;
    tipin paketten geldiği artık `ComponentRegistry::isPackageType()` ile
    işaretli (`registerType(..., fromPackage)`).
  - İçe aktarma komutu (`component.importPackage`): **YOKTU → eklendi** (madde 1).
  - Yeniden yükleme komutu + geliştirme klasörü + `QFileSystemWatcher`: **YOK**
    (madde 2).
  - `schemaVersion` yükseltmesi + modeldeki örneklerin yeniden üretimi: **YOK**
    (madde 3; kurulum katmanındaki sürüm kuralı eklendi, yeniden üretim sırada).
  - Katalog: `pickCount` (1 ve 2) + `primaryMemberType`/`secondaryMemberType`
    Türkçe seçim istemi: **VAR** (paket manifest `catalog` bloğu okunuyor).
  - Diyalog (`dialog.json`, `autoSchematic`, `preview.dimensions`, String/Enum,
    `documentedOnly` salt okunur, `check` hataları TR): **VAR**.
  - `.mmw` bileşen kaydında `typeId` + `schemaVersion` + parametreler: **VAR**;
    paket **checksum**'ı: **YOK** (madde 6).
  - Studio paketleri golden fixture olarak (`tests/fixtures/studio_pkgs/`):
    **YOK** (madde 7).
  - GT-1 paket senaryosu: **YOKTU → `t15_package_import` eklendi** (madde 1).
  - Kabul paketi + `NELER-YENI.txt`: **YOK** (madde 9).
- **P-1/1 (içe aktarma) — BİTTİ (2026-11-10 21:15).**
  - `mm::installComponentPackage(sourceDir, targetRoot, registry, dialogs, opts)`
    (`src/component_package.cpp`): doğrula → geçici klasöre kopyala → KOPYAYI
    yeniden doğrula (checksums bayt üzerinden) → `rename` ile atomik yerine koy.
    Hata halinde hedefteki paket DOKUNULMAZ, `.tmp`/`.old` artığı silinir.
  - Sürüm kuralı: yerleşik typeId → RED; paketten gelen tip + **yüksek**
    `schemaVersion` → KABUL; aynı/düşük sürüm → RED (mevcut kayıt korunur).
  - `ComponentRegistry::registerType(..., fromPackage)` + `isPackageType()`;
    `ComponentDialogRegistry::unregisterDialog()` (yükseltmede eski şema düşer).
  - `Application::importComponentPackage(sourceDir, embed, userRootOverride={})`
    — kullanıcı klasörü + isteğe bağlı `<proje>\components` kopyası; sonuç
    `lastComponentPackageMessage()` (GT/teşhis). `userRootOverride` boş değilse
    oraya kurulur (GT izolasyonu).
  - Komut `component.importPackage` (Komponent sekmesi + menü + hızlı arama) →
    `QtMainWindow::importComponentPackage()` (klasör seçimi + "Projeye göm").
  - `defaultUserComponentPackagesDir()`: `%APPDATA%` yoksa
    `%USERPROFILE%\AppData\Roaming` yedeği; ikisi de yoksa AÇIK mesaj
    ("hedef klasor bos (APPDATA/USERPROFILE yok)") — sessiz yutma yok.
  - Testler: `model_maker_component_package_install_test` (**51/51 kontrol**),
    GT `tests/gt1/t15_package_import` (**18/18 adım**, `plateGap 0.00 mm`,
    `componentDigest p1 = de1c60be6ea84f32`, kaydet→aç aynı digest), yeni runner
    adımları `appImportPackage` (+`userRoot` izolasyonu) ve
    `componentTypeRegistered`; fixture `tests/fixtures/p1_pkg/` (`mm_p1_gt_pkg`).
  - Kapı: tam derleme **exit=0** + tam ctest **161/161 exit=0** (21:13, `-j 4`).
  - **DİKKAT (GT tuzağı):** MSYS `timeout` ile GUI testi koşulursa süreç
    ORTAMSIZ başlıyor (`APPDATA`/`USERPROFILE` yok) → paket yolu boş. GT'yi
    `timeout` olmadan koş. (Aynı tuzağa düşersen belirti: "hedef klasor bos".)
- **P-1/2 (yeniden yükleme + geliştirme klasörü) — BİTTİ (2026-11-10 21:50).**
  - `Application::reloadComponentPackages()`: (1) PAKET kaynaklı tipler kaldırılır
    (`isPackageType`), YERLEŞİK tipler korunur; (2) kökler **GELİŞTİRME →
    kullanıcı → proje** sırasıyla taranır — Studio'nun canlı klasörü aynı
    `typeId`'yi GÖLGELER; (3) etkilenen örnekler yeni recipe ile yeniden
    üretilir, başarısız örnek ESKİ hâliyle kalır (fail-closed).
  - `setComponentDevPackageDirs()` + `setComponentUserPackageRootOverride()`
    (GT izolasyonu: reload, içe aktarmanın kurduğu kökü tarar).
  - UI: komutlar `component.reloadPackages` ("Paketleri Yeniden Yükle") ve
    `component.addDevFolder` ("Geliştirme Paket Klasörü Ekle..."), Komponent
    sekmesi "Katalog" grubu + menü + hızlı arama. İçe aktarma sonrası
    "geliştirme klasörü olarak izle?" sorusu.
  - `QFileSystemWatcher` + 1 sn debounce (`QtMainWindow::setupComponentPackageWatcher`);
    ayar `component/devPackageDirs` (QSettings). Studio atomik yazdığı için
    yarım klasör okunmaz (yükleyici checksums ile reddeder).
  - Testler: `model_maker_component_package_reload_test` (**19/19 kontrol**:
    geliştirme kökü v2 kazandırır, yerleşik tipler korunur, bozuk dev paketi
    atlanır, v1'e dönüş), GT `tests/gt1/t16_package_reload` (**21/21 adım**:
    v1 → v2 (dev) → v1, `componentDigest p2 = de1c60be6ea84f32` üç durumda da
    AYNI); fixture `tests/fixtures/p1_dev/mm_p1_gt_pkg.mmcpkg` (schemaVersion 2).
  - Kapı: tam derleme **exit=0** + tam ctest **162/162 exit=0** (21:47, `-j 4`).
  - **varsayım:** GT'de `QFileSystemWatcher` olayı tetiklenmiyor (dosya sistemi
    olayı zamanlaması deterministik değil); izleyici yolu `appReloadPackages`
    adımıyla AYNI kodu çağırdığı için GT, izleyicinin çağırdığı yolu ölçüyor.
    İzleyicinin kendisi elle GUI turunda doğrulanmalı (aşağıdaki adım).
- **P-1/3 (sürüm yükseltmesi + parametre eşleme) — BİTTİ (2026-11-10 22:15).**
  - `mm::remapParamsToSpecs(specs, values, &report)` + `ComponentParamRemap`
    (`component_engine.hpp/cpp`): eşleme ADA GÖRE — aynı ad → değer korunur
    (aralık/tip dışı `validateParamValue` ile KELEPÇELENİR), yeni ad →
    varsayılan, semadan KALKAN ad → ATILIR. Rapor: kept/clamped/added/dropped
    + `addedNames`/`droppedNames`. (`alignParamsToSpecs` bilinmeyenleri KORUR;
    paket yükseltmesinde bu yanıltıcı olduğu için ayrı fonksiyon.)
  - `reloadComponentPackages()`: eşleme her etkilenen kayda YAZILIR
    (`params` + `schemaVersion`), sonra geometri yeniden üretilir. Durum
    çubuğu: "Paketler yeniden yüklendi: N tip, M örnek (parametre: X korundu,
    Y yeni varsayılan, Z atıldı[, W kelepçelendi])".
  - **DÜZELTİLEN YANLIŞ POZİTİF:** yeniden üretimden önce `updated.dirty=true`
    set ediliyordu; materializer bu bayrağı temizlemediği için BAŞARILI
    yeniden üretim "1 örnek ESKİ hâlinde kaldı" diye raporlanıyordu. Bayrak
    kaldırıldı; başarısızlık artık `atomicRecompute`'nun dönüş değerinden
    (hazırlama/commit başarısız → HİÇBİR örnek değişmez) okunur. Teşhis için
    `solidStatusMessage_` gerekçesi startup loguna yazılır.
  - Testler: `model_maker_component_param_remap_test` (**18/18 kontrol**:
    korunur/eklenir/atılır/kelepçelenir, sıra, boş şema), GT
    `tests/gt1/t17_package_version_upgrade` (**23/23 adım**): v1 üzerinde
    `plateThickness=16` → v3 (schemaVersion 3; `plateFinish` KALKTI,
    `plateCorner` EKLENDİ, `plateExtra` üst sınırı 20→10) → reload →
    26 korundu / 1 yeni / 1 atıldı / 1 kelepçelendi; digest
    `4af221b652d301da` → `daeab7ec32c15c4a` (yeniden üretim GERÇEKTEN koştu),
    kaydet→aç sonrası AYNI digest. Fixture `tests/fixtures/p1_dev3/`.
  - Runner'a `expect.packageMessage {contains, excludes}` eklendi (Türkçe
    rapor metni GT'de denetlenir).
  - **varsayım:** yeniden üretim başarısızlığı GT'de zorlanamadı (geçerli
    paketle üretim hep başarılı); yol `atomicRecompute` sözleşmesiyle
    (all-or-nothing) ve startup log kanıtıyla doğrulandı, ayrı bir "bozuk
    üretim" senaryosu YOK.
- **P-1/4 (katalog ve seçim) — BİTTİ (2026-11-10 22:50).**
  - **BULUNAN KUSUR (gerçek):** `ComponentCatalogMeta::catalogVisible` varsayılanı
    `false` (yerleşik tipler için opt-in). Studio paketleri manifestte `catalog`
    bloğu YAZMIYOR → paket tipi MM kataloğunda ve aramasında HİÇ görünmüyordu;
    ayrıca `pickCount` varsayılanı 2 olduğu için tek üyeli (taban levhası)
    paketler "tam 2 parça seçin" ile reddediliyordu.
  - `component_package.cpp`: (a) `catalogVisible` manifestte AÇIKÇA verilmediyse
    paket tipleri GÖRÜNÜR; (b) `pickCount` verilmediyse **recipe'deki üye
    referanslarından** çıkarılır (`inferPickCountFromRecipeJson`: `secondary_<n>`
    → n+2, yalnız `main` → 1, bilinmiyorsa 2) ve `warnings`'e yazılır;
    (c) üye tipi istemleri boşsa nötr Türkçe metin ("üye" / "ana üye" /
    "ikincil üye"). Manifest AÇIKÇA verirse o değer kazanır (test 3).
  - `application.cpp`: tek üyeli komutta istem "«üye» seçin — sağ tık uygular";
    kullanıcı ikinci üyeyi beklemez.
  - Testler: `model_maker_component_catalog_pick_test` (**20/20 kontrol**:
    Studio paketi görünür + pickCount 1, iki üyeli paket pickCount 2, açık
    manifest kazanır, gizli paket listede yok, katalogda Türkçe ad),
    GT `tests/gt1/t18_base_plate_pick1` (**18/18 adım**: catalogEntry pick=1
    visible=1, TEK kolonla uygulama, `plateGap 0.00 mm`, kaydet→aç aynı
    `d170874d915c71ec`). Fixture `tests/fixtures/p1_pick1/`.
  - Studio paketleri fixture olarak: `tests/fixtures/studio_pkgs/`
    (`mm_stiffened_base_plate.mmcpkg`, kaynak Studio commit `9b50ac5`) +
    `README.md` (P-1/7'nin ilk girdisi).
  - **P-2 İÇİN BULGU (engel):** Studio taban levhası paketi uygulanınca
    `[component] Rijitlestirilmis taban levhasi hazirlanamadi: levha kati
    uretilemedi: stiffener_alt_w` — `plate.polygon` bloğu materyalizatörde
    katıya çevrilemiyor (P-2 tablosundaki "plate.polygon → dışbükey kontur"
    satırı). P-1/4 kapsamı bunu ÇÖZMEZ; GT `t18` bu yüzden kendi tek üyeli
    `plate.rect` fixture'ıyla koşuyor.
- **P-1/5 (diyalog) — BİTTİ (2026-11-10 22:45).**
  - Mevcut altyapı DOĞRULANDI (yeni kod gerekmedi): `dialog.json` → `useTabs`/
    `asTab`/`groups.paramNames` + `preview.dimensions` paket yükleyicisinde
    semaya taşınıyor; `documentedOnly` `parameters.json`dan okunup
    `CompParamSpec.documentedOnly`'ye yazılıyor; String/Enum (malzeme, kalite)
    `PlateSpec.material`/`name`e gidiyor.
  - **BULUNAN İKİ GERÇEK KUSUR:**
    1. `setSelectedComponentParams` (diyalog OK / Properties yolu) üretim
       reddinde **SESSİZCE** `false` dönüyordu — `gp.error`/`plan.error`/
       `prep.error` hiçbir yere yazılmıyordu (kullanıcı "Uygula" der, hiçbir
       şey olmaz, sebebi görünmez). Artık üç dalda da durum çubuğuna
       "«tip» — UYGULANAMADI: <Türkçe sebep>" yazılıyor.
    2. Recipe `check` ihlali `plan.checks`'e **HİÇ** yazılmıyordu (adım erken
       `return` ediyordu) → `applyComponentFromParts`'ın "parametreyle
       düzeltilebilir mi?" kararı (`hasFailingCheck`) paket doğrulamaları için
       HEP false kalıyor, red diyaloğu açılmıyordu. Artık `pass=false` +
       Türkçe `message` ile `checks`'e ekleniyor.
  - Testler: `model_maker_component_package_dialog_test` (**28/28**:
    useTabs + 2 sekme adı/sırası, String malzeme → levha material, Enum kalite
    → levha adı, `documentedOnly` işaretli + geometriyi DEĞİŞTİRMİYOR,
    `check` ihlali ok=false + Türkçe mesaj + `checks`'te raporlanıyor),
    fixture `tests/fixtures/p1_dialog` (2 sekme, 5 param, `bp_t>=12` check),
    GT `tests/gt1/t19_package_dialog` (**27/27**: `componentDialogSchema`
    adımı 2 grup/2 sekme/5 param, reddedilen düzenleme `bp_t=10` → durum
    çubuğunda "en az 12 mm" + **digest DEĞİŞMEDİ** `d3210a7a29c550ec`, geçerli
    düzenleme `bp_t=24/S420M/10.9` → `a12fc1e01f308fec`, kaydet→aç aynı).
  - Yeni runner adımları: `componentDialogSchema {typeId, groups, tabs,
    useTabs, paramCount, titles}`; `appComponentParams` artık `expectFail`.
  - Regresyon: t18 18/18, t17, t16, t15, t14, t13 hepsi exit=0.
- **P-1/6 (kayıt) — BİTTİ (2026-11-10 23:20).**
  - `.mmw` bileşen kaydı artık **paket içerik özetini** taşıyor:
    `ComponentType.packageChecksum` (checksums.json dosya özetlerinin kanonik
    sha256'sı) + `ComponentInstanceRecord.packageChecksum`. MMW8 'I' bloğunda
    satır SONUNA eklenir; **eski dosyalarda yoktur → boş okunur** (geriye
    uyumlu, digest'ler değişmedi).
  - Açılışta üç durum:
    1. özet AYNI → hiçbir şey yapılmaz;
    2. özet FARKLI (Studio Ctrl+S / paket yeniden içe aktarıldı) → parametre
       ada göre yeniden eşlenir + örnek yeniden üretilir, kalıcı loga
       `[paket-checksum] Paket güncellendi: N örnek yeniden üretildi (tip)`;
    3. paket YOK (tip kayıtlı değil) → kayıt `orphan`, geometri **dosyadaki
       hâliyle** korunur, `[paket-eksik] Uyarı: ...` yazılır ve **düzenleme
       kilitlenir** (`Paket eksik: <typeId> — düzenleme kilitli (önce paketi
       içe aktarın)`).
  - Özet, uygulama/parametre düzenleme/reload yollarında damgalanır (aksi
    halde her açılışta gereksiz yeniden üretim olurdu).
  - Testler: `component_serialize_test` +1 ("P-1/6 paket içerik özeti .mmw'de
    korunur"; 52/52, FAILURES: 0), GT `tests/gt1/t20_package_record_checksum`
    (**31/31**: v1 uygula+kaydet → v2 içe aktar → aç: "Paket güncellendi" ve
    digest `d3210a7a29c550ec` → `b7e01819b7d8c7ec`; paketi kaldır → aç:
    "kayıtlı olmayan component türü", digest **dosyadaki hâline** döner
    (`d3210a…`), düzenleme reddedilir; paketi geri kur → aç: v2 digest aynı).
  - Yeni runner adımları: `appRemovePackage {typeId, userRoot, deleteDir}`,
    `expect.startupLog {contains}` (kalıcı teşhis logu; durum çubuğu sonradan
    ezilebildiği için uzun açılış raporlarının tek güvenilir kanıtı).
  - Fixture: `tests/fixtures/p1_dialog_v2` (schemaVersion 2, levha +10 mm).
  - Regresyon: t19 27/27, t18 18/18, t17 23/23, t16 21/21, t15 18/18,
    t14 24/24, t13 11/11 hepsi OK.
- **P-1/7 (Studio golden) — BİTTİ (2026-11-11 00:35).**
  - Studio'nun **gerçek** `mm_stiffened_end_plate.mmcpkg` paketi repoya alındı
    (`tests/fixtures/studio_pkgs/`, Studio commit `9b50ac5`,
    `feature/ai-command-mvp`; `checksums.json` bayt bayt doğrulandı).
  - Yeni birim testi **`model_maker_studio_package_golden_test` (21/21)**:
    (a) iki Studio paketi de yükleniyor, 8 parametre, katalog metası +
    diyalog şeması geçerli; (b) alın levhası için **kanonik girdiyle
    `planDigest` golden'ı** — `tests/golden/studio_end_plate.json`,
    digest `178cb73b928dadd34c9b6cfbc875b0912031f2872c651ea79bd99583780d4818`,
    3 levha + 1 bulon grubu; (c) aynı girdi iki kez üretilince **aynı** digest
    (determinizm); (d) parametre değişince digest **değişir**.
  - Kanonik girdi: HEB300 kolon `(0,0,0)-(0,0,3000)` + **IPE240** kiriş
    `(0,120,2880)-(6000,120,2880)`. IPE300 denendi → paketin KENDİ check'i
    haklı olarak reddetti ("sureklilik levhasi kolon flansinin icine giriyor");
    kiriş kesiti kolondan küçük olmak zorunda.
  - GT `tests/gt1/t21_studio_end_plate.json` (**25/25**): paket içe aktar →
    katalogda görünür (pickCount 2) → HEB300+IPE240 üzerine uygula → bilesen
    birlesime oturur (`componentAttached`, maxGap 500) → kolon boyu 3000→3300
    → digest `1396f4d5…` → `31cdc31a…` → kaydet/aç: digest **aynı**.
  - Yeni GT adımı gerekmedi; `appImportPackage`/`catalogEntry`/`componentDigest`
    yeterli oldu. `componentPlateGap` bu senaryodan ÇIKARILDI: runner tüm
    levhaları hedef üyeye göre ölçüyor, süreklilik levhaları ise **kolona**
    kaynatılır — beklenti yanlıştı (aşağıdaki bulgu (a) ile karışmasın).
  - **P-1/7 BULGUSU (a) — düzeltilmedi, ayrı iş kalemi:** Studio paketinin
    süreklilik levhalarında `du`/`dv` **negatif** varyantlar simetrik konuma
    oturmuyor. Ölçüm (HEB300 kolon, GT `t21` çıktısı): `stiffener_top_a`
    y=−144.5…0 (doğru), `stiffener_top_b` y=−300…−155.5 (olması gereken
    y=0…144.5; **~155 mm kayık**), `stiffener_bot_b` aynı şekilde kayık.
    Şüphe: `frame.offset` + `frame.rotate` sonrası `plate.rect` köşe/merkez
    (sol-elli çerçeve) uzlaşımı. `plan` üretimi ve digest **kararlı** olduğu
    için golden etkilenmiyor; geometri kusuru.
  - **P-1/7 BULGUSU (b) — düzeltilmedi, ayrı iş kalemi:** kiriş profili
    IPE240→IPE270 değişince paket komponenti **yeniden üretilmedi** (digest
    sabit kaldı), oysa yerleşik alın levhasında (t9) aynı adım digest'i
    değiştiriyordu. Kolon boyu değişimi çalışıyor. Şüphe: paket tipinde
    `secondary` üye geometrisinin bağımlılık grafiğine yazılmaması.
    Doğrulama senaryosu t21'den çıkarıldı (yeşil kalsın diye değil — yanlış
    pozitif üretmesin diye); kalem açılacak.
  - `mm_stiffened_base_plate` için golden **yok**: `plate.polygon` materyalizasyon
    engeli (P-2). Test bunu "yükleme+şema geçerli, üretim beklenen hata"
    olarak doğruluyor ve P-2 bitince golden ekleneceğini hatırlatıyor.
  - Kapı: tam derleme **exit=0** + tam ctest **166/166 exit=0** (23:41, 308 s).
- **CONTRACT İSTEKLERİ (Studio, a–k) — BİTTİ (2026-11-11 10:30).** Her istek
  AYRI sürüm + AYRI commit; `libs/mm_contract/CONTRACT.md` "Contract istekleri
  (Studio) — sürüm günlüğü" bölümünde tek tek yazılı.
  **MEVCUT DİGEST'LER DEĞİŞMEDİ**: yeni alanlar kanonik JSON'a YALNIZ varsayılan
  dışı değerde yazılır (ankraj/kaynak), yeni bloklar eski recipe'lerde yok.

  | istek | sürüm | commit | ne geldi |
  |---|---|---|---|
  | (a) manifest kanonik sıra | 1.12.0 | `098700e` | `end_plate_pkg/manifest.json` alfabetik |
  | (b) `writePackageJsonFile` | 1.13.0 | `d61349d` | geniş/UTF-8 yol, CR reddi, sonda TEK LF |
  | (c) `validateRecipeJsonHeader` | 1.14.0 | `494c973` | PUBLIC API (`Impl` + sarmalayıcı) |
  | (g) yinelenen `roleKey` | 1.15.0 | `a48aa97` | dışlayıcı `when`; çalışma anında çift üretim RED |
  | (d) `frame.member_end` | 1.16.0 | `b2b9373` | `end: lower|upper` (dünya Z) |
  | (i) `frame.member_face` | 1.17.0 | `b8aef47` | `face` + `at`, `toward` gerekmez |
  | (h) `frame.bisector` | 1.18.0 | `c4f4314` | açıortay düzlemi çerçevesi |
  | (e) ankraj bulonu | 1.19.0 | `d683205` | `anchorType/Length/Embed` |
  | (j) `bolts.grid xs/ys` | 1.20.0 | `79b6519` | Tekla `70 2*80 70` |
  | (k) kaynak tipi | 1.21.0 | `b84590f` | `type/length/offset/around/site` |
  | (f) `member.fit` | 1.22.0 | `408956b` | ucu düzleme KESER; UZATMA YOK |

  (a)/(b)/(c) `git reset --soft 61b9d91` sonrası yeniden commit'lendi; SHA'lar
  `git log --oneline c2f54af..HEAD` ile doğrulandı.
  - Test: **`model_maker_contract_requests_test`** (a,b,c,g,d,i,h,e,j,k,f
    bölümleri) — hepsi yeşil; `kContractVersion` **1.22.0**.
  - **(f) sınırı:** `GenerationPlan`'da üye UZATMA modeli yok → `mode:
    extend` "uzatma henüz desteklenmiyor (yalnız kesme)" ile AÇIK reddedilir
    (sessiz yutma yok). Studio fitting aracı şimdilik yalnız kesme yapabilir.
  - **(a) regresyonu:** manifest yeniden yazılırken `end_plate_pkg/checksums.json`
    güncellenmemişti → paket fail-closed reddediliyordu (3 test kırmızı:
    `component_plate_color_test`, `component_command_path_test`,
    `component_dialog_widget_test`). Özet yeniden üretildi (`bc18291`).
    **Ders: paket dosyasına dokunan her değişiklikte checksums.json yenilenir.**
  - **Studio REPIN gerekli: contract 1.22.0.** Studio paketlerini bu sürümle
    yeniden üretmeli; `planDigest` raporlamaya başlayınca golden karşılaştırması
    eklenecek. Studio deposuna dokunulmadı (INBOX'a planlayıcı yazar).
- **P-1/8 (tek akışlı uçtan uca GT) — BİTTİ (2026-11-11 10:35).**
  - `tests/gt1/t22_p1_full_flow.json` (**35/35 adım, ok=true**): Studio
    `mm_stiffened_end_plate` içe aktar → katalog (pickCount 2, visible,
    hasDialog) → diyalog şeması (4 sekme, 8 parametre) → HEB300 + IPE240
    (z=2880, **y=0**; y=120 verilince H-9 temas denetimi reddediyor) →
    uygula (`componentAttached` maxGap 500) → digest `ilk` → `st_t=16`
    (`duzenlendi` ≠ `ilk`) → kaydet → aç (aynı) → **dev klasörü v2** →
    `appReloadPackages` → örnek YENİDEN ÜRETİLİR (`v2` ≠ `duzenlendi`) →
    kaydet/aç (aynı).
  - Fixture: `tests/fixtures/studio_pkgs/dev/mm_stiffened_end_plate.mmcpkg`
    — golden paketin **türetilmiş** v2'si (schemaVersion 2, dört levha
    kalınlığı `param.st_t + 10`, checksums yeniden üretildi). Golden DEĞİL;
    README'de yazılı. Golden digest yalnız kök klasördeki Studio paketiyle.
- **P-1/9 (P-1 bütünü kabul paketi) — BİTTİ (2026-11-11 10:40).**
  - `C:\work\mm-kabul61111-1038-p1-butunu\`: `ekran\` 5 GT ekranı,
    `ornekler\` Studio paketi + iki `.mmw` + `golden\studio_end_plate.json`,
    `result.json`/`runner.log`/`t22_p1_full_flow.json`, `NELER-YENI.txt`
    (deneme adımları + P-1/1…/9 özeti + contract 1.12.0…1.22.0 tablosu +
    Studio REPIN notu).
- **P-1 KAPISI (2026-11-11 10:45):** tam derleme **exit=0** (652 hedef) + tam
  ctest **167/167 exit=0** (394.7 s; `model_maker_contract_requests_test`
  dahil, GT script'leri ctest'e dahil DEĞİL — ayrı koşulur); golden
  `178cb73b…d4818` **DEĞİŞMEDİ**; GT `t21` 25/25, GT `t22` 35/35.
- **BULGU (b) — KAPANDI (2026-11-11 11:05, `830c735`).** Kök neden bağımlılık
  grafiği DEĞİLDİ: yeniden üretim zaten koşuyordu; paketin kendi check'i
  ("Süreklilik levhası kolon flanşının içine giriyor…") reddediyor ve
  `atomicRecompute` TÜM işlemi (profil ataması dahil) geri alıyordu. Artık
  gerçek neden geri alma mesajında (`prepWhy`) + kalıcı `CR-PREP-FAIL` logu;
  GT `t23_package_profile_change` iki dalı kilitler: geçerli IPE240→IPE220
  "yenilendi" + digest değişir, geçersiz IPE220→IPE270 RED + rollback
  (digest sabit).
- **BULGU (a) — TEŞHİS TAMAM, DÜZELTME İÇİN KARAR GEREKLİ (2026-11-11 11:20,
  `e115737`).** `docs/agent/BULGU-A.md`. Ölçüm (`tests/gt1/t24_bulgu_a_olcum.json`,
  HEB300 + IPE240): `stiffener_top_a` x 13.5…275.5 / y −144.5…0,
  `stiffener_top_b` aynı x / y −300…−155.5 — ikisi de aynı tarafta ve
  262 mm'lik boyut GENİŞLİK ekseninde (kolon kesiti x ±150 → levha dışına
  taşıyor). Kanonik (golden) girdide plan doğru ve simetrik. Kök neden:
  `frame.member_face` gövde yüzü dalında `u = sf.height` veriyor; recipe ise
  `u = genişlik` varsayımıyla `du` uygulayıp 90° döndürüyor → levha 90° dönük.
  Düzeltme iki yönlü olabilir (MM `frame.offset` semantiği ya da paketin
  gövde yüzü bağlantısını reddetmesi); **Studio'nun raporladığı plan hâlâ
  alınamadığı için** (`planDigest` yok) hangisinin doğru olduğu ölçülemiyor.
- **BULGU (a) — KARAR UYGULANDI: fail-closed (2026-11-11 17:20, kontrat 1.24.0).**
  Planlayıcı kararı (INBOX 17:15): `frame.member_face` **semantiği DEĞİŞMEZ**
  (u/v/w aynı → mevcut digest'ler korunur); paket bağlandığı yüzü bildirir.
  - **Kontrat eklendi:** `frame.member_face {require: flange|web, message}`.
    Yüz türü uyuşmazsa plan REDDEDİLİR; `message` verilmişse birebir o Türkçe
    metin döner. `require`/`message` plan çıktısına yazılmaz → digest etkisi YOK.
  - **CONTRACT.md:** yüz başına eksen takımı tablosu (flanş: `w`=±yükseklik,
    `u`=genişlik; gövde: `w`=±genişlik, `u`=yükseklik) + 1.24.0 girişi ve
    gerekçe (Bulgu a ölçümü).
  - **Test:** `model_maker_contract_requests_test` bölüm `re` (6 kontrol):
    flanş+require flange → plan; gövde+require flange → RED + paket Türkçe
    mesajı birebir; gövde+require web → plan; mesajsız → varsayılan teshis
    metni; `require` yok → eski davranış; geçersiz `require` → RED.
  - **Paket tarafı Studio'da:** `mm_stiffened_end_plate` recipe'sine
    `"require": "flange", "message": "bu paket yalniz kolon flansina baglanir"`
    eklenmeli (REPIN-2). O gelince `t24` beklentisi "kurulmadı + mesaj" olacak;
    bugünkü fixture eski davranışı (kurulur) koruyor.
  - `mm-kabul` temizliği kurala göre yapıldı: son 3 paket
    (`20261111-1205-bulgu-b-p2`, `20261111-1038-p1-butunu`,
    `20261111-0045-p1-7-studio-golden`) + `son\` en yeni paketin kopyası.
- **P-2 (`plate.polygon` materyalizasyonu) — BİTTİ (2026-11-11 11:35).**
  - Kök neden: `blockPlatePolygon` konturu `outline`'a yazıyor ama
    `PlateSpec.width/height` **0** bırakıyordu; `ComponentMaterializer::
    buildPlateSolid` ilk kapıda (`width/height <= 0`) boş döndürüyor →
    "levha katı üretilemedi" ve paket uygulanamıyordu.
  - Düzeltme (`src/recipe.cpp`, `blockPlatePolygon`): konturun **sınır
    kutusu** `width/height`'e yazılır (PlateSpec sözleşmesi: "outline dolu ise
    width/height yalnız sınır kutusudur"); sıfır alanlı kontur artık açık RED.
    Haunch gövdesi aynı kuralı zaten kullanıyordu.
  - Kanıt: `tests/gt1/t25_p2_polygon_materyalizasyon.json` —
    `mm_stiffened_base_plate` (6 kontur levhası) içe aktar + uygula:
    **düzeltme öncesi `componentCount=0`** (adım FAIL), düzeltme sonrası
- **P-2 VAR/YOK listesi yazıldı:** `docs/agent/P-2-VAR-YOK.md` — her recipe
  çıktısı için VAR / KISMI / YOK + kanıt (dosya:satır, test, GT). Eksikler
  sırayla: ankraj bulonu deliği (P-2/2), `cut.plane` görünürlüğü (P-2/3, B8 ile
  aynı kök), içbükey `plate.polygon` (P-2/4), BOM/malzeme listesi (P-2/5),
  kopyala/taşı bütünlüğü (P-2/6), GT-1 GUI senaryosu (P-2/7). BOM özelliği
  MM'de YOK; malzeme bilgisi parça özelliklerinde + `Pos_No:` açıklamasında.
  - **Golden (2026-11-11 17:55):** `tests/golden/studio_base_plate.json`
    (`planDigest 92134f6a…c85e`) — kanonik HEB300 kolon (yalnız kolon, pickCount 1),
    varsayılan parametreler: 3 levha (500×500×30 taban + 2 kontur), 1 bulon
    grubu, 5 kaynak. `studio_package_golden_test` bölüm 2 artık "P-2 engeli"
    beklentisini DEĞİL, gerçek golden'ı denetler: 26 PASS / 0 FAIL, digest iki
    koşumda birebir aynı. Kontur levhalarında `width/height > 0` ve
    `outline` dolu olması ayrıca kontrol edilir (P-2 regresyon kilidi).
    komponent yerleşir ve 17 model oluşur (taban levhası + 8 ankraj +
    6 kontur levhası + kaynaklar). `recipe_test` (HR-3) artık
    `width/height == sınır kutusu` kontrolünü de yapar.
  - `t24` ve `t25` CMake gt1 listesine eklendi (`MM_GUI_TESTS=ON` ile
    `ctest -L gui` altında koşar; varsayılan ctest kapsamı 167'de kalır).
  - **Kontrat 1.23.0.** (a) P-2 için `plate.polygon` sınır kutusu, (b) sürüm
    alanı tutarlılığı: `kContractVersionMinor` **11'de kalmıştı** (string
    1.22.0). `contractSupports()` sayısal alanları, paket doğrulaması string'i
    kullandığı için `minContractVersion: "1.22.0"` bildiren Studio paketleri
    sessizce reddedilebiliyordu — artık 1/23/0 ve `contract_requests_test`
    bölüm `p2` ikisini de kilitler. CONTRACT.md'de 1.23.0 girişi var.
  - Not: `libs/mm_contract/tests/recipe_test.cpp` bu build'de DERLENMİYOR
    (kök CMakeLists o dosyayı hedefe eklemiyor; `libs/mm_contract/CMakeLists.txt`
    add_subdirectory edilmiyor). Oraya test eklerken canlı hedefe
    (`model_maker_contract_requests_test`) de eklenmeli.
- Son güncelleme (önceki): 2026-11-09

- Son tur: 2026-11-09 (Asus) — H-2 (ters seçim: End Plate/Fin Plate/Haunch rol
  çözümü) — BİTTİ — Kök neden: plan rol anahtarları `inputRoleFor` ile konumsal
  atanıyordu; ters seçimde digest değişiyor + End Plate kiriş ucu kesimi kolonu
  kesiyordu. Düzeltme: generator'larda rol anahtarları geometriden SABİT
  ("main"=kolon, "secondary_0"=kiriş); yeni `componentRolesFromGeometry`
  (dikeylik swap + belirsizde fail-closed red) `applyComponentFromParts`'te
  cap-gap/kayıttan ÖNCE çalışıyor. Test: `component_role_resolution_test`
  (kırmızı→yeşil) + tam ctest 160/160. GUI doğrulaması bekliyor.
  Sıradaki: B8 → B5 → T-planı.
- Son tur: 2026-11-09 (ZBook) — H-9 (temas etmeyen üyede bağlantı) — BİTTİ —
  Kullanıcı: "yine haunch sorunu var" (`CUMALI KOC.mmw`). Ölçüm (yeni
  `componentPlateGap` / `modelBox` beklentileri): mertek Y=0'da, kolonlar
  Y=-250'de → alın levhası kolondan **155 mm havada**; haunch matematiği doğru,
  kusur üyelerin 3B'de ayrık olması (mertek ön görünüşte, derinlik bilgisi
  olmadan çizilmiş). (1) engine: `componentContactCheck` (fail-closed) — kiriş
  uç kesiti kolonun kesit+eksen kapsamıyla üç eksende örtüşmüyorsa bağlantı
  KURULMAZ, mesaj "kiriş kolona değmiyor: N mm boşluk (eksen)". Yalnız YENİ
  kurulum (`Application::applyComponentFromParts`); dosyadan gelen kayıtların
  regeneration'ı etkilenmez. (2) Testler: birim testi
  `model_maker_component_contact_test` (değen / kısmi örtüşen / ayrık / ters
  seçim / eksik girdi) + GT-1 `t13_haunch_offset_failclosed` (red + "175.0 mm");
  t9/t11'e `componentPlateGap` kalkanı (reanchor sonrası dahil). (3) Doğrulama:
  t9/t11/t13 yeşil, tam ctest **158/158**, ZBook kurulum derlemesi 665/665.
  Kullanıcı modeli için tamir: mertekleri seç → Taşı (M) → ΔY = -250 → haunch'ı
  yeniden kur (kod artık hizasız kurulumu reddediyor).
- Ek (2026-11-09, ZBook): **cap levhası–kolon üstü boşluğu parametresi** —
  kullanıcı: "haunch'ta cap plakası ile kolon üstü için bir parametre olmalı".
  Yeni `topPlateGap` (varsayılan **0** = eski davranış). Diyalog "Kolon Cap"
  sekmesinde `t cap` + `gap cap`; 3B önizlemede `gap cap` alanı. Diğer levhalar
  (alın levhası) kaymaz. (Aşağıdaki H-10 bu parametrenin nihai davranışını
  belirler.)
- H-10 (aynı gün, devam): **cap boşluğu KOLON BOYUNU değiştirir** — kullanıcı:
  "kolon cap yeri değişince kolon üstüne göre uzayıp kısalmalı". Karar (kullanıcı
  onayı): levha havada kalmaz; kolonun üst ucu gap kadar kayar, cap levhası yeni
  tepeye oturur; parametre 0'a dönünce kolon eski boyuna döner.
  - Motor (`generateHaunch40`) artık gap'i levha konumunda KULLANMAZ (levha =
    kolon üstü + t/2). Aralık ±500 mm (negatif = kolon kısalır).
  - `Application::shiftColumnTopNoUndo` (yeni): dikey elemanın ust ucunu kaydırır,
    katı profil/rotasyon/stille yeniden üretir, kesme zincirini tazeler; undo
    kaydı AÇMAZ.
  - `applyComponentFromParts` + `setSelectedComponentParams`: undo kaydı kolon
    kaydırmadan ÖNCE açılır, `commit/recompute(openOwnRecord=false)` aynı kayda
    yazar → tek geri-alma adımı; başarısızlıkta rollback. Düzenlemede **delta**
    (yeni−eski) uygulanır: birikme yok.
  - Önizleme: `applyCapPlateGapToColumn` (motor yardımcısı) — canlı 3B önizleme
    ve 2B şema da kolonun uzadığını gösterir (taban geometri her recompute'ta
    yeniden kurulur).
  - Test: `haunch_cap_gap_test` 33 kontrol (motor gap'i yok sayar + yardımcı
    ±gap + no-op), yeni GT senaryosu `tests/gt1/t14_haunch_cap_gap_column.json`
    (7000→7025→7050→7000→6970→7000, alın levhası teması korunur) ve yeni runner
    adımı `appComponentParams`.
- Önceki tur: 2026-11-09 — B-picker regresyonu + H-5 — BİTTİ —
  (1) Kullanıcı: "yeni profil ve props düzeltmesinden sonra kiriş komutu profil
  çizmiyor" — picker UYGULA/TAMAM yolu panele yazıyor ama pendingProfileName_'i
  güncellemiyordu; Kiriş kapısı reddediyordu. profileApplied bağlantısı artık pending'i
  günceller; komut kapısı KİRLİ panel değerini benimser (katalog dışıysa engeller), kirli
  değilse bekleyen profil geçerli (bos panel ezmez — B6/H-4). Kalkan: t12_beam_picker_regression
  (21 adım). (2) H-5: yüzeye göre görünüş yatay yüzlerde "Plan Görünüşü" sanılıyordu —
  ViewDefinition.fromFace bayrağı eklendi; başlık "yüzey görünüşü, derinlik …" olur.
  h1 senaryosu ayrımlı: kiriş Z=3000, üst yüz (viewTitle+viewOriginZ=3000) + yan yüz
  (viewNormalZ). Runner: faceClick/clickWorld/fit + viewTitle/viewOriginZ/viewNormalZ.
  Doğrulama: 13/13 GT-1, ctest 157/157, python props OK. Commit'ler fa8e853 (B-picker)
  + 9261441 (H-5). Sıradaki: H-2 → B8 → B5 → T-planı.
- Daha önceki tur: 2026-11-09 — H-8 (Haunch kolon/mertek ayrıklığı) — BİTTİ — panel kot
  uygulaması artık bağlı üye uçlarını yeni kota sabitliyor (reanchorSecondaryMembers,
  diğer ucu sabit — Tekla davranışı) + End Plate/Haunch aynı undo kaydında yeniden
  üretiliyor; başarısızlıkta tam rollback. Aynı turda assignProfileToSelection da
  uç bileşenlerini yeniden üretmeye başladı (profil değişimi de aynı sınıf açıktı).
  GT-1 kalkanı: t9_haunch40_e2e (34 adım) + t9_endplate_e2e (35 adım — profil
  değişim kalkanı eklendi), ikisi yeşil; ctest 155/155; python props testi OK.
  H-7 INBOX'ta BİTTİ (15a41fb). Sıradaki: H-6 →
  T-planı (INBOX Sıra).

## Git düzeni

- Bu klon: `C:\work\mm-hermes` (git worktree; yerel dal `hermes/work`, uzak:
  `origin/feature/component-contract`).
- Pull: `git pull --ff-only origin feature/component-contract`
- Push: `git push origin hermes/work:feature/component-contract`
- Yarım kalmış iş YALNIZ `wip/hermes` dalına commit edilir; `hermes/work`'e asla.
- Detay: `docs/agent/skills/model-maker/model-maker-hermes-git/`

## Derleme (Windows — Asus/ZBook)

- Derleme dizini: `C:\work\mm-hermes\build-hermes` (Release, Ninja, UCRT64 g++).
- Kabuk: Git Bash; her derleme/ctest komutundan önce:

      export PATH=/c/msys64/ucrt64/bin:$PATH
      export QT_PLUGIN_PATH=/c/msys64/ucrt64/share/qt6/plugins
      export TMP='C:\work\mm-tmp' TEMP='C:\work\mm-tmp' TMPDIR='C:\work\mm-tmp'
      mkdir -p /c/work/mm-tmp

- **ZBook (zorunlu):** üç `TMP/TEMP/TMPDIR` değişkeni olmadan hiçbir hedef
  derlenmez (g++ `Cannot create temporary file in C:\Windows\`) — ayrıntı
  `docs/agent/known-failures.md` §4. Asus'ta görülmedi.

- İlk configure:

      cmake -S . -B build-hermes -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe \
        -DQT_ROOT=C:/msys64/ucrt64 -DOCC_ROOT=C:/msys64/ucrt64 \
        -DMM_OCC_LINK_DIRECT=ON

- Paralellik: `-j 4` (bellek daralırsa `-j 2`). Aynı anda iki büyük derleme
  başlatma (Claude Code da aynı makinede derleyebilir).
- Kullanıcı kabul klasörü: `C:\work\model-maker-p2d` (kabul derlemesi
  `build-occ`, PowerShell ile) — DOKUNMA.

## Oturum kuralı

- Oturum başı: pull + STATE.md oku + `docs/agent/skills/`'i yerel skill
  klasörüne kopyala.
- Oturum sonu ("devir" ya da iş bitince): STATE.md güncelle + commit + push.

## Push kapısı

- Windows: tam derleme (model-maker.exe dahil, link dahil) + tam ctest.
- Linux: GitHub Actions CI (ubuntu, Qt6 + OCCT, tam ctest, offscreen).

## Bilinen dış hatalar (Windows)

- Testler gevşetilmez; ctest etiketi/WILL_FAIL kullanılmaz.
- Ayrı liste: `docs/agent/known-failures.md` — #78 (occ_brep_serialize),
  #96 (component_plate_color), #60 (entity_id) → üçü ÇÖZÜLDÜ.
- Çözüm (2026-11-07): pacman-yönetimli paket sabitleme —
  `docs/agent/msys2-paket-sabitleme.md` (opencascade 7.9.2-2 + ffmpeg
  8.0-5 zinciri + IgnorePkg). ZBook kurulumunda aynısı uygulanacak.

## Durum (2026-11-07)

- [x] İçe aktarma (skill + hafıza, Linux yolları Windows'a çevrildi)
- [x] `hermes skills trust C:/work/mm-hermes`
- [x] docs/agent/ kurulumu: AGENTS.md, STATE.md, ROADMAP-UI.md, skills/
- [x] Windows temiz derleme (build-hermes) + tam ctest: 153/153
- [x] Bağlam optimizasyonu: context_length 1048576 (deepseek-v4-pro 1M),
      boş oturum tabanı 16.012 → 11.989 token; gereksiz araçlar kapalı,
      83+20 skill arşivli (INBOX 0a/0b/0c)
- [x] Gece modu cron'u: `gece-mm` (40dk, kilit dosyalı, 09:00 self-delete)
      → 2026-11-08 SÜREKLİ MOD'a geçildi: `gece-mm` silindi; `surekli-mm`
      cron kuruldu (30dk, süresiz, catch-up kapalı, kilit
      `C:\work\mm-hermes-surekli.lock`, workdir `C:\work\mm-hermes`)
- [x] H-4 (tutulan snap mesafe kuralı): tutma süreye değil yalnız
      histerezise bağlandı (tutma süresi yalnız görsel marker); GT-1
      h4_held_snap_release_distance kırmızı→yeşil; ctest -L gui 6/6 +
      tam ctest 160/160; kabul paketi yayınlandı (cb9506f)
- [x] H-6 (soluk snap izi): holdTimeMs yeniden çalışıyor — histerezis
      dışında snap hemen geçersiz ama marker holdTimeMs boyunca soluk
      çiziliyor (yalnız görsel; tıklama/hover değişmez); GT-1 h6 senaryosu
      kırmızı→yeşil + captureLive görsel kanıt; ctest -L gui 7/7 + tam
      ctest 161/161; kabul paketi yayınlandı (133fe71)
- [x] H-7 (3B GL Solid çizgi görünmezliği): kök neden GL Solid'de
      lineBatch kapısı + kontur üreticisinin yüzü boş modelleri atlaması;
      freeLineBatch_ eklendi; b2b7 senaryosu GDI+GL ayrıldı + lineVisible
      piksel kanıtı (GL önce kırmızı 0/101, sonra yeşil); ctest -L gui 8/8
      + tam ctest 162/162; kabul paketi yayınlandı (15a41fb)
- [x] B1 (Katalog + hızlı arama): kök neden setHost→setCatalog sırası +
      arama indeksi; düzeltildi, ribbon testi kırmızı→yeşil, ekranda
      doğrulandı (INBOX 1. madde)
- [x] H-1 (Yüzeye Göre Görünüş): HEAD'de üretilemedi (tik yolu 9e548f3'ten
      beri değişmemiş; ekranda gerçek tıkla 3 komut doğrulandı); tanı logu +
      sözleşme testi eklendi (82be6d7) — kullanıcı build'i yenileyip
      deneyecek, tekrarlarsa trim log kök nedeni gösterecek
- [ ] INBOX 2-6: B6, B2/B7, B8, B5, GT-1
- [ ] Linux CI iş akışı (GitHub Actions)
- [x] UI-3 + UI-4 (UI-34): Özellikler paneli iskeleti (29cc028) + profil
      seçim penceresi + GT-1 t10 (bu commit); ctest 156/156. Uç ofseti ve
      Konum düzlemde/derinlik bölümleri, levha/bulon/kaynak bölümleri ve
      K1/K2 ayrı kesit olarak kaldı.

## Notlar

- Kullanıcının uzun talimatları dosyada: `C:\work\HERMES-YEREL-ILK.md`
  (ilk mesaj + Sıra), `C:\work\HERMES-MESAJ-19.md` (UI-3/4/5/6b tarifi;
  repodaki kopyası `docs/agent/ROADMAP-UI.md`).

## P-2 turu — "taş → MM nesnesi" eksikleri (2026-11-11, ZBook, gece gözetimsiz)

Sıra: `docs/agent/P-2-VAR-YOK.md`. Her madde ayrı commit + tam kapı (build + ctest).

- [x] **P-2/2** (`c466e6e`) — ankraj bulonu (`through: []`) üyeye delik AÇMIYOR.
      `tests/component_anchor_bolt_test.cpp`: A) ankraj grubunun `parts`'ında üye
      yok → delik modifier'ı yalnız levhaları hedefler, kolon B-Rep'i değişmez;
      B) normal grup `parts={main}` → ana üyede delik modifier'ı var.
- [x] **P-2/4** (`3f850d7`) — içbükey (L) `plate.polygon`: hacim **675000 mm³**
      ((300×300 − 150×150) × 10), ters sarımda da aynı (`component_materializer_test`).
- [x] **P-2/5** (`1e51059`) — malzeme listesi/BOM: `report.hpp` + `report.cpp`
      (`mm::buildMaterialList` / `materialListReport`), `report.materialList` menü
      komutu + `QtMainWindow::showMaterialList()` (Panoya Kopyala),
      `material_list_test`. Görsel bulon/kaynak modelleri levha sayılmaz.
- [x] **P-2/6** (`4e9cd87`) — `Application::translateModelsAndRecompute(indices, delta)`:
      kullanıcı "Taşı" akışının tek çekirdeği; undo → move → cut target recompute →
      bulon senkron → komponent recompute; başarısızsa fail-closed.
      `component_move_test`: kolon +2000 mm → taban levhası birlikte, adet sabit,
      kaynak görselleri 5/5 canlı, stale model 0, sahipsiz model 0.
- [x] **P-2/7** (`3f9a146`) — GT-1 `tests/gt1/t26_p2_obje_uyumlulugu_tasima.json`
      (17 adım, `ok=true`): paket uygula → `appMaterialList` → `appMoveModels`
      (+2000 mm X) → `malzeme-sonra.txt` = `malzeme-once.txt` birebir
      (5 levha / 4 bulon 1 grup / 9 kaynak), tüm kutular X≈1888…2156.
      GT koşucusuna `appMaterialList`, `appMoveModels`, `expect.fileContains` eklendi.
- [x] **P-2/3** (`cut.plane` kopyası) — kök neden: `cloneDependentsForCopies` kesme
      modifier'ını yalnız HEDEF kopyalandığında klonluyordu; yalnız düzlem
      kopyalanınca modifier hiç kurulmuyordu (kopya görünür + seçilir ama kesmez,
      INBOX B8). Düzeltme: `sourceCopied` dalı (hedef aynı, `source` kopya
      düzleme bağlı) + `copySelectionBy` / yeni `copyModelsAndRecompute` kopya
      sonrası yeni modifier hedeflerini `recomputeCutTarget` ile tazeler.
      Önce KIRMIZI: GT `t27` "kesme modifier sayisi 1 != 2"; sonra YEŞİL:
      `t27` 13 adım `ok=true` (2 düzlem, 2 görünür, 2 kesme modifier'ı).
      GT koşucusuna `appConstructionPlane`, `appPlaneCut`, `appCopyModels`,
      `expect.document {cutModifiers, planes, visiblePlanes}` eklendi.

Kapılar: `p2-245-gate.log` **169/169** (454.71 s), `p2-67-gate.log` **170/170**
(444.21 s), ikisi de `BUILD_EXIT=0` / `CTEST_EXIT=0`. GT `t26` exit 0.

Ders (bu turda öğrenilen): kapı koşarken kaynak/CMakeLists düzenlemek kapıyı
yanlış kırmızıya düşürür (`qt_exe_sources_ok` configure anını okuyor) — kapı
sürerken yalnız `docs/` düzenlenir.

## B5 — view cube (kapandı, 2026-11-11)

- [x] **B5 (a) seçim/komut korunur** — kod tarafı `88099d5`'te zaten vardı
      (`applyViewCubeTarget` yalnız kamerayı değiştirir; `cancelDrawing` /
      `cancelTransformCommand` çağırmaz) ama doğrulaması YOKTU.
      GT `t28_b5_view_cube` (31 adım, GDI): ISO'da kolon seçili → küpün
      ÖN/ÜST/SOL yüzlerine GERÇEK fare tıklaması (`ViewCube::layout`dan yüz
      merkezi → `WM_LBUTTONDOWN/UP`) → `selection` 1 kaldı, kamera standart
      görünüşe oturdu (ÖN 3.14159/1.57080, ÜST 0/0, SOL 1.57080/0;
      `view.dir.*` ile karşılaştırmalı koşum birebir aynı sonucu verdi).
      Komut ortasında (`draw.line` aktif) küp tıklaması aracı bozmadı.
- [x] **B5 (b) yüz kaybı** — bu derlemede ÜRETİLEMEDİ: ISO/ÖN/ÜST/SOL
      karelerinde gövde+flanş yüzleri dolu, GDI ve GPU'da aynı (vision ile
      incelendi). Bulunan gerçek kusur GT koşucusundaydı: `pump()` `Sleep` ile
      bekliyordu, mesaj işlemediği için ViewCube geçiş animasyonu (16 ms
      `SetTimer`) ilerlemiyor ve kup yarım dönmüş halde yakalanıyordu.
- Koşucuya eklenenler: `viewCubeFace {view}` adımı (yüz merkezi layout'tan;
      görünmeyen yüzde ilk görünür yüz + log), `expect.camera {yaw, pitch, tol}`
      (±π sarmalı), `Application::editMode()` salt-okunur erişimi, zaman
      tabanlı `pump()`.
- Kanıt: `C:\work\mm-hermes-shots\2026-11-11	28\` (GDI) ve `t28gpu\` (GPU).
  Kapı: `b5-gate.log` **170/170**, `BUILD_EXIT=0`, `CTEST_EXIT=0`, 313.28 s.

## T-5 — secim (kapandi, 2026-11-11)

- GT `t32_t5_secim` 38 adim `ok=true`, GDI ve GPU ayni.
- Tek tik (`clickModel`) -> `Secim: 1`; Esc -> 0.
- KUTU SECIMI tik-tik ile calisir: birinci tik bos noktaya kosse, ikinci tik
  pencereyi tamamlar (`completeWindowSelection`, `onLeftButtonDown` icinde).
  Tek surukleme (`drag`) secim URETMEZ. Pencere (sol->sag, tam kapsama) ve
  crossing (sag->sol) ikisi de 2 uyeyi secti.
- View cube ON tiklamasi secimi korur (2 kalir, kamera yaw=3.14159265
  pitch=1.57079633); `draw.line` sirasinda kup tiklamasi araci korur.
- `sel.part` / `sel.assembly` hata vermez; Parca modunda kutu secimi 2.
- BULGU (uretim kodu DEGISMEDI): `drag` UP'ta `selectionFirstCorner_` birakir;
  sonraki tek tik esit noktadan pencere tamamlamaya calisip erken doner
  (`first.x == x || first.y == y`). Kosucuya `boxSelect` adimi eklendi
  (Esc ile normalize -> tik -> MOVE -> ikinci tik) + `projectModels` tani
  adimi (model basina ekran kutusu, tuval olcusu).
- 3B'de bos tiklama secimi BIRAKMAZ (temizlik yalniz Esc); `draw.line` Esc ile
  sonmuyor (`activeTool` kaliyor) — kullanici beklentisi farkliysa ayri madde.
- Kanit: shots\2026-11-11\t32 (GDI) ve t32gpu (GPU). Kapi: `t5-gate.log`
  170/170, BUILD_EXIT=0, CTEST_EXIT=0, 433.94 s.

## T-6 — ozellikler paneli (kapandi, 2026-11-11)

- GT `t34_t6_ozellikler_paneli` 47 adim `ok=true`, GDI ve GPU ayni.
- Panelin gercek widget'lari suruldu: `propsRotation` + UYGULA yalniz secili
  modele yazar; secim degisince diger nesne ETKILENMEZ; `propsClass` (Sinif =
  renk kutusu) secimi ANINDA atanir (undo kapsami disinda); coklu secimde
  `propsFinish` iki modele birden yazilir, donus acilari ve profiller korunur
  (yalniz degisen alan) ve `edit.undo` TEK adimda ikisini birlikte geri alir.
- Kosucuya eklenenler: `panelSpin`, `panelColor`, `panelCombo`,
  `expect.modelProps {model, finish?, name?, hasTrueColor?, trueColor?}`.
- BULGU (uretim kodu DEGISMEDI): panel 500 ms'lik yoklamayla tazelenir; SECIM
  DEGISTIKTEN hemen sonraki ilk tazeleme o aralikta yazilan degeri ezer
  (GT'de secimden sonra `wait 700` gerekti). Kullanici secimi degistirip
  500 ms icinde alan yazarsa deger kaybolabilir — ayri madde adayi.
- Kanit: shots\2026-11-11\t34 (GDI) ve t34gpu (GPU). Kapi: `t6-gate.log`.

## T-7 — duzenle (kapandi, kismi, 2026-11-11)

- GT `t35_t7_duzenle` 41 adim `ok=true`, GDI ve GPU ayni.
- Tasi / Kopyala / Ayna / Sil gercek fare ile; her islem TEK `edit.undo`.
  Ayna kaynagi silmez (ayna kopyasi ekler). Olcumler ekran tiki kaynakli
  (±3 mm, tuval 1064x830).
- KAPSAM DISI: Tasi-D / Kopyala-D (`linearDeltaRequest_` sayisal giris yolu
  kosucuda yok; denemede surec takildi) ve Dogrusal/Dairesel dizi
  (`edit.arrayLinear/Polar` — ayni sinif). K1 (diyalog acikken ana pencere)
  sinanmadi → UI-3.
- Kanit: shots\2026-11-11\t35 (GDI) ve t35gpu (GPU). Kapi: `t7-gate.log`.

## T-8 — kesimler (kapandi, kismi, 2026-11-11)

- GT `t36_t8_kesimler` 46 adim `ok=true`, GDI ve GPU ayni.
- Plane Cut yatay/dusey uctan uca; her biri TEK `edit.undo` (cutModifiers 1→0,
  duzlem kalir). Kesim kopya (B8) kaynak duzlem kopyalaninca modifier'i klonlar.
- Bes kesim komutu UI'dan acilir; sahne bozulmaz, cokme yok.
- KAPSAM DISI (UI-3): Line/Polygon/Part Cut ve Fitting tam akisi - kosucuda 3B
  nokta/3DFACE secimi yok. Eksenel iki kolon Part Cut'ta kesim uretmedi.
- Kanit: shots\2026-11-11\t36 (GDI) + t36gpu (GPU). Kapi: `t8-gate.log`.

## T-9 — baglantilar (kapandi, kismi, 2026-11-11)

- GT `t37_t9_baglantilar` 39 adim `ok=true`, GDI ve GPU ayni.
- End Plate / Haunch(40) / 146 / Fin Plate ayni sahnede kuruldu; parametre
  degisimi mevcut kaydi gunceller (appComponentParams) ve digest degisir.
- Undo yalniz son baglantiyi kaldirir. Levhalar sahnede gri.
- Bulgu: baglanti levhalari Model Agaci'nda satir olarak gorunmuyor.
- KAPSAM DISI (UI-3): diyalog mock view, ihlal -> kirmizi check (ctest
  componentContactCheck), katalog/hizli arama ile acma (B1).
- Kanit: shots\2026-11-11\t37 (GDI) + t37gpu (GPU). Kapi: `t9-gate.log`.

## T-10 — bulon/kaynak (kapandi, kismi, 2026-11-11)

- GT `t38_t10_bulon_kaynak` 20 adim `ok=true`, GDI ve GPU ayni.
- End plate 1 bulon grubu (+1 kaynak kaydi), 146 ile 2 grup; undo ile grup
  sayisi geri dusuyor. Bulonlar 3B'de gorunuyor.
- KAPSAM DISI (UI-3): `steel.bolt` / `steel.weld` kipsel diyalog -> kosucu
  bloke (200 s timeout, surec olduruldu). Motor ctest'te kanitli.
- Kosucuya `expect.document {welds, boltGroups}` eklendi.
- Kanit: shots\2026-11-11\t38 (GDI) + t38gpu (GPU). Kapi: `t10-gate.log`.

## T-11 — gorunumler (kapandi, kismi, 2026-11-11)

- GT `t39_t11_gorunumler` 40 adim `ok=true`, GDI ve GPU ayni.
- Plan (0/0), 3B (0.7854/0.6155), On (pi/pi2) kamera degerleriyle; dort gorsel
  stil + dongu; yuzeye gore gorunus 2. viewport.
- F6 iki kosumda da backend'i degistirmedi; GPU yolu backend:"gpu" kosumuyla
  kanitli.
- KAPSAM DISI (UI-3): calisma duzlemi 3 nokta / sifirla (3B nokta secimi yok).
- Kanit: shots\2026-11-11\t39 (GDI) + t39gpu (GPU). Kapi: `t11-gate.log`.

## T-12 — kayit (kapandi, kismi, 2026-11-11)

- GT `t40_t12_kayit` 34 adim `ok=true`, GDI ve GPU ayni.
- Kaydet -> file.new -> ac: profil/duzlem/bulon grubu/baglanti/temas/renk/donus
  birebir korunuyor (30 derece donus dahil).
- KUSUR ADAYI: donuslu uyede baglanti geometrik digest'i kayit-ac sonrasi
  degisiyor (23cbf56fbf0b9e70 -> a6a0488f14991650); donussuz turda ayni.
- Duzlem kesme bilesenden SONRA kurulamiyor (fail-closed); profilli kiriste
  9 modifier, kolonda 1; cutModifiers bilesen kesimlerini de sayiyor.
- Kanit: shots\2026-11-11\t40 (GDI) + t40gpu (GPU). Kapi: `t12-gate.log`.

## T-13 — kisayollar (kapandi, kismi, 2026-11-11)

- GT `t41_t13_kisayollar` 77 adim `ok=true`, GDI ve GPU ayni.
- Kosucu duzeltmesi: `key` adimi artik SendInput + tuval OS odagi + modifier
  onekleri + 150 ms pump. Eski yol hicbir kisayolu tetiklemiyordu.
- G/L/M/K/I/T/E, Esc, Delete, Ctrl+Z/Y, Ctrl+5/7, F9 (GRID), F8 (ORTHO),
  F2, Alt+1..4 ve Turkce karakter girisi kanitlandi.
- BULGU: F7 hicbir bayragi degistirmiyor.
- KAPSAM DISI (UI-3): O/Q/D modal sayisal giris -> kosucu kilitleniyor.
- Kanit: shots\2026-11-11\t41 (GDI) + t41gpu (GPU). Kapi: `t13-gate.log`.

## T-14 — cizim / paper (kapandi, kismi, 2026-11-11)

- GT `t42_t14_cizim` 16 adim `ok=true`, GDI ve GPU ayni.
- Kosucuya `appPaper {model, pdf}` adimi eklendi: `draw.paper` diyalogunun
  ARKASINDAKI ayni cekirdek (`buildDrawingSet` + `exportSheetPdf`).
- Kolon (HEB240) ve kiris (IPE300) icin 1 pafta, 4 gorunus
  (TOP/FRONT/SIDE + izometrik, 1:20 + A-A kesiti), PDF ~20 KB yazildi;
  PDF metin katmaninda baslik blogu ("Part P1 - HEB240") dogrulandi.
- Uretim belgeyi degistirmiyor (2 profilli model korunuyor).
- `expect.paper {sheets, views, pdfOk, pdfBytesAtLeast}` denetimi eklendi.
- KAPSAM DISI (UI-3): `draw.paper` kipsel diyalog (`exec()`) -> kosucu
  kilitleniyor; UI'dan pafta acma/olcek secme akisi GT'de yok.
- BULGU: varsayilan kagit A2 (A3 secilmiyor) — ayri madde adayi.
- Kanit: shots\2026-11-11\t42 (GDI) + t42gpu (GPU). Kapi: `t14-gate.log`.

## recipe_test — kontrata baglandi (2026-11-12)

- `libs/mm_contract/tests/recipe_test.cpp` kok agacta DERLENMIYORDU
  (`add_subdirectory` yok). Hedef `model_maker_recipe_test` eklendi;
  ctest 170 -> 171.
- Bayat beklentiler duzeltildi: surum 1.11.0 -> 1.24.0, tas 18 -> 20
  (`frame.bisector`, `member.fit` J listesine de eklendi).
- Sonuc: `recipe test: OK (0 hata)`, `J: 20/20`, `L: 20 tas`.
- Kapi: `recipe-gate.log`.
