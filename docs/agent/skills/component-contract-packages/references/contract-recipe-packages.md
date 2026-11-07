# Contract, recipe ve paket ekseni (`libs/mm_contract`)

Component contract yüzeyi (recipe taşları, altın testler, `.mmcpkg` yükleyici) üzerinde
çalışırken geçerli iş akışı ve tuzaklar. OCC/Qt yok — bu katman OCCT'siz test edilir.

## İki derleme ağacı — hangisi ne için

| Ağaç | Configure | Kapsam | Süre |
|---|---|---|---|
| `build-contract` | `cmake -S libs/mm_contract -B build-contract` | contract testleri (smoke, codec, recipe, package, preview, checks — 11 test) | saniyeler |
| `build-occ-hermes` | `cmake -S . -B build-occ-hermes` (OCC_ROOT=/usr, MM_OCC_LINK_DIRECT=ON) | tam suite (162 test; `occ_solid_ids` tek başına ~165 s) | ~10 dk |

- **TUZAK (sessiz bayat derleme):** `libs/mm_contract/CMakeLists.txt`'e kaynak/test ekleyip
  `cmake --build build-occ-hermes` dersen ninja `no work to do` der ve ESKİ kütüphaneyi bırakır
  (build.ninja yeni dosya adını içermez). Kontrol: `grep -c <yeni_dosya> build-occ-hermes/build.ninja`
  → 0 ise `cmake -S . -B build-occ-hermes` ile AÇIKÇA yeniden yapılandır.
- **TUZAK (kaynak listesi iki yerde):** kütüphaneye eklenen `.cpp`'yi yalnız
  `libs/mm_contract/CMakeLists.txt`'e yazmak yetmez; kök `CMakeLists.txt`'teki
  `mm_component_contract` hedefi de güncellenmeli. Aksi halde kod contract testlerinde koşar
  (test dosyayı doğrudan derler) ama app hedefine hiç girmez — "kod var, davranış yok" veya
  app'te undefined reference.
- `TMPDIR=$HOME/.hermes/cache/scratch` ver (tmpfs dolunca derleme SIGBUS verir); bellek kısıtlı
  host'ta `-j 3`, ctest ayrı adımda.

## Contract sürüm bump'ı = pin avı

Public yüzeye ekleme (yeni başlık, yeni recipe taşı/argümanı, yeni public metot) → **minor**.
Bump'ta unutulmaması gerekenler:

1. `contract_version.hpp`: `kContractVersionMinor` + `kContractVersion` metni.
2. Pin assert'leri: `recipe_test`, `contract_smoke_test`, `component_codec_test`
   (`kContractVersion == "x.y.z"`) + `component_visual_test` (`minor >= N`).
3. `contractSupports` sınır assert'leri: "mevcut sürüm desteklenir" + "üst minor desteklenmez"
   çiftini yeni sürüme kaydır.
4. `packages/*/manifest.json` `minContractVersion` genelde ESKİ kalır (paket hâlâ uyumlu).

## Recipe taşları (blok kataloğu)

Katalog KODDADIR: `src/recipe.cpp` → `blockRegistry()`. `BlockDecl` =
`{name, numArgs, strArgs, listArgs, fn, strParamArgs}`. Yeni taş/argüman dört yerde biter:

1. bloğu yaz (argümanı `numArg` / `strArg` / `strParamArg` / `strListArg` ile oku, hatada `ctx.error`),
2. `blockRegistry()` girdisi (deklare edilmeyen argüman statik taramadan kaçar),
3. `libs/mm_contract/RECIPE.md` tablo satırı,
4. `recipe_test` davranış testi (başarı + fail-closed dalları).

- **Metin argümanı** `param.<ad>` kabul edecekse `strParamArg` kullan ve bloğu `strParamArgs`
  listesine ekle. Bu liste `validateRecipe`'nin statik denetimini besler: parametre şemada yoksa
  "bilinmeyen parametre", metin tipinde değilse "metin parametresi degil". Listeye eklemeyi
  unutursan hata çalışma anına kayar. `strArg` yalnız rol/anahtar/token argümanları içindir.
- Çerçeve taşları: `frame.member_end` = `memberSectionFrame` (refNormal + `profileRotation`),
  `frame.offset` yalnız origin'i kaydırır, `frame.rotate` eksen etrafında döndürür. Döndürmede
  `w`'yi `u × v` ile YENİDEN ÜRETME — sol elli çerçeve sağa döner; eksenleri ayrı normalize et ve
  Gram-Schmidt ile dikleştir.
