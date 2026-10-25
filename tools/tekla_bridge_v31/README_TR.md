# Model Maker — Tekla Bridge V3.1 (Stage 1: Legacy Adapter)

**Durum:** Ayrı çalışan, salt kaynak okuyan prototip. Model Maker kaynak kodu değiştirilmedi. GitHub push yok. Hermes/PERF-003A/Hotfix-01'e müdahale edilmedi.

## Hedef ve sürüm politikası

- Mevcut `bridge_package.cpp` okuyucusuna dokunmadan `mm.tekla.v1`, `bridge-geometry-probe-2` ve `bridge-probe-1` JSON verilerini **mevcut Bridge V3 klasör şemasına** dönüştürür.
- **Araç sürümü 3.1.0-stage1**, fakat çıktıdaki `manifest.formatVersion` **`3.0`** olmalıdır: mevcut C++ okuyucu `3.1` tanımaz.
- Tekla GUID ile Model Maker EntityId'si farklıdır. Kaynak Tekla GUID'leri korunur; ID-only probe dosyalarında `probe-id:<integer>` üretilir ve **gerçek kalıcı GUID olmadığı** özellikle raporlanır. Bunlar otomatik üye eşlemesi için kullanılmamalıdır.
- Parametreler varsa aslı korunur. Eksik Tekla parametresi, delik çapı, fitting düzlemi, kaynak geometrisi **uydurulmaz**. İthalatta editability/equivalence başarı beyanı yapılmaz.

## Kullanım (Windows PowerShell)

Python 3.10+ gerekir, harici Python paketi yok. Arşivi proje klasörü dışına açıp PowerShell'de:

```powershell
cd C:\work\model-maker-bridge-v3.1
py -3 bridge31_adapter.py fixtures\end_plate_144.json --out output\end_plate_144-v3
py -3 verify_bridge31.py output\end_plate_144-v3
py -3 -m unittest discover -s tests -v
```

Gerçek Tekla çıktı örneği:

```powershell
py -3 bridge31_adapter.py fixtures\component-5245-v2.json --out output\shear-146-v3
py -3 verify_bridge31.py output\shear-146-v3
```

`--out` hedefi **mevcut olmamalıdır**; üzerine yazmayı reddeder. Bir V1 dosyasında birden çok component varsa ayrı alt klasörler oluşturur. Var olan paketi temizleme veya silme işlemi yapılmaz.

**Model Maker tarafındaki sonraki adım:** `readBridgePackage(<output-folder>, ...)` ile mevcut Bridge V3 reader üzerinden doğrulama; ardından `mapBridgeToNative` + `resolveBridgeMemberBinding` sadece kullanıcıca onaylanmış gerçek GUID/EntityId eşlemesiyle. Bu aşama henüz burada çalıştırılmadı.

## Modül kapsamı

| Dosya | Görev |
| --- | --- |
| `bridge31_adapter.py` | Eski Tekla JSON -> V3 dosyaları, semantik kayıtlar, tanı logları, ilişkiler, sha256 |
| `verify_bridge31.py` | Yapısal alan ve checksum kontrolü (geometri testi **değil**) |
| `tests/test_bridge31.py` | 8 otomatik test: 144, 146, Haunch 40, checksum, çoklu dosya, hatalar, overwrite |
| `fixtures/` | Kullanıcıdan gelen kaynak referanslar ve sentetik 144 örneği |
| `examples/` | Oluşturulmuş örnek V3 paketleri |

## Hâlâ çözülecekler (Stage 2–4)

1. **Exporter 3.1 (C# / Tekla 2025 SP6):** API'den gerçek GUID, component'in bütün ulaşılabilir `GetAttribute` değerleri, boolean/fitting hedefleri, kaynak tipleri, bulon ekseni, hole tipi ve koordinat doğrulama metadata'sını doğrudan üretmek. `.inp` regex sonuçları tek başına doğrulama değil. Tekla GUI ve uygun .NET/SDK ile derlenip test edilmeli. Legacy probe formatlarının geri desteklenmesi sürdürülecek.
2. **V3 reader / schema:** Sadece gerçekten gerekirse yeni opsiyonel alanlar için V3.1 `formatVersion` desteği eklenmeli. Önce mevcut 3.0 okuyucuya paket okutup parse testi yapılmalı. Metadata için stabil `sourceGUID -> roleKey`, `confidence`, `units` denetlenmeli.
3. **144 native generator:** ZIP'deki sentetik `end_plate_144.cpp` mevcut `ComponentRegistry` ve `GenerationPlan` sözleşmesine *ayrı* port edilmeli; yeni OCC store, ikinci `mm::Vec3` veya ikinci `ComponentInput` alınmamalı. 144'ü gerçekten düzenlenebilir kabul etmek için gerçek Tekla benchmark seti gerekli.
4. **Doğrulama:** 146 regresyonu, 144 parametre uç durumları, bolt grid/axis/hole, weld, fitting, clash, undo/redo, MMW8, OCC-on/OCC-off ve Windows GUI testleri. `Unsupported` hiçbir zaman `Equivalent` raporlanmamalı.

## Güvenilirlik sınırı

- **Başarılı 8/8 test yalnızca dönüşüm aracının davranışıdır**; Model Maker çekirdek testleri veya Tekla API derlemesi değildir.
- 144 fixture sentetiktir, gerçek Tekla bağlantı geometrisine karşı karşılaştırma değildir.
- 146 probe'da component parametreleri yok; 0 parametre aktarılır. 6 bulon verisi korunur ama delik ekseni doğrulanmadan kesme yapılmaz.
- Haunch 40 eski probe'daki koordinatlar global değil/kanıtlanmamış olabilir. `unverified` etiketlidir; native üretim için uygun değildir.
- `geometry/*.json` diagnostic/contour/bbox alanları tam B-Rep değildir; yalnızca referans metadata'sıdır.

## Model Maker'a bağlamadan önce teslim kriteri

- Mevcut C++ `readBridgePackage` ile 144/146 örneklerinin parse edilmesi ve checksum onayı.
- Tekla 2025 SP6 exporter gerçek seçimleri için GUID'lerin kalıcı ve tekil olduğunun doğrulanması.
- 146 native eşlemesiyle kaynak parametre provenance denetimi; belirsiz değerlerin `Unverified` kalması.
- 144 için en az 3 gerçek bağlantı örneği ve geometri doğrulaması.
