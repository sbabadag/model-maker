---
name: component-contract-packages
description: "Use when editing the mm_component_contract surface."
version: 1.0.0
author: Hermes Agent
license: MIT
platforms: [linux, windows]
metadata:
  hermes:
    tags: [contract, recipe, package, digest, semver, C++]
    related_skills: [model-maker, model-maker-component-development]
prerequisites:
  commands: [git, g++, cmake, ninja]
---

# component contract + recipe + paket ekseni

İki uygulama (Model Maker ve Studio/mmcs) aynı `libs/mm_contract` yüzeyinden aynı
`planDigest`'i üretmek zorunda. Bu skill o eksende değişiklik yapmanın sırasını ve
sessiz bozulma tuzaklarını taşır: recipe taşları, contract sürümü/pinleri, altın test
digest'leri, `.mmcpkg` yükleyici. Repo haritası ve makineye özel derleme için `model-maker`
skill'i geçerlidir; buradaki kurallar onun üstüne biner.

## When to Use

- `libs/mm_contract` altında değişiklik: yeni recipe taşı/argümanı, `BlockDecl`/`validateRecipe`,
  `ComponentInput`/`ComponentInput` codec, `GenerationPlan.checks[]`.
- Contract sürümü (semver) veya pinleri, altın test / `planDigest` golden'ları.
- `.mmcpkg` paket yükleyici, paket `tests/*.json`, `checksums.json`, kap (klasör/ZIP) kararı.
- "Studio şunu istiyor / digest tutmuyor / paket reddedildi / contract sürümü" gibi iki uygulama
  arası sözleşme sorunları.

## Değişiklik akışı (sıra önemli)

1. **Contract testleriyle başla:** `cmake -S libs/mm_contract -B build-contract` →
   `cmake --build build-contract -j 2` → `ctest --test-dir build-contract`. Saniyeler sürer;
   recipe/paket mantığını burada doğrula.
2. **Değişikliği yaz**, sonra yalnız İLGİLİ ana-ağaç testini hedefli derle
   (`cmake --build build-occ-hermes -j 2 --target <test>`). Commit'ler arası tam suite koşma —
   kullanıcının hız tercihi: yalnız ilgili testler; tam suite + Win32 kapısı YALNIZ push öncesi.
3. **Public yüzey değiştiyse sürümü bump'la** ve pinleri güncelle (ekleme = minor).
4. **Altın test/digest etkilendiyse** yeni digest'i probe ile ÜRET, elle yazma.
5. Commit mesajını dosyaya yazıp `git commit -F <dosya>` ile ver. Madde başına ayrı commit +
   test özeti; push `origin-ssh`, `main`'e merge yok, GUI kabulü kullanıcıda.