- Çerçeve AD UZAYLARI iki ayrıdır: generic bloklar `frameArg` ile `ctx.frames`'i (üretici
  `frame.member_end/offset/rotate`; tüketici `frame.offset/rotate`, `plate.rect/polygon`,
  `bolts.grid`, `cut.plane`), haunch blokları `haunchFrameArg` ile `ctx.haunchFrames`'i (üretici
  `frame.contact/plumb_face` + `haunch.*` + `bolts.haunch_groups` + `haunch.welds`) okur/yazar.
  `validateRecipe` bir `frame` argümanının yalnız KENDİ ad uzayında ÖNCEKİ bir adımın id'sini
  göstermesini denetler — değilse "recipe adim <n> (...): tanimsiz cerceve: <id>" ret. Sınıflandırma
  helper'ları (`producesGenericFrame`/`producesHaunchFrame`/`frameArgReadsHaunch`) `blockRegistry()`
  ile paralel tutulur; yeni taş eklerken İKİSİNİ de güncelle.
  `frame.contact` (O) ARTIK İKİSİNİ de üretir (opak haunch + generic yüz çerçevesi): bu yüzden
  `validateRecipe`'teki ad-uzayı kaydı `if/else if` DEĞİL iki bağımsız `if` olmalı (yoksa ikinci
  ad uzayı kaydedilmez ve generic taş "tanimsiz cerceve" verir). `frame.contact` argümanları
  `main/secondary` DEĞİL `member/target` (member=secondary_N, target=main); `haunch40_param_flow_test`'in
  "0b sabit sayisal arguman" skip listesi bu arg adlarını SABİT yazar — arg yeniden adlarken orayı da
  güncelle (belirti: `FAIL 0b sabit sayisal arguman KALMADI`). Yeniden adlandırmada J mesajı YENİ adı
  söylemeli: `validateRecipe`'nin bilinmeyen-argüman dalına eski→yeni eşleme (`renamedArgHint(use,arg)`)
  ekle; yoksa dış paket yalnız "bilinmeyen arguman: frame.contact.main" alır, `member/target`'ı öğrenemez.
- Levha taşları: `plate.rect.chamfers` = `"corner:type:h:v"` listesi (alanlar ifade olabilir);
  bacaklar KIRPILMAZ, kenarı aşan / komşusuyla çakışan / aynı köşeye ikinci pah REDDEDİLİR.
  `plate.polygon.outline` = `"u,v"` listesi, noktalar levha MERKEZİNE göre; ≥3 nokta, yalnız
  dışbükey + basit kontur (içbükey ve kendini kesen reddedilir). `plate.polygon` `p.width/height`
  kullanmaz (materializer `outline` yolundan gider), `outline` elemanları `pair<double,double>`.
- `bolts.grid.grip` / `holeDepth`: `grip` verilmezse VARSAYILAN grip hesaplanır = `through`
  üyelerinin delinen kalınlıkları toplamı (bulon ekseni kesit YÜKSEKLİĞİNE hizalıysa flans `tf`,
  GENİŞLİĞİNE hizalıysa gövde `tw`; ayrım `|w·height| >= |w·width|` ile, `memberSectionFrame(m)`).
  `holeDepth` verilmezse = `grip + 2` (end plate kuralı). Açık `grip`/`holeDepth` verilirse DOKUNMA.
  Studio grip TÜRETMEZ — değer her zaman plandan gelir. Eski "0 kalır" davranışı kaldırıldı;
  `recipe_test` HR-4 bloğunu da güncelle.

## Kanonik recipe/paket metin yazıcısı

- `recipe.json` KAYNAK DOĞRUDUR (elle yazılır, `param.*` referansları taşır). Test-local
  `endPlateRecipe()`/`haunch40Recipe()` (recipe_test/equiv testleri) LİTERAL değerlerle kurulur —
  paketi yeniden üretmek için kullanılmaz; ikisi birbirinin yerine geçmez.
- `recipeCanonicalText(const Recipe&)`: anahtar sırası kontrollü (kok = recipeVersion, let, steps;
  adım = id, use, role, when, check, message, args), args alfabetik (`RecipeStep::args` std::map),
  formatVersion YOK, `dump(2)` + tek `\n`. `nlohmann::ordered_json` (basic_json<ordered_map>) ile
  kontrollü sıra; düz `nlohmann::json` (std::map) anahtarları HER ZAMAN alfabetik yazar — sıra
  kontrolü için ordered_json şart.
- `packageJsonText(json)` = `dump(2)+"\n"`; `writePackageJsonFile(path, text)` CR baytı içeren metni
  REDDEDER ve BINARY modda yazar (Windows metin modunda `\n`→`\r\n` LF-only garantisini bozar).
  Studio aynı fonksiyonları kullanır → iki taraf bayt-ayni dosya yazar.

