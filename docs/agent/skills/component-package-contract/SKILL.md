---
name: component-package-contract
description: "Use when authoring or validating component packages."
version: 1.0.0
author: Hermes Agent
license: MIT
platforms: [linux, macos, windows]
metadata:
  hermes:
    tags: [contract, package, golden-test, digest, json, semver, component]
    related_skills: [model-maker, cpp-layered-library-split, git-worktree-publish]
prerequisites:
  commands: [cmake, ctest, g++]
---

# Component package + shared contract (paket, altın test, sürüm)

## When to Use

- Bir komponent PAKETİ (manifest + parameters + dialog + recipe + tests) yazıyor, yüklüyor veya
  doğruluyorsan; iki uygulama (üretici + Studio/tüketici) aynı JSON sözleşmesini paylaşıyorsa.
- Bir "altın test" (girdi + beklenen digest) ekliyor veya yükleyiciye digest replay bağlıyorsan.
- Ortak sözleşmeye public başlık/alan ekleyip sürüm (semver) artırıyorsan.

Repo haritası, branch/worktree ve derleme yolları için `model-maker` skill'i; buradaki kurallar
katman davranışıyla ilgilidir.

## 1. Bağımsız contract döngüsü (en hızlı geri bildirim)

```
cmake -S <repo>/libs/mm_contract -B <repo>/build-contract
cmake --build build-contract -j 3
cd build-contract && ctest --output-on-failure
```

- Test exe'leri `mm_contract_*`; ana ağaçtakiler `model_maker_*`. İsimleri karıştırma.
- Ana ağaç contract testlerini KOŞMAZ. Contract değişikliğinde ikisini de doğrula: bağımsız
  contract ctest + ana suite (aynı kütüphane ana ağaca da linklenir).
- Yeni test hedefi eklerken bayat build dizini sessizce atlar: `grep -c "<yeni>.cpp" build-contract/build.ninja`
  ile doğrula, 0 ise `cmake -S libs/mm_contract -B build-contract` ile yeniden configure et.

## 2. Prob ile ölç (kod okumak yerine)

Plan/digest gibi değerleri küçük bir programla bastır, tahmin etme:

```
g++ -std=c++20 -O0 -Iinclude -Ilibs/mm_contract/include -Ithird_party probe.cpp \
  src/geometry.cpp src/sha256.cpp build-contract/libmm_component_contract.a -o probe
```

- `geometry.cpp` (Vec3 operatorleri) ve `sha256.cpp` arşivde YOKTUR; kaynak olarak ekle, yoksa
  `undefined reference to mm::Vec3::operator-` / `sha256Hex` alırsın.
- Geniş karakter basarken tek akışta `wprintf` + `%ls` kullan ve `setlocale(LC_ALL, "C.UTF-8")` çağır;
  `printf`/`wprintf` karışımı satırları kaybeder.
- Çalıştırıcı yolu LİTERAL tam yol olsun; değişkenle çağırmak onay kapısına takılır.

## 3. Paket yapısı ve altın test biçimi

Klasör: `manifest.json`, `parameters.json`, `dialog.json`, `recipe.json`, `checksums.json`,
`tests/*.json` (opsiyonel).

```json
{ "name": "varsayilan",
  "input": { "main": {...}, "secondary": [...], "params": [...] },
  "expect": { "planDigest": "<sha256 hex>" } }
```

- `input` = üretim girdisinin JSON'u; alan adları struct alanlarıyla aynı olsun (`id`, eksenler,
  kesit b/lik/tw/tf, anchor ofseti, `profileRotation`, `params[] = {name, value:{type,value}}`).
- Yükleyici `input` görürse recipe'yi o girdiyle KOŞAR ve `generationPlanDigest` ile
  `expect.planDigest` karşılaştırır; uyuşmazlık/üretim hatası → paket REDDEDİLİR. `input` yoksa
  yalnız yapısal doğrulama yapılır ve bu durum warnings'e yazılır.
- **Digest'i ELLE yazma**: probla üret, sonra aynı değeri hem paket testine hem üreticinin golden'ına
  karşı kontrol et. İkisi ayrışıyorsa sözleşme eşdeğerliği bozulmuştur — testi "güncelle" deme,
  kök nedeni ara.
- Negatif vakayı da kilitle: digest'i bozulmuş bir kopya REDDEDİLMELİ ("digest TUTMADI").
  Paket kopyalama yardımcısı yalnız çekirdek dosyaları kopyalıyorsa `tests/` sessizce kopyalanmaz ve
  altın test hiç koşmaz — kopyalamayı dizin dahil yap (`std::filesystem::copy(..., recursive)`).
- `checksums.json` çekirdek dosyaları kapsar; tests/ eklemek checksum güncellemesi gerektirmez. Ama
  manifest/dialog `typeId` değişirse checksum'lar yeniden üretilmelidir, yoksa paket reddedilir.
