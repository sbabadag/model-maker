#pragma once

// GT-1 — GUI TEST MODU (INBOX 6): `model-maker.exe --test-script <senaryo.json>
// --out <klasor>` ile tekrarlanabilir, ekran-kapali da calisan GUI senaryolari.
//
// Senaryo JSON: { "backend": "auto|gdi|gpu", "timeoutMs": 60000,
//                 "steps": [ ... ] }
// Adimlar:
//   {"cmd": "draw.line"}          komut id (kayit uzerinden, serit yolu)
//   {"click": [x, y]}             aktif tuvale sol tik (client koordinati)
//   {"drag": [x1, y1, x2, y2]}    aktif tuvale surukle
//   {"key": "esc"|"return"|"tab"|"delete"|"F5"|tek harf}  tusa basilir
//   {"type": "metin"}             odakli kontrole metin yaz (WM_CHAR)
//   {"wait": 500}                 ms bekle
//   {"open": "model.mmd"}         belge ac
//   {"profile": "HEB240"}         panel aktif profilini ayarla (test kestirmesi)
//   {"search": "haunch"}          arama kutusuna metin koy (deterministik)
//   {"clickModel": {"model": 0}}  modelin koselerinin ekran agirlik merkezine
//                                 tiklar (izdusum uzerinden, vision'siz);
//                                 "vertex": i -> tek kose, "vertex": [i,j] ->
//                                 orta nokta
//   {"capture": "final"}          <out>/final.png: en ustteki ModelMakerCanvas
//                                 (yeni gorunum tuvalleri dahil) tam kare
//   {"capture": {"name": "final", "target": "window"}}
//                                 tum ana pencere (MDI cercevesi + alt
//                                 pencereler + ribbon; PrintWindow ile)
//   {"expect": {"models": 2, "selection": 1, "viewports": 1,
//               "activeTool": "draw.line", "backend": "gdi"}}
//                                 beklenti; tutmazsa cikis kodu 1
//                                 "models" SAYI (toplam) ya da NESNE olabilir:
//                                 {count, profiled, plain, face3d, point,
//                                  construction, visible} (alt tip sayilari)
//   {"expect": {"visibleCount": 1}}  gorunur model sayisi
//   {"expect": {"status": {"contains": "açıldı"}}}  durum cubugu alt dizgesi
//   {"expect": {"quickSearch": {"text": "haunch", "has": "Haunch (40)"}}}
//                                 arama kutusu metni + oneri listesi (B1)
//   {"expect": {"catalog": {"minCount": 4, "has": "Haunch (40)"}}}
//                                 Katalog acilir icerigi (B1; "has" alt dizge)
//
// H-8 (kot -> bagli bilesen zinciri) uygulama-katmani op'lari — panel ENTER/
// UYGULA'nin cagirdigi AYNI public API'ler (kablolama Python testlerinde):
//   {"appColumnLevels": {"top": 7200, "bottom": 0}}
//                                 applyColumnLevels (secili kolona uygular,
//                                 secim yoksa sonraki yerlesim icin saklar)
//   {"placeColumn": {"x": 0, "y": 0, "z": 0}}  bekleyen profil+dotlarla kolon
//   {"assignLine": {"x1":..,"y1":..,"z1":..,"x2":..,"y2":..,"z2":..,
//                   "profile": "IPE300"}}      egik uye (assignProfileToLine)
//   {"select": [0, 1]}           programatik secim (indeksler)
//   {"assignProfile": {"model": 1, "profile": "IPE400"}}
//                                 secili uyenin profilini degistir (UYGULA
//                                 profil satiri; bagli component recompute dahil)
//   {"appComponent": {"typeId": "tekla_haunch_40", "parts": [0, 1]}}
//                                 generic uretim yoluyla component kur
//   {"save": "t9.mmd"}           outDir'e kaydet (diyalog yok)
// H-8 expect'leri:
//   {"expect": {"modelAxis": {"model": 0, "fromZ": 0, "toZ": 7200, "tol": 5}}}
//                                 eksen uc noktalari (verilen alanlar kontrol)
//   {"expect": {"modelProfile": {"model": 1, "name": "IPE400"}}}
//   {"expect": {"componentCount": {"typeId": "tekla_haunch_40", "count": 1}}}
//   {"expect": {"componentAttached": {"typeId": "tekla_haunch_40",
//                                     "maxGap": 500}}}
//                                 main/secondary eksen uclari boslugu < maxGap
//                                 ("kolon mertekten ayrik" kalkani)
//   {"expect": {"componentDigest": {"typeId": "tekla_haunch_40",
//                                   "key": "h1", "remember": true}}}
//                                 geometri digest'i hafizala; sonra "equals"
//                                 (kaydet-ac korunur) ya da "notEquals"
//                                 (recompute gercekten calisti) ile karsilastir
//
// HER adimdan sonra: <out>/step_%03d.png (tuval ekran goruntusu) +
// <out>/step_%03d.json (durum: secim sayisi/tipi, aktif komut, nesne
// sayilari/tipleri, gorunur cizim listesi, hizli arama sonuclari, katalog
// menu ogeleri, kamera). Sonda <out>/result.json {ok, steps, error}.
// Bitince uygulama KENDILIGINDEN kapanir: QCoreApplication::exit(0/1).

#include <QString>

namespace mm {

class QtMainWindow;

// Senaryoyu calistirir; bitince (ya da hata/zaman asiminda) sureci kapatir.
// Donus degeri QCoreApplication::exit'a gider: 0 basari, 1 hata.
int runGuiTestScript(QtMainWindow& window, const QString& scriptPath,
                     const QString& outDir);

} // namespace mm
