# Recipe dili (MM-3) — şema ve semantik

Bu belge, bir component paketinin `recipe.json` içindeki bildirimsel üretim
tarifinin şemasını ve yorumlayıcının davranışını tanımlar. Yorumlayıcı
`runRecipe(recipe, specs, input) -> GenerationPlan` fonksiyonudur
(`include/model_maker/recipe.hpp`).

## İlkeler

- Bildirimsel, deterministik, yan etkisiz. Dosya/ağ/rastgelelik/saat erişimi yok.
- Her blok bir `roleKey` üretir (`[A-Za-z0-9_]`, paket içinde tekil; `main` /
  `secondary_N` rezerve, kullanılamaz).
- Koordinatlar her zaman bir çerçeveye (frame) göre yereldir; dünya koordinatı
  yazılamaz.
- Hata → `GenerationPlan.ok = false` + Türkçe mesaj; belge değişmez (fail-closed).
- Döngü yok.

## JSON yapısı

```json
{
  "recipeVersion": "1.0.0",
  "let": {
    "W": "max(secondary_0.b + 20, upperSpanX + param.boltEdgeLeft)",
    "upperSpanX": "(param.upperBoltCountX - 1) * param.upperBoltPitchX"
  },
  "steps": [
    { "id": "c", "use": "frame.member_end", "args": { "member": "main", "end": "end" } },
    { "id": "f", "use": "frame.offset", "args": { "frame": "c", "dw": "param.gap" } },
    { "use": "plate.rect", "role": "end_plate",
      "args": { "frame": "f", "w": "W", "h": "param.endPlateHeight", "t": "param.endPlateThickness", "material": "param.plateMaterial" } },
    { "use": "bolts.grid", "role": "upper_bolts",
      "args": { "frame": "f", "nx": "param.upperBoltCountX", "ny": 1, "px": "param.upperBoltPitchX", "d": 20, "hole": 22, "through": ["main"] } },
    { "check": "param.haunchDepth > param.haunchTipDepth + 25",
      "message": "Haunch derinligi uc derinliginden en az 25 mm buyuk olmali" }
  ]
}
```

- `let`: ad → ifade, sıralı; ifade içinde diğer let'lere ve `param.*`/üye
  alanlarına başvurabilir (döngü yasak).
- `steps`: sıralı adımlar. İki tür:
  - **Blok adımı** (`use` dolu): `id` (çıktı bağlama adı), `args`, opsiyonel
    `role` (üretilen nesnenin roleKey'i), opsiyonel `when` (koşul, 0 ise atlanır).
  - **Check adımı** (`check` dolu): koşul sağlanmazsa plan `ok=false` olur,
    `message` (Türkçe) hataya yazılır.

## İfade dili

```text
sayılar, + - * / ^, ( ), min max abs sqrt sin cos tan atan2 clamp,
if(kosul, a, b), < <= > >= == !=, and/or/not (&& || !)
param.<ad>                       parametre değeri (double)
main.<alan> / secondary_N.<alan>  üye alanı: h, b, tw, tf, length
```

- `^` sağ-birleşik (üs); `if` üç argüman, koşul sıfırdan farklıysa `a` değilse `b`.
- Trig fonksiyonları **derece** alır; `atan2(y, x)` **derece** döndürür
  (Angle parametreleriyle tutarlı).
- Karşılaştırma ve mantık 1.0 / 0.0 döndürür.
- `main.h` = kesit yüksekliği, `main.b` = genişlik, `main.tw` = gövde kalınlığı,
  `main.tf` = flanş kalınlığı, `main.length` = eksen uzunluğu (mm).

## Çerçeveler (frame)

Bir çerçeve `MemberLocalFrame`'tir: `origin` + ortonormal `u`/`v`/`w`. Bloklar
çerçeve üretir ve tüketir; levha/bulon/kesim hep bir çerçeveye göre yerleşir.

## v0 taşları (blok kataloğu)

| Taş | Argümanlar | Çıktı |
|---|---|---|
| `frame.member_end` | `member` (main/secondary_N), `end` (start/end) | üye uç/baş çerçevesi (origin uçta, w üye ekseni, u/v kesit genişlik/yükseklik) |
| `frame.offset` | `frame`, `du`, `dv`, `dw` | yerel eksenlerde ötelenmiş çerçeve |
| `plate.rect` | `frame`, `w`, `h`, `t`, `name`, `material`, `finish`, `posNo` | `PlateSpec` (center=origin, u/v/axis=frame, genişlik/yükseklik/kalınlık) |
| `bolts.grid` | `frame`, `nx`, `ny`, `px`, `py`, `d`, `hole`, `grade`, `through[]` | `BoltGroup` (axis=frame.w, xDir=frame.u; through = üye rolleri → parts) |
| `weld.fillet` | `roleA`, `roleB`, `size`, `double` | `WeldSpec` (üye rolü → part EntityId; levha roleKey'i → partARole/partBRole) |
| `cut.plane` | `target` (üye rolü), `frame`, `keep` (positive/negative) | `CutModifier::PlaneCut` (hidden=true, component iç elemanı) |
| `endplate.placement` | `main`, `beam`, `rolePlate`, `roleBolts` | mevcut `computeEndPlatePlacement`'ı SARAR; bir `PlateSpec` (rolePlate) + bir `BoltGroup` (roleBolts) üretir. Parametreleri `generateEndPlate` ile birebir aynı şekilde `input.params`'tan okur → planDigest `generateEndPlate` ile özdeş |

Not: `bolts.grid`'in `through` listesi yalnız üye rolleri (main/secondary_N)
içerir; levhalar materialization'da parts listesinin başına eklenir (mevcut
sahiplik kuralı). `cut.plane` hedefi de yalnız üye rolü kabul eder (v0).

## Statik analiz (yükleme anında, fail-closed)

`validateRecipe` aşağıdakileri denetler; ihlalde Türkçe hata döner:
- Bilinmeyen blok / bilinmeyen fonksiyon.
- Bilinmeyen parametre (`param.X` şemada yok).
- Tanımsız let / döngüsel let.
- roleKey formatı + tekilliği + tür tutarlılığı (aynı roleKey farklı türe
  bağlanamaz) + rezerve role reddi.
- Üst sınırlar: adım ≤ 256, ifade derinliği ≤ 32 (çalışma anında ayrıca:
  grup başına bulon ≤ 64, toplam bulon ≤ 512).

## Hata davranışı

Herhangi bir adım başarısız olursa `runRecipe` `GenerationPlan{ok=false,
error=<Türkçe mesaj>}` döndürür; çağıran belgeyi değiştirmez. Yorumlayıcı
Studio ile Model Maker arasında aynı commit'ten derlenir ve aynı girdi + aynı
recipe için birebir aynı plan (dolayısıyla aynı `planDigest`) üretir.
