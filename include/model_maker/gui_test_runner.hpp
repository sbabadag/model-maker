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
//   {"expect": {"models": 2, "selection": 1, "viewports": 1}}
//                                 beklenti; tutmazsa cikis kodu 1
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
