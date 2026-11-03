# MM-7 — Bulon/üye çakışma ve EC3 minimum kenar/aralık kontrolü

Durum: tamamlandı ve push edildi (3b72375 → 9699817).
Kod: `include/model_maker/component_checks.hpp`, `src/component_checks.cpp`,
`src/end_plate.cpp`, `src/recipe.cpp` (`blockEndPlatePlacement`),
`src/component_engine.cpp` (`generateEndPlate`).

## 1. Seçilen yerleşim: "extended" (varsayılan), flush geçerli ama varsayılan değil

Varsayılan girdi (End Plate, `beam_to_column_end_plate`):

| Parametre | Varsayılan | Anlamı |
|---|---|---|
| `plateExtra` | 20 mm | Levha, kiriş kesit bbox'ından her yönde 20 mm taşar |
| `boltEdge` | 35 mm | Bulon merkezinin **kesit** kenarından uzaklığı |
| `boltCountX/Y` | 2 / 2 | Yatay flanş gauge'ı, dikey kesit yüksekliği |
| `boltDiameter` / `holeDiameter` | 20 / 22 mm | M20, normal boşluk |

**Gerekçe (neden extended):**
- Tekla 144 alışkanlığı: alın levhası kesitin üstüne/altına taşar; kaynak kökü
  flanş dışında kalır, levha köşesi kaynakla çakışmaz.
- `plateExtra = 0` (flush) yasak değil; sadece EC3 kenar mesafesini sağlamak
  zorlaşır (levha yüksekliği = kesit yüksekliği olur, bulonlar flanş hizasına
  yaklaşır). Bu durumda kontrol FAIL verir ve üretici fail-closed davranır —
  yani flush "seçilebilir ama sessizce geçmez".

**MM-7'de değişen yerleşim kuralı (19352a1):** dikey bulon aralığı artık
**kesit yüksekliğinden** (`secHeight`) türer, levha yüksekliğinden değil —
yatay gauge'ın `secWidth`'ten türemesiyle simetrik. Eski davranış (levha
yüksekliğinden türetme) taşan levhada (`plateExtra > 0`) bulonları başlığın
içine itiyordu; IPE300'de delik doğrudan başlıkta oluşuyordu. `boltEdge`
30 → 35 mm: M20 için 22 mm delik + 10 mm kaynak payı ⇒ kesit kenarından
~21 mm net mesafe şartı; Haunch 40 varsayılanıyla aynı.

## 2. `checks[]` yapısı

`GenerationPlan::checks` — raporlanabilir alan (geometri değil):

```cpp
struct ComponentCheck {
    std::string  id;      // "bolt_flange_clash" | "bolt_web_clash" |
                          // "edge_distance" | "pitch_p1" | "pitch_p2"
    bool         pass;    // false => çağıran plan.ok=false + Türkçe mesaj
    double       value;   // ölçülen (mm)
    double       limit;   // EC3/şartname sınırı (mm)
    std::wstring message; // Türkçe, hangi bulon/üye/mm
};
```

- Üretim: `runComponentChecks(plan, input, opts)`; `opts` = `{weldAllowance=10,
  minEdgeRatio=1.2, minPitch1Ratio=2.2, minPitch2Ratio=2.4}`.
- Uygulama: `applyChecksToPlan()` — hem üretici (`generateEndPlate`) hem recipe
  taşı (`blockEndPlatePlacement`) **aynı** fonksiyonu çağırır (gen == rec).
- Kural: delik (d0 + 10 mm kaynak payı) kiriş başlık/gövde izdüşümüyle
  çakışmamalı; `e1/e2 ≥ 1.2·d0`; `p1 ≥ 2.2·d0`; `p2 ≥ 2.4·d0`.
- `tw`/`tf` bilinmeyen üyelerde çakışma kontrolü **atlanır** (bilinmeden
  yanlış ret yok).
- MMW8 kodeği: `checks` JSON'a yazılır (boşsa yazılmaz), **digest'e girmez**
  (digest yalnız geometri). `contractVersion` 1.4.0 → **1.5.0**.

## 3. Varsayılan girdide sonuç (ölçülmüş)

Girdi: kolon 300×300 (tw 11 / tf 19), kiriş 200×400 (tw 8,6 / tf 13,5),
tüm parametreler varsayılan. Üretilen: levha 240 × 440 × 12, bulon 2×2,
sX = 100, sY = 330, d = 20 / delik 22.

