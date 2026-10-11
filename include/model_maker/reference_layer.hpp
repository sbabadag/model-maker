#pragma once

// REFERANS KATMANI EKRAN-UZAYI YARDIMCILARI (portatif — Windows basligi YOK).
//
// Sabit referans katmani (yapi gridi akslari + UCS eksen glifi) DUNYA biriminde
// tanimli geometridir ve dogrudan projeksiyonlanip cizilir. Bu iki yardimci o
// katmanin ZOOM'DAN BAGIMSIZ kalmasi icin gereken iki kurali tutar.
//
// ---------------------------------------------------------------------------
// 1) KIRPMA — neden gerekli
// ---------------------------------------------------------------------------
// Zoom arttikca AYNI dunya geometrisi EKRANDA dogrusal buyur. Kirpma olmadan
// derin zoom'da tek aks cizgisi yuz binlerce piksel uzunlugunda cizilir.
//
// OLCUM (gercek Camera + grid, tests/zoom_deep_cost_probe.cpp; 1600x900,
// 18x15x7.2 m bina, ISO -> plan gridi, 48 tekerlek detenti = 1.12^48 = 231x):
//   cizilen TOPLAM ekran uzunlugu   8.091 px  ->  1.864.015 px   (230x)
//   tek en uzun aks cizgisi         1.093 px  ->    251.894 px
// Akslar PS_GEOMETRIC|PS_USERSTYLE kesikli kalemle cizilir; GDI geometric pen
// desen fazini kirpma oncesinde TUM hat boyunca hesaplar, bu yuzden maliyet
// cizgi uzunluguyla buyur -> "zoom derinlestikce cok yavasliyor".
//
// KURAL: referans katmani cizilirken segment EKRAN uzayinda tuval
// dikdortgenine kirpilir. Kirpilmis cizgi en fazla tuval kosegeni kadar olur
// (1836 px @1600x900), yani maliyet zoom'dan BAGIMSIZ hale gelir.
//
// ---------------------------------------------------------------------------
// 2) GLIF UZUNLUGU — neden gerekli
// ---------------------------------------------------------------------------
// UCS oklari EKRAN-OLCEKLI olmali (~120 px), dunya biriminde degil. ONCEKI kod
// dunya biriminde bir ALT SINIR (60) tasiyordu: zoom arttikca pxPerUnit buyur,
// 120/pxPerUnit 60'in altina duser, kelepce 60'a oturur ve cizilen EKRAN
// uzunlugu 60*pxPerUnit olur — yani zoom ile DOGRUSAL buyur. OLCUM (48 detent):
// ok ekranda 120 px -> 758 px. Tek dogru form: dunya uzunlugu = hedefPx/pxPerUnit.

namespace mm {

// [x0,y0]-[x1,y1] segmentini [xmin,xmax] x [ymin,ymax] dikdortgenine kirpar
// (Liang-Barsky). true donerse koordinatlar kirpilmis segmentle GUNCELLENIR;
// false donerse segment dikdortgenin tamamen disindadir (cizme).
inline bool clipSegmentToRect(double& x0, double& y0, double& x1, double& y1,
                              double xmin, double ymin, double xmax, double ymax) noexcept {
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    double t0 = 0.0;
    double t1 = 1.0;
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {x0 - xmin, xmax - x0, y0 - ymin, ymax - y0};
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0.0) {
            // Segmente paralel kenar: tamamen disarida ise gorunmez.
            if (q[i] < 0.0) return false;
            continue;
        }
        const double r = q[i] / p[i];
        if (p[i] < 0.0) {
            if (r > t1) return false;
            if (r > t0) t0 = r;
        } else {
            if (r < t0) return false;
            if (r < t1) t1 = r;
        }
    }
    const double nx0 = x0 + t0 * dx;
    const double ny0 = y0 + t0 * dy;
    const double nx1 = x0 + t1 * dx;
    const double ny1 = y0 + t1 * dy;
    x0 = nx0;
    y0 = ny0;
    x1 = nx1;
    y1 = ny1;
    return true;
}

// Eksen glifi (UCS oklari) icin DUNYA uzunlugu: ekranda `targetPixels` kalacak
// sekilde. TEK KAYNAK — renderer bunu cagirir, test bu invariant'i olcer.
// pxPerUnit cok kucukse (dejenere olcek) makul bir dunya uzunluguna duser.
inline double referenceGlyphWorldLength(double pxPerUnit, double targetPixels) noexcept {
    return pxPerUnit > 1e-9 ? targetPixels / pxPerUnit : 2.0;
}

} // namespace mm
