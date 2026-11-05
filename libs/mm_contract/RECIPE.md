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

Salt okunur erişim: `runRecipeFrames(recipe, specs, input)` generic çerçeve
üreten her adımın çerçevesini `adım id → {origin, u, v, w}` olarak döndürür
(`recipe.hpp`); plan ve digest DEĞİŞMEZ (runRecipe ile aynı çekirdek). Haunch
opak çerçeveleri (frame.plumb_face, haunch.*) bu API'de YER ALMAZ.

## v0 taşları (blok kataloğu)

| Taş | Argümanlar | Çıktı |
|---|---|---|
| `frame.member_end` | `member` (main/secondary_N), `end` (start/end) | üye uç/baş çerçevesi (origin uçta, w üye ekseni, u/v kesit genişlik/yükseklik — `profileRotation` DAHİL, `memberSectionFrame`) |
| `frame.offset` | `frame`, `du`, `dv`, `dw` | yerel eksenlerde ötelenmiş çerçeve (eksenler değişmez) |
| `frame.rotate` | `frame`, `about` (`u`/`v`/`w`, varsayılan `w`), `deg` | çerçeveyi kendi ekseni etrafında döndürür. `u/v/w` ortonormal kalır, el yönü KORUNUR (sol elli çerçeve sağa çevrilmez). `deg=0` kimliktir (digest değişmez). |
| `frame.member_face` | `member` (main/secondary_N, varsayılan main), `toward` (main/secondary_N, varsayılan secondary_0) | `member`'ın `toward`'a **bakan yüzünün** çerçevesi. `w` = yüzün dış normali (toward'a bakar), `u/v` kesit genişlik/yükseklik + üye ekseninden deterministik; `origin` seçilen yüz düzleminde (kesit merkez ofseti + flanş dış yüzü `h/2` / gövde yüzü `tw/2` dahil, toward ucunun eksene izdüşümü hizasında). Yüz kümesi yalnız bağlantı yüzleridir: flanş dış yüzü (±height) ve gövde (±width); flanş KENARI değildir. `toward` eksenle aynı doğrultuda ya da iki yüze eşitse (~45°) AÇIK ret. Kanonik MemberGeometry/digest kurallarına DOKUNMAZ. |
| `plate.rect` | `frame`, `w`, `h`, `t`, `name`, `material`, `finish`, `posNo`, `chamfers[]` | `PlateSpec` (center=origin, u/v/axis=frame, genişlik/yükseklik/kalınlık). `chamfers` = `"corner:type:h:v"` listesi (sayısal alanlar İFADE olabilir; corner 0..3, type 1..3). Bacaklar KIRPILMAZ: kenarı aşan ya da komşusuyla çakışan pah REDDEDİLİR. |
| `plate.polygon` | `frame`, `t`, `outline[]`, `name`, `material`, `finish`, `posNo` | serbest kontur levhası: `outline` = `"u,v"` listesi (ifade olabilir), levha MERKEZİNE göre yerel koordinat. En az 3 nokta; içbükey ya da kendini kesen kontur REDDEDİLİR (yalnız dışbükey ve basit). |
| `bolts.grid` | `frame`, `nx`, `ny`, `px`, `py`, `d`, `hole`, `grip`, `holeDepth`, `grade`, `through[]` | `BoltGroup` (axis=frame.w, xDir=frame.u; through = üye rolleri → parts). `grip` verilmezse VARSAYILAN hesaplanır: through üyelerinin delinen kalınlıkları toplamı (bulon ekseni kesit YÜKSEKLİĞİNE hizalıysa flanş tf, GENİŞLİĞİNE hizalıysa gövde tw). `holeDepth` verilmezse `grip + 2` (end plate kuralı). Açık verilen değer dokunulmaz; verilirse plana ve digest'e girer (ankraj bulonu). |
| `weld.fillet` | `roleA`, `roleB`, `size`, `double` | `WeldSpec` (üye rolü → part EntityId; levha roleKey'i → partARole/partBRole) |
| `cut.plane` | `target` (üye rolü), `frame`, `keep` (positive/negative) | `CutModifier::PlaneCut` (hidden=true, component iç elemanı) |
| `endplate.placement` | `main`, `beam`, `rolePlate`, `roleBolts` | mevcut `computeEndPlatePlacement`'ı SARAR; bir `PlateSpec` (rolePlate) + bir `BoltGroup` (roleBolts) üretir. Parametreleri `generateEndPlate` ile birebir aynı şekilde `input.params`'tan okur → planDigest `generateEndPlate` ile özdeş |

Metin argümanları (`name`, `material`, `finish`, `posNo`, `grade`,
Haunch `plateMaterial`): değer `param.<ad>` ise girdi parametresinden (yoksa
şema varsayılanından) çözülür; `param.` öneki yoksa LİTERAL metindir.

Not: `bolts.grid`'in `through` listesi yalnız üye rolleri (main/secondary_N)
içerir; levhalar materialization'da parts listesinin başına eklenir (mevcut
sahiplik kuralı). `cut.plane` hedefi de yalnız üye rolü kabul eder (v0).

## Statik analiz (yükleme anında, fail-closed)

`validateRecipe` aşağıdakileri denetler; ihlalde Türkçe hata döner:
- Bilinmeyen blok / bilinmeyen fonksiyon.
- Bilinmeyen parametre (`param.X` şemada yok) — sayısal argümanlarda ve
  metin argümanlarında (`name`, `material`, `finish`, `posNo`, `grade`,
  `plateMaterial`). Metin argümanı metin olmayan bir parametreye bağlanırsa
  "metin parametresi degil" hatası verilir (HR-7).
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

## Haunch 40 blokları (MM-3c)

`packages/haunch40/recipe.json` 8 adım kullanır. Tüm parametreler
`frame.contact` argümanlarında AÇIKÇA listelenir (input.params'a gizli okuma
yok); adımlar opak bir durum (`state`) paylaşır:

| Blok | Görev |
|---|---|
| `frame.contact` | temas çerçevesi + eğik çerçeve + levha boyutu + başlangıç |
| `frame.plumb_face` | düşey yüz doğrulaması (contact ile hazır) |
| `haunch.end_plate` | alın levhası (+ kolon tepesi kırpma) |
| `haunch.web` | konik gövde konturu |
| `haunch.flange` | basınç flanşı |
| `haunch.stiffeners` | kapak + kiriş/kolon berkitmeleri |
| `bolts.haunch_groups` | iki bulon grubu |
| `haunch.welds` | kaynaklar |

`frame.contact` argümanları: `haunchLength, haunchDepth, haunchTipDepth,
haunchWebThickness, haunchFlangeThickness, endPlateThickness, gap,
boltEdgeLeft, boltEdgeRight, boltEdge, endPlateWidth, endPlateHeight,
haunchFlangeWidth, topPlateThickness, stiffenerClipSize, stiffenerThickness,
addStiffenerThickness, rafterBottomStiffenerThickness, comprStiffenerThickness,
morrisStiffenerThickness, diagStiffenerThickness, diagStiffenerWidth, weldSize,
createBeamStiffener, createAddStiffener, createRafterBottomStiffener,
createComprStiffener, createMorrisStiffener, createDiagStiffener, createWelds,
upperBolt*, lowerBolt* (CountX/CountY/PitchX/PitchY/Diameter/HoleDiameter/
EdgeClear)`, metin: `plateMaterial, grade`. Aritmetik `generateHaunch40` ile
birebir; 5 golden girdide `digest(runRecipe) == golden`.