| Kontrol | Sonuç | Ölçülen | Sınır |
|---|---|---|---|
| `bolt_flange_clash` | PASS | 21,5 mm | 21,0 (d0/2 + 10) |
| `edge_distance` | PASS | 55,0 mm | 26,4 (1,2·d0) |
| `pitch_p1` | PASS | 330,0 mm | 48,4 (2,2·d0) |
| `pitch_p2` | PASS | 100,0 mm | 52,8 (2,4·d0) |

`plan.ok = true`, `plan.checks` 4 kayıt. Kırmızıya düşen örnek: `boltEdge = 25`
ile `bolt_flange_clash` 10,5 < 21,0 → `plan.ok=false` + Türkçe hata; testte
fail-closed olarak sabitlendi.

## 4. Golden önce / sonra

`tests/golden/end_plate_digests.json` (bilinçli değişim, 8e1fa17):

| Anahtar | Önce | Sonra |
|---|---|---|
| `endplate_beam_px` | `394675658cd0bd8d223d7ce54c5ceca9f0fdd28ee308fbe4f3a647b1e018bfa2` | `0540df88bd72d33a32639192e5c1bd096ae315992b6d9853ea877035c94d262b` |
| `endplate_beam_nx` | `8c4fdf4e39190ee0899ca23e4c3fb573553c94fcf5866b884bd5c7ed0f707278` | `2efec47957e8c4c510a5da4255c9cd880922b721c3a9f0f9d2d9db4954abfedd` |
| `endplate_beam_py` | `c81de6f0186371a3d3907078f599c49b087384d13a79c4e6a838f96ab459e99d` | `beb88b6c1575ea464a70c1b9a9ff41675bd7d7334cbae14b299191295cea77a8` |
| `endplate_rotated_90` | `f1f64d50e169341f8511e3e30c01eacb387ebc39827eb88a357050ade80e9693` | `df7c718e204a2a6c9f7fba913398fa9149a9358c65756dd4a0882f0864ffec99` |
| `endplate_diff_bolts` | `be56bd5c8b97e6c425771437cd10540b0dceb15e8677f2b8cde7929f55faee66` | `7a65b7b40dfc21646792159582ec734d3d6930bf4d61bacfe84133f3e45025f8` |

Neden değişti: `boltEdge` 30 → 35 ve dikey aralığın `secHeight`'ten türemesi.
`endplate_diff_bolts` ayrıca `boltEdge = 40` kullanır. Değişmeyen senaryolar
golden'da aynı kaldı.

## 5. Haunch 40'ta aynı kontrol var mı? — **Hayır**

`applyChecksToPlan` bugün yalnız iki yerden çağrılıyor:
`generateEndPlate` (component_engine.cpp) ve recipe'de
`blockEndPlatePlacement` (recipe.cpp). Haunch 40 blokları
(`blockHaunchContact/PlumbFace/EndPlate/Web/Flange/Stiffeners/Bolts/Welds`)
kontrol üretmiyor, yani **Haunch 40 bulonları çakışma/EC3 denetiminden
geçmiyor**; `plan.checks` boş kalıyor.

Bu bir boşluk, hata değil: Haunch 40 yerleşimi `boltEdge = 35` ile zaten
varsayılan olarak geçerli (End Plate ile aynı varsayılan). Kapatmak için
önerilen iş kalemi (ayrı commit): `blockHaunchBolts` sonunda
`applyChecksToPlan(plan, ctx.input)` çağırmak. Risk: Haunch 40'ta bulonlar
levha + kiriş flanşı üzerinden geçtiği için çakışma kuralı (delik kiriş
başlık/gövde izdüşümüyle çakışmamalı) burada **yanlış pozitif** üretebilir;
önce kural setinin Haunch geometrisine uyarlanması (hangi üye ikincil, hangi
levha hedef) gerekir. Bu yüzden ayrı ele alınmalı.

## 6. Testler

| Test | Kapsam | Sonuç |
|---|---|---|
| `mm_contract_component_checks_test` | 18 kontrol: fail-closed, kenar/aralık, bilinmeyen kesit, JSON round-trip, digest etkisizliği | PASS |
| `model_maker_end_plate_test` | golden digest'ler + varsayılan yerleşim + M24/26 mm delik eşdeğerliği | PASS |
| `model_maker_component_engine_test` | üretici ↔ recipe eşdeğerliği (`gen == rec`) | PASS |

Doğrulama komutu (Linux, OCCT'li ağaç):

```
cmake --build build-occ-hermes -j 4
cd build-occ-hermes && ctest -j 4 --output-on-failure
```