## Altın test / digest

Test dosyası biçimi:
`{ "name": ..., "input": <ComponentInput JSON>, "expect": { "planDigest": "<sha256>" } }`.
Yükleyici `input` varsa recipe'yi koşar, digest tutmazsa paketi REDDEDER; `input` yoksa yalnız
yapısal doğrular (uyarı). Bilinmeyen ek anahtarlar yok sayılır ama replay için `input` zorunludur
(Studio tarafında `x-mmcs-member` gibi ek anahtarlar sorun çıkarmaz).

Digest üretmek için küçük probe yeterlidir — mevcut `.a` tek başına yetmez (geometry + sha256 sembolleri eksik):

```bash
g++ -std=c++20 -O0 -Iinclude -Ilibs/mm_contract/include -Ithird_party probe.cpp \
    src/geometry.cpp src/sha256.cpp -o probe build-contract/libmm_component_contract.a
```

- **TUZAK (dangling):** `for (const auto& s : json::parse(text)["parameters"])` — `operator[]`
  referans döndürür, geçici json ifade sonunda yok olur → döngü boş/dangling. Önce
  `const json pj = json::parse(text);` sonra `pj["parameters"]`.
- **TUZAK (çıktı):** aynı akışta `printf` ile `wprintf` karıştırma (sıra kayar, satırlar kaybolur).
  Geniş metni `utf8FromWide` ile daralt ve `printf` kullan.
- **TUZAK (paket testi kopyalama):** `makePackageCopy` yalnız dört çekirdek dosyayı kopyalar;
  `tests/` dizininin de gelmesi gerekiyorsa `fs::copy(src, dst, recursive)` kullan.

## Paket yükleyici kuralları

- Sıra: checksums (varsa) → manifest → contract sürümü → parameters → dialog → recipe
  (`validateRecipe`) → altın testler. Herhangi biri düşerse tip KAYDEDİLMEZ; dialog kaydı
  düşerse `ComponentRegistry::unregisterType` ile tip geri alınır (mesaj "geri alindi" diyorsa
  gerçekten alınmalı — yoksa mesaj yalan söyler).
- `checksums.json` isteğe bağlı ama varsa dört zorunlu dosyayı kapsamak zorunda; uyuşmazlıkta
  dosyada CR baytı varsa CRLF dönüşümü teşhisi mesaja eklenir (en sık neden `core.autocrlf`).
- Klasör biçimi normatif; tek dosya `.mmcpkg` (ZIP) planlandı, henüz açılmadı — `.mmcpkg` bir
  DOSYA ise yükleyici "paket klasoru yok" ile reddeder. ZIP açılırsa bayt-deterministik arşiv
  şart: sabit giriş sırası, sabit zaman damgası, zip-slip reddi.
- **Temsili önizleme girdisi:** yükleyici `ComponentDialogDefinition.previewInput` alanını
  doldurur — öncelik `manifest.previewInput` (nesne), yoksa `tests/*.json` içindeki İLK geçerli
  `input` (dosya adı sıralı → deterministik). Parça seçilmeden açılan diyalog bununla çizilir;
  modelde bağlı instance varsa gerçek girdi bunu EZER. Alan public yüzeydir (başlık değişikliği
  = minor bump) ve `manifest.previewInput` nesne değilse paket reddedilir.
- Yeni paket klasörü depoya girecekse `.gitattributes` `libs/mm_contract/packages/** -text`
  kuralı şart: CRLF dönüşümü checksums'ı bozar.

## Commit ve komut hijyeni

- `git commit -m "...\`kod\`..."` → kabuk backtick'i komut yerine koyar, mesaj BOZULUR. Mesajı
  `write_file` ile dosyaya yaz, `git commit -F <dosya>` (düzeltme: `--amend -F`).
- `patch` aracında `old_string` yalnız bir imza satırı + satır sonu ise eşleşme komşu satırları
  BİRLEŞTİRİR (kodu bozar). Blok düzenlerken tam çok satırlı metni ver, sonra diff'i oku.
- Onay tarayıcısı değişkenle dolaylı çalıştırmayı (`S=path; $S/binary`) ve heredoc'lu python'u
  bloklayabilir; tam yolu yaz, python yerine `write_file` kullan.
- Kısmi commit ayırırken: `git stash push -u -m "..." -- <path>...`. Untracked içerik
  `stash@{0}^3` altındadır (düz `stash@{0}:src/dosya.cpp` "does not exist" verir).
- Madde başına ayrı commit + test özeti; GUI kabulü kullanıcıda, `main`'e merge yok.