6. **Push öncesi tek sefer:** tam `cmake --build build-occ-hermes -j 2` + `ctest` + Win32 kapısı
   (`x86_64-w64-mingw32-g++ -fsyntax-only -std=c++20 -Iinclude -Ilibs/mm_contract/include
   -Ithird_party src/recipe.cpp`; nlohmann `third_party/nlohmann`'da). Tam suite 178 hedefle ~9 dk
   sürer — background başlat, `process_manage(wait)` ile bekle.

## Always-on kurallar

- **Statik denetim çalışma anından ÖNCE gelir; blok yüzeyi TEK kayıttan beslenir.** Bir taşın
  argümanları `blockRegistry()`'de KATEGORİYLE kayıtlıdır: `numArgs` (sayısal ifade), `strArgs`
  (literal metin), `listArgs` (liste), `strParamArgs` (`param.<ad>` kabul eden metin), `boolParamArgs`
  (mantıksal; `param.*` Boolean olmalı), `memberArgs` (girdi rolü main/secondary_N), `exprListArgs`
  (ögeleri ifade olan liste: `chamfers` ':', `outline` ','). Bunun DIŞA AÇIK, salt-okunur aynası
  `blockDeclarations()` (recipe.hpp): her taş için argüman adı + `RecipeArgKind` + isList/exprItems,
  ada göre alfabetik. Studio aynasını buna bağlar; `validateRecipe` içindeki J (bilinmeyen argüman)
  ve M (bool-param/üye-roĺü/liste-ifade) denetimleri de `blockDeclarations()`'tan kind filtreleyerek
  okur — kategori listelerini validateRecipe içinde İKİNCİ kez elle yazma. Yeni argüman eklerken beşi
  birden: blok gövdesi, `blockRegistry()` girdisi (doğru kategori), `blockDeclarations()` kind
  eşlemesi (ekleme gerekmiyorsa bile doğru türün düştüğünü test et), `RECIPE.md` satırı, test.
  `memberArgs` kapsamını dar tut: yalnız GERÇEKTEN girdi rolü alan argümanlar (`frame.member_end.member`,
  `cut.plane.target`, `bolts.grid.through`). `weld.fillet.roleA/roleB` levha roleKey'i de alabilir,
  `endplate.placement.main/beam` kendi çalışma-anı denetimini yapar — bunları memberArgs'a koyma,
  yoksa mevcut recipe'ler (`roleB="end_plate"`) fail-closed kırılır.
- **Fail-closed ve mesaj dolu.** Her `return false` dalı Türkçe `ctx.error` kurmalı; boş mesajlı
  `ok=false` yasak (kullanıcı nedenini göremez).
- **Sessiz geometri değişimi yasak.** Yeni argüman verilmediğinde mevcut plan ve digest AYNI
  kalmalı; nötr varsayılan (ör. 0) seç, verildiğinde plana ve digest'e girsin.
- **El yönü serbest, formül tek.** Sol elli çerçeve (`u × v = −axis`) reddedilmez ve "düzeltilmez";
  döndürme kodunda eksenleri `u × v` ile yeniden üretme (sol elli çerçeveyi sağa çevirir).
- **Kırpma değil ret.** Sığmayan pah/ölçü kırpılmaz, açık Türkçe hatayla reddedilir.
- **Altın test = sözleşme.** Paket içindeki `tests/*.json` girdi taşıyorsa yükleyici recipe'yi
  koşar ve `planDigest` uyuşmazlığında paketi reddeder; replay'i "CI'da koşar" diye erteleme.
- **Aynı fiziksel girdi = aynı digest; digest GEVŞETİLMEZ, girdi KANONİKLEŞTİRİLİR.** İki uygulama
  aynı üyeyi farklı uç sırası/çerçeveyle verince digest farklı çıkıyorsa `generationPlanDigest`
  hesabına dokunma: contract'a TEK dönüşüm fonksiyonu ekle (`memberGeometryFromProfile(...)`) ve
  iki tarafı da ona bağla (Model Maker `memberGeometryFor`, Studio adaptörü — ikinci dönüşüm yok).
  Kural seti: `axisTo` daima bağlantı ucu / `axisFrom` uzak uç (çağıran `connectionAtA` ile bildirir);
  `refNormal` eksene dikleştirilir ve işareti kanonikleştirilir (ilk sıfırdan farklı bileşen pozitif)
  → `n` ile `−n` aynı KANONİK refNormal ve kesit çerçevesini verir (u/v ve bulon `xDir` ters dönmez);
  AMA işaret çevrildiğinde (`n→−n`) çerçevenin genişlik VE yükseklik yönü ters döner
  (`height = axis × (n × axis)`), bu yüzden `sectionCenterOffset` DA negatiflenmelidir — yoksa eğik
  kirişte (Top/Bottom ankraj) kesit bir tam yükseklik kadar yanlış tarafa kayar. Ofset ≠ 0 iken
  `n` ile `−n` ayna görüntüsü üyelerdir (aynı MemberGeometry DEĞİL, ofset işareti ters) — "n/−n aynı
  geometri" iddiası ofset ≠ 0 için YANLIŞTIR. `profileRotation` `[0,360)` ve kanonik eksene göre. Kabul testi: aynı üye iki uç sırasıyla → aynı `MemberGeometry`
  **ve** aynı `planDigest` (gerçek generatorle), artı `n/−n`, eğik `n`, `n ∥ axis`, sıfır uzunluk.
  DİKKAT: "axisTo = bağlantı ucu" kuralı SADECE KURULUM SIRASINDA uygulanır
  (`memberGeometryFromProfile` çağrısı). Sonradan eklenen `isCanonicalMemberGeometry(g)`
  yalnız refNormal/dönüş idempotence'ını ölçer; MemberGeometry bağlantı-ucu bilgisini
  TAŞIMAZ, dolayısıyla hangi ucun bağlantı olduğunu söyleyemez. El ile kurulan bir girdi
  bağlantı ucunu `axisFrom`'a koymuşsa (konvansiyona aykırı) bu denetim YEŞİL geçer ama
  digest yine farklı çıkar — çünkü yatay eksende `refNormal {0,0,1}` zaten dik/birim/
  işaret-kanonik ve dönüş zaten aralığındadır. Bir girdinin "ters kurulduğunu" ispatlamak
  için `isCanonical` DEĞİL, bağlantı noktasının konumunu doğrudan assert et.
  `memberGeometryFromProfile` ayrıca YENİ bir `MemberGeometry` döndürür ve `id`'yi
  TAŞIMAZ (kInvalidEntityId kalır); test/probe'da kanonikleştirdikten sonra `.id`'yi geri
  atamadan kullanırsan rol çözümü (`inputRoleFor`) kırılır ve digest SESSİZCE değişir
  (belirti: kanonikleştirme geometriyi değiştirmediği hâlde digest farklı görünür).
- **Digest KİMLİK-BAĞIMSIZDIR (MM-2b): EntityId değil, SEMANTİK ROL ANAHTARI hash'lenir.**
  `generationPlanCanonicalJson` plandaki üye referanslarını rol anahtarına çevirir: `WeldSpec.partA/partB`
  → `partARole/partBRole` ("main"/"secondary_N"/plateKey), `BoltGroup.parts` → `partRoles`,
  `CutModifier.target` → `targetRole`; malzemeleşme-zamanı kimlikleri (bolt `id`, `displayEntityId`,
  `shapeParts`, cut `tool/source/owner`) digest'e HİÇ girmez. Böylece aynı fiziksel girdi, iki
  uygulamada farklı EntityId ile de aynı digest'i verir. Yeni role alanı eklemek (örn. `partRoles`,
  `targetRole`) semver minor bump + golden yenileme ister. Kabul testi: aynı girdi, farklı id'lerle
  (ör. Document'ta farklı insersiyon sırası) → aynı digest.
- **Okuyucu fail-closed: varsayılana düşme, tür çevirme, gevşek sürüm yok.** (a) `manifest.displayName`
  zorunlu — yoksa `typeId`'ye düşürme, açık hata ver. (b) Recipe argümanında JSON `true/false`
  **metne çevrilmez**; sessizce `"1"`/`"0"` olmasın, ret mesajı adımı + argüman adını söylesin
  (dizi/nesne içindeki boolean'lar dahil). (c) `recipeVersion` semver + major eşleşmesi ister
  (`2.0.0` ret). Her biri için negatif fixture: paketi geçici dizine kopyala, alanı boz,
  `checksums.json`'ı yeniden yaz, yüklemenin REDDİNİ ve mesajın alanı adlandırmasını assert et.

## Tuzaklar (neden + belirti)

- **Tek taraflı sınırı İKİ taraflı clamp ile uygulama.** `validateParamValue`'da `hasMinDouble`
  varken `hasMaxDouble` yoksa `clampD(v, min, max=0)` çağrısı değeri `[min, 0]`'a kırpar; min > 0
  için `std::clamp` lo > hi tanımsız davranış. Her sınırı yalnız bayrağı varsa uygula:
  `if (hasMin) v = std::max(v, min); if (hasMax) v = std::min(v, max);`. Int yolu da aynı.
  Testte dört durumu kapsa: yalnız-min, yalnız-max, ikisi, hiçbiri.
- **Push öncesi tam suite kırmızıysa ÖNCE kendi commit'lerini ölç, sonra suçu at.** Commit'ler
  arası yalnız ilgili test koşulduğu için tam suite ilk kez push öncesi koşulur ve ÖNCEKİ
  maddelerin regresyonlarını ortaya çıkarabilir. `git diff --name-only <eski-head>..HEAD` ile
  commit'lerinin dokunduğu dosyaları listele; kırmızı test o yollara dokunmuyorsa regresyon
  SENİN DEĞİL, önceki işin kalıntısı. Raporu buna göre ver (kendi commit'in izole/yeşil; eski
  kırmızıyı ayrı madde diye işaretle), kendi maddene karıştırıp sessizce "fix" etme.
- **Bayat build.ninja:** `libs/mm_contract/CMakeLists.txt`'e dosya ekleyince `cmake --build`
  "no work to do" der ve eski kütüphaneyi bırakır → testler eski kodu doğrular. Kontrol:
  `grep -c <yeni_dosya> build-occ-hermes/build.ninja` = 0 ise yeniden yapılandır.
- **Kaynak listesi İKİ yerde:** contract kütüphanesine eklenen her `.cpp` hem kütüphanenin kendi
  `libs/mm_contract/CMakeLists.txt`'ine hem kök `CMakeLists.txt`'teki `mm_component_contract`
  hedefine yazılmalı. Yalnız ilkine yazılırsa dosya contract testlerinde koşar (test .cpp'yi
  doğrudan derliyorsa yeşil kalır) ama UYGULAMAYA hiç girmez: belirti "kod var, davranış yok"
  ya da app hedefinde undefined reference. Kontrol: `grep -n <dosya> CMakeLists.txt
  libs/mm_contract/CMakeLists.txt` iki satır dönmeli.
- **Yeni contract BAŞLIĞI elle derleme harness'larına da ulaşmalı.** `libs/mm_contract/include`
  altına yeni bir başlık ekleyip bir çekirdek kaynağa (`src/bolt.cpp` gibi) include edersen, o kaynağı
  KENDİ `-I` listesiyle derleyen betikler (`tests/run_occ_solid_ids.py` vb.) kırılır — belirti YALNIZ
  tam suite'te `fatal error: mm_contract/<yeni>.hpp: No such file`, normal build yeşil. Harness'ın
  derleme komutuna `-I <repo>/libs/mm_contract/include` ekle ve değişiklikten sonra `tests/`
  altındaki derleme betiklerini grep'le.
- **Sürüm bump'ı DAVRANIŞ/İMZA değişimine bağlıdır, her yeni dosyaya değil.** Yeni ve kendi kendine
  yeten bir başlık (ör. ISO bulon ölçü tablosu) eklemek mevcut hiçbir bildirimi, plan alanını veya
  digest'i değiştirmiyorsa pin/golden dokunmadan bump GEREKMEZ; contract testleri yeşil kalır.
  Bump'ı yalnız mevcut bir imza/alan/digest değiştiğinde yap (o zaman pin + golden da güncellenir).
- **Sürüm pinleri:** bump sonrası `recipe_test`, `contract_smoke_test`, `component_codec_test`
  pinleri + `contractSupports` alt/üst sınır assert'leri güncellenmeli; yoksa suite kırmızı.
  ÜST sınır assert'ini de yeni minor'a TAŞI (`contractSupports(1,8)` artık true kabul edilir,
  `1,9` false) — yoksa test bir önceki sürümün tavanını doğrulamaya devam eder ve gerçek regresyonu
  kaçırırsın.
- **Paket dosyaları KODLA senkronize tutulur — sürüm/diyalog değişince aynı değişiklikte yeniden üret.**
  `mm_gen_*_package` manifest/parameters/dialog/recipe'yi koddan üretir; semver bump, dialog/param
  varsayılanı değişimi ya da `preview` eklenmesi bu dosyaları BAYAT bırakır (belirti: manifest
  `minContractVersion` eski, `parameters.json` `defInt`/açıklama eski, `dialog.json.preview` eksik).
  Araçsız `end_plate_pkg` paketi `end_plate`'ten kopyalanır (recipe + parameters aynıdır) ve
  `checksums.json` SHA-256'sı `sha256sum` ile yeniden hesaplanır. "Sonra bir kez yeniden üretirim"
  bayat dosyaları alakasız bir commit'in yüzeyine taşır.
- **Probe linki:** `libmm_component_contract.a` tek başına link etmez — `src/geometry.cpp` ve
  `src/sha256.cpp` de komut satırında olmalı.
- **`json::parse(...)["key"]` üzerinde range-for:** `operator[]` referans döndürür, geçici yok olur
  → boş/dangling liste. Önce `const json pj = json::parse(text);`.
- **printf/wprintf karışımı:** aynı akışta sıra kayar, satırlar kaybolur; `utf8FromWide` + `printf`.
- **Kabuk backtick'i:** `git commit -m "...`kod`..."` mesajı bozar; mesajı dosyaya yaz, `-F` kullan.
- **Recipe yolu AYRI bir plan kurucusudur (generator'la paylaşılmaz).** `runRecipe` blokları
  (`endplate.placement`, `weld.fillet`, `cut.plane`, `bolts.grid`) planı kendi kurar; yalnız Haunch 40
  recipe (`runHaunch40Block`) aynı `component_engine.cpp` fonksiyonlarına delege eder, o yüzden orayı
  otomatik miras alır. Digest'i değiştiren VEYA yeni role/kimlik alanı ekleyen bir değişiklik
  generator'a yazılınca ilgili recipe bloğuna DA yazılmalı, yoksa `*_equivalence_test` "gen != rec"
  verir (belirti: `digest(runRecipe) == digest(generateX)` satırı FAIL — golden değil). Eşdeğerlik
  testi gen= ve rec= digestlerini yan yana basar; bir değişiklikten sonra İKİSİNİN de eşitliğini
  gözle doğrula, yalnız golden satırını değil.

- **Altın fixture'lar GERÇEK katalog kesitiyle kurulur, elle uydurulmuş ölçüyle değil.**
  Golden testinde `MemberGeometry`'yi elle doldurmak (ör. adı "IPE200" olan 200×400 bir kesit)
  katalog→kesit çerçevesi yolunu tamamen atlar; o yol bozulduğunda tüm suite yeşil kalır.
  Katalogu `loadProfileCatalog(<repo>/profiles)` ile yükle (CMake'te hedefe
  `MM_PROFILES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/profiles"` tanımı ver) ve matrisi gerçek
  kesitlerle kur: kolon {HEA300, HEB300} × kiriş {IPE200 100×200, IPE300, IPE400} ×
  bulon {M16, M20}. Vakaların hepsi checks'ten GEÇMELİ (varsayılan gauge EC3'e uygun
  değilse golden değil varsayılan düzeltilir).
- **Eski sahte profili adını dürüstleştirerek emekliye ayır, ölçüsünü değiştirme.**
  Mevcut golden'lar sahte ölçüye bağlıysa profili olduğu gibi bırakıp `TESTPROFIL-400x200`
  gibi dürüst bir adla yeniden adlandır: ölçü aynı kaldığı için digest DEĞİŞMEZ ve golden
  churn'ü olmaz; gerçek kesitler AYRI yeni anahtarlarla eklenir. Golden commit mesajına
  dosya bazında "değişiklik yok / YENİ n anahtar" gerekçesini yaz.
- **Yeni golden'ı elle yazma; test kendisi bastırsın.** Golden dosyasını `{}` olarak koy,
  testte anahtar eksikse `GENDIGEST <key>=<digest>` yazdır, koş, çıktıyı dosyaya geçir,
  tekrar koş (bu koşuda 0 hata beklenir). Digest'i elle hesaplamak ya da testin "yoksa üret"
  diye golden'ı kendisi yazmasına izin vermek sözleşmeyi sessizce kaydırır.
- **Golden girdilerini KANONİKLEŞTİRİRKEN: üreteci değiştir, geometri-kanıt testi ekle, sonra golden'ı güncelle.**
  Elde `MemberGeometry` alan atamasıyla kurulmuş golden girdisi `memberGeometryFromProfile`'ı
  atlar; girdiyi bu fonksiyonla yeniden kur (main/secondary hepsi, bağlantı ucunu `connectionAtA`
  ile bildir) ve her girdiye kanoniklik CHECK'i ekle. Golden'dan ÖNCE, golden'dan BAĞIMSIZ bir
  "dünya geometrisi aynı" kanıt testi yaz: levha DÜNYA köşe noktaları (`center + u·ou + v·ov`),
  bulon merkez kümesi (`boltGroupBoltOrigins`), kesim düzlemi (`planeOrigin`/`planeNormal`)
  1e-6 kümeler olarak eşleşmeli; yalnız digest farklı (çerçeve işareti farkı). Bu kanıt yeşil
  olmadan golden dosyasına dokunma. Değişen digest'leri probe ile ölç: genelde YALNIZCA kanonik
  olmayan girdi değişir (diğerleri nötr); değişenleri commit mesajında listele.
- **`memberGeometryFromEntityProperties` bağlantı-ucu varsayımı main/secondary için AYNI DEĞİL.**
  Document modelinde kolon bağlantı ucunu `axisTo`'da (üst uç), kiriş ise `axisFrom`'da (kolon
  tepesinden başlar) taşır; ama builder tüm üyeler için `endA=axisTo, connectionAtA=true`
  varsayar → kirişte ters. Simetrik kesit + dönüş 0'da (haunch40) eksen yönü digest'e girmediği
  için görünmez; dönüş 90'da (end plate) ortaya çıkar. Modelden kurulan digest kanonik golden'la
  uyuşmuyorsa ÖNCE bu asimetriyi kontrol et, digest'e dokunma.
- **`autoPreviewDefinition` "etkisiz" listesi ÜÇ sınıf parametrede sahte negatif üretir (δ=1 mm
  pertürbasyonu yanıltır).** (a) Ayrık/standart değerli (bolt/hole çapı): δ=1 M20→21 gibi
  standart-dışı değer verir, plan değişmemiş görünür — komşu STANDART değerle dene (M20→M24,
  d0 22→26; `standardBoltDiameters()`/`standardBoltHoleDiameter`). (b) Fark yalnız bulon `params`
  alanındaysa (nominalDiameter/holeDiameter): analiz levha kalınlık/genişlik/yükseklik + bulon
  spacingX/Y/origin karşılaştırır, bulon params'ı DEĞİL — çap değişimi görünmez kalır. Çapı
  değiştiren parametre "etkili" say ve olcu değil ETIKET olarak işaretle (MM-6b); hiç çap değişimi
  üretmiyorsa bu bir HATADIR, çapı yok sayan kodu bul. (c) Sentinel varsayılanlı (defDouble ≤ -1e8
  "tanımsız" / -1 "otomatik"): δ sentinel'i aşamaz, plan hiç değişmez görünür — bunları "bu modda
  etkisiz: <sebep>" gerekçesiyle AYRI raporla ("tanımlı değer verince etkili olur") ve Default
  kutusu toggle'ında yeniden değerlendir (mod = tanımlı/tanımsız, boltCountY DEĞİL). Bir
  parametrenin GERÇEKTEN etkili olup olmadığını önce probe ile ÖLÇ (her parametreyi pertürbe edip
  plan alanlarını yazdır), jeneratör kodunu okuyup tahmin etme.

Ayrıntılı tablolar, komutlar ve paket yükleyici sırası:
`references/contract-recipe-packages.md`.

## Referanslar

| Dosya | Ne zaman |
|---|---|
| `references/contract-recipe-packages.md` | İki derleme ağacı, sürüm bump pin listesi, recipe taş kataloğu, altın test/digest üretimi, paket yükleyici kuralları, commit/komut hijyeni |