- Vaka seçerken farkın GERÇEKTEN oluştuğunu doğrula: kare kesitte (b == h) `profileRotation = 90`
  hiçbir şeyi değiştirmez ve "dönüş testi" sessizce anlamsızlaşır. Asimetrik kesit kullan.

## 4. Sürüm (semver) disiplini

- Public başlık/alan eklemek = MINOR: `kContractVersionMinor` ve `kContractVersion` string'i birlikte artar.
- Sürüm artışı pinleri kırar: literal sürüm karşılaştıran testler (recipe/codec/smoke) ve
  `contractSupports(1,N)` / `!(1,N+1)` iddiası. Hepsi AYNI commit'te güncellenmeli, yoksa suite kırmızı.
- Paketlerin `manifest.json` içindeki `minContractVersion` geriye dönük uyumluluk beyanıdır; sürüm
  artışında otomatik yükseltme — gerçekten gerekiyorsa artır.
- Yeni public başlık eklerken kuralları (görsel sabitler, çerçeve kuralları) belgeye de yaz; kod ve
  sözleşme belgesi aynı commit'te gitsin.

## 5. Recipe/DSL parametre bağlama kuralları

- Sayısal argüman ifade değerlendiricisinden, metin argümanı `param.<ad>` çözücüsünden, mantıksal
  argüman bool çözücüsünden geçmeli. Metin alanlarını ham string okuyucuyla okumak `param.x`
  metnini LİTERAL değer yapar (levha malzemesi "param.plateMaterial" olur) — sessiz yanlış.
- `param.<ad>` girdide yoksa ŞEMA varsayılanına (default string / ilk choice) düş; golden girdiler
  parametresiz gelir, bu dal şarttır. Tanımsız parametre → fail-closed + Türkçe mesaj (sessiz false yok).
- Metin çözücü hata durumunda çağıranın hata alanını doldursun; `false` dönüp genel mesaja düşmek
  kullanıcıya "neden" göstermez.

## 6. Geometri/çerçeve kuralları (paket yazarken)

- Üye kesit çerçevesi `profileRotation`'ı UYGULAMALI (`memberSectionFrame` eşdeğeri). Dönüşü yok sayan
  yol (yalnız eksen + refNormal) dönük üyede genişlik/yükseklik yer değiştirmez ve levha/bulonlar
  100+ mm kayar. Dönüş 0 iken iki yol birebir aynıdır → golden digest değişmez; değişirse yerleşim bozulmuştur.
- Levha: genişlik `u`, yükseklik `v`, kalınlık `axis` boyunca SİMETRİK (`center ± t/2`, extrüzyon `+axis`);
  köşe numaraları materyalizer nokta sırasına göre `0=(-u,-v) 1=(+u,-v) 2=(+u,+v) 3=(-u,+v)`.
  El yönü serbest (`u × v = +axis` zorunlu değil); çerçeve "düzeltilmez". Testte sağ VE sol elli
  levhayı kur; pah köşesinin doğru köşeyi kaldırdığını, kalınlık aralığının iki elde aynı olduğunu doğrula.
- Görsel sabit (ör. levha grisi) sözleşmede TEK sabit olarak yaşar; uygulama katmanı ona bağlanır,
  ikinci bir sabit tutmaz.

## Pitfalls

- **Fail-closed reddetme mesajı TANI vermeli.** "Temas yüzeyi belirsiz/geçersiz" gibi tek cümle
  kullanıcıya neden ve ne yapacağını söylemez. Mesajda: (a) durum kodu/adı, (b) denenen nokta ve
  ADAY yüzler + ölçüler (ör. X−/X+/Y−/Y+/Z−/Z+ ve uzaklıkları), (c) en yakın adayın yorumu
  ("uç kolonun tepesine oturuyor, yan başlığa temas yok"), (d) somut öneri ("köşe düğümü için
  <bileşen> kullanın"). Türkçe karakterler düzgün yazılır; ASCII'ye kaçmak yarım metin üretir.
  Reddetme DAVRANIŞI değişmez — yalnız tanı okunur olur; geometri değişmediği için test seti aynı kalır.

- **Yükleyicide doğrulama sırası bozulmaz**: checksums → manifest → contract sürümü → parameters →
  dialog şeması → recipe statik analizi → altın testler → tip çakışması. Herhangi biri düşerse tip
  KAYDEDİLMEZ (kısmi kayıt yok).
- **Altın testi "yapısal doğrulandı" diye bırakma.** Girdi taşıyan testte digest karşılaştırması yoksa
  test hiçbir şeyi kilitlemez; replay'i yükleyiciye bağla.
- **Digest'e raporlama alanı sokma.** Kontrol sonuçları yalnız raporlanır; özet yalnız geometriden
  hesaplanır. Aksi halde her yeni kontrol tüm golden'ları kırar.
- **Paket içi metin dosyalarında CRLF**: checksum bayt üzerinden hesaplanır; `core.autocrlf`
  paketi sessizce bozar — `.gitattributes` ile paket dizinini `-text` sabitle.
