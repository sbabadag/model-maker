// H-9 TEMAS DENETIMI — "kiris kolona degmiyor" fail-closed kalkani.
//
// Gerçek kullanıcı modeli (CUMALI KOC.mmw): mertek ön görünüşte çizildiği için
// derinlik (Y) 0'da kalmış, kolonlar Y=-250 düzleminde. Yerleşim matematiği
// kolonun yüz DÜZLEMİNİ kesiştirdiği için alın levhası kolondan 155 mm uzakta
// "havada" üretiliyordu ve işlem başarılı görünüyordu. componentContactCheck
// bu durumu reddeder; bu test hem temas eden (yeşil) hem etmeyen (kırmızı)
// durumu kilitler.
#include "model_maker/component_engine.hpp"
#include <cmath>
#include <cstdio>
#include <string>

using namespace mm;

static int bad = 0;
static void check(bool p, const char* what) {
    std::printf("%s %s\n", p ? "PASS" : "FAIL", what);
    bad += !p;
}
static bool has(const std::wstring& s, const wchar_t* needle) {
    return s.find(needle) != std::wstring::npos;
}

// Düşey kolon (0,0,0)->(0,0,3000), 300x300 (her yönde yarı kapsam 150).
// Yatay kiriş X boyunca, verilen Y ofsetinde; 100 geniş / 200 yüksek
// (Y yönünde yarı kapsam 50) -> toplam izin verilen ofset 200 mm.
static ComponentInput makeInput(double beamY) {
    ComponentInput in;
    in.main.id = 1;
    in.main.axisFrom = {0, 0, 0};
    in.main.axisTo = {0, 0, 3000};
    in.main.refNormal = {0, 1, 0};
    in.main.secWidth = 300;
    in.main.secHeight = 300;
    in.main.sectionCenterOffset = 0.0;

    MemberGeometry beam;
    beam.id = 2;
    beam.axisFrom = {6000, beamY, 3000};
    beam.axisTo = {0, beamY, 3000};
    beam.refNormal = {0, 0, 1};
    beam.secWidth = 100;
    beam.secHeight = 200;
    beam.sectionCenterOffset = 0.0;
    in.secondary = {beam};
    return in;
}

int main() {
    // 1) Hizali: kesitler tam ortusur.
    {
        const ComponentContact c = componentContactCheck(makeInput(0.0));
        check(c.checked, "hizali: kontrol calisti");
        check(c.inContact, "hizali: temas VAR (yesil)");
        check(c.gap == 0.0, "hizali: bosluk 0");
    }
    // 2) Kismi ortusme (150 mm ofset, izin 200) hala temastir.
    {
        const ComponentContact c = componentContactCheck(makeInput(150.0));
        check(c.inContact, "150 mm ofset: kesitler ortusuyor -> temas VAR");
    }
    // 3) Kullanicinin kusuru: 400 mm ofset -> kesitler ayrik, baglanti RED.
    {
        const ComponentContact c = componentContactCheck(makeInput(400.0));
        check(c.checked, "ofset: kontrol calisti");
        check(!c.inContact, "400 mm ofset: temas YOK (kirmizi)");
        check(c.gap > 190.0 && c.gap < 210.0, "ofset: bosluk ~200 mm olculdu");
        check(has(c.message, L"degmiyor"), "ofset: mesaj 'degmiyor' iceriyor");
        check(has(c.message, L"Y yonunde"), "ofset: mesaj kayma eksenini (Y) soyluyor");
        std::printf("     mesaj: %ls\n", c.message.c_str());
    }
    // 4) Secim sirasi sonucu degistirmez (kolon/kiris ters secilmis).
    {
        ComponentInput in = makeInput(400.0);
        ComponentInput swapped;
        swapped.main = in.secondary[0];
        swapped.secondary = {in.main};
        const ComponentContact c = componentContactCheck(swapped);
        check(!c.inContact, "ters secim: yine RED");
        check(c.gap > 190.0 && c.gap < 210.0, "ters secim: ayni bosluk");
    }
    // 5) Eksik girdi: kontrol sessizce atlanir (baska dogrulama yakalar).
    {
        ComponentInput in;
        in.main = makeInput(0.0).main;
        const ComponentContact c = componentContactCheck(in);
        check(!c.checked, "ikincil yok: kontrol atlandi (checked=false)");
    }
    std::printf("%s\n", bad == 0 ? "TUM KONTROLLER GECTI" : "HATA VAR");
    return bad == 0 ? 0 : 1;
}
