// KONTROL NOKTASI COKLU SECIMI regresyon testi (Selahattin 2026-10-31).
//
// Kural:
//   * Alt + pencere secimi  -> ONCEKI secimi birak, pencere ICINDE kalanlari sec
//   * Shift + Alt + pencere -> eskileri koru, yenileri ekle
//   * Sadece SECILI profillerin kontrol noktalari secilebilir
//   * Secilen noktalar move / move-delta ile TOPLU tasinir
//
// Bu test soyut dikdortgenlerle yetinmez: GERCEK kamera + GERCEK profilli
// katilarin uc noktalari (memberEndCenter) ekrana projekte edilir, pencere
// secimi o ekran koordinatlariyla yapilir.
#include "model_maker/camera.hpp"
#include "model_maker/document.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/grip_selection.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace mm;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) { ++failures; std::printf("FAIL %s\n", what); }
    else std::printf("ok   %s\n", what);
}

std::size_t addProfile(Document& doc, const Vec3& from, const Vec3& to) {
    EntityProperties p;
    p.profileName = "BOX200";
    p.axisFromX = from.x; p.axisFromY = from.y; p.axisFromZ = from.z;
    p.axisToX = to.x; p.axisToY = to.y; p.axisToZ = to.z;
    std::vector<Vec3> v;
    for (const Vec3& axis : {from, to}) {
        v.push_back({axis.x, axis.y - 100.0, axis.z - 100.0});
        v.push_back({axis.x, axis.y + 100.0, axis.z - 100.0});
        v.push_back({axis.x, axis.y + 100.0, axis.z + 100.0});
        v.push_back({axis.x, axis.y - 100.0, axis.z + 100.0});
    }
    std::vector<Edge> e{{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},
                        {0,4},{1,5},{2,6},{3,7}};
    // Gercek profil katisi gibi YUZLU olsun (uygulama tutamaklari yalniz
    // B-Rep katilari icin acar; faces().empty() olan cizgiler tutamak tasimaz).
    std::vector<Face> f{{0,1,2,3}, {4,5,6,7}, {0,1,5,4},
                        {1,2,6,5}, {2,3,7,6}, {3,0,4,7}};
    WireframeModel m(v, e, f);
    m.setProperties(p);
    const std::size_t index = doc.models().size();
    doc.addModel(std::move(m));
    return index;
}

// Secilebilir tutamaklar: yalniz SECILI profilli katilarin iki ucu.
std::vector<GripCandidate> candidatesFor(const Document& doc,
                                         const std::vector<std::size_t>& selected,
                                         const Camera& cam, int width, int height) {
    std::vector<GripCandidate> out;
    for (const std::size_t index : selected) {
        if (index >= doc.models().size()) continue;
        const auto& m = doc.models()[index];
        if (m.faces().empty() || m.properties().profileName.empty()) continue;
        for (const bool toEnd : {false, true}) {
            const Vec2 px = cam.project(memberEndCenter(m, toEnd), width, height);
            out.push_back(GripCandidate{GripRef{index, toEnd}, GripScreenPoint{px.x, px.y}});
        }
    }
    return out;
}

GripScreenPoint screenOf(const Document& doc, std::size_t index, bool toEnd,
                         const Camera& cam, int width, int height) {
    const Vec2 px = cam.project(memberEndCenter(doc.models()[index], toEnd), width, height);
    return GripScreenPoint{px.x, px.y};
}

}  // namespace

int main() {
    Document doc;
    const std::size_t colA = addProfile(doc, {0, 0, 0}, {0, 0, 4000});       // dikey
    const std::size_t colB = addProfile(doc, {3000, 0, 0}, {3000, 0, 4000}); // dikey
    const std::size_t beam = addProfile(doc, {0, 0, 4000}, {3000, 0, 4000}); // yatay

    const int W = 1400, H = 1000;
    Camera cam;
    cam.setView(StandardView::Front);
    cam.setCenter3D(Vec3{1500, 0, 2000});
    cam.fit3D(Vec3{-500, -500, -500}, Vec3{3500, 500, 4500}, W, H, 40.0);

    // 1) Yalniz SECILI profiller secilebilir: colA + colB secili, beam degil.
    std::vector<std::size_t> selected{colA, colB};
    auto cand = candidatesFor(doc, selected, cam, W, H);
    std::printf("aday tutamak=%zu (secili 2 profil x 2 uc)\n", cand.size());
    check(cand.size() == 4, "yalniz secili profillerin 2+2 kontrol noktasi aday oluyor");
    bool beamLeak = false;
    for (const auto& c : cand) beamLeak = beamLeak || (c.ref.solidIndex == beam);
    check(!beamLeak, "secili OLMAYAN profilin kontrol noktasi adaya girmiyor");

    // 2) WINDOW: sol taraftaki iki noktayi (colA from/to) kapsayan dikdortgen.
    const GripScreenPoint aFrom = screenOf(doc, colA, false, cam, W, H);
    const GripScreenPoint aTo = screenOf(doc, colA, true, cam, W, H);
    std::printf("colA from=(%.1f,%.1f) to=(%.1f,%.1f)\n",
                aFrom.x, aFrom.y, aTo.x, aTo.y);
    const GripScreenPoint first{aFrom.x - 6.0, std::min(aFrom.y, aTo.y) - 6.0};
    const GripScreenPoint second{aFrom.x + 6.0, std::max(aFrom.y, aTo.y) + 6.0};
    auto picked = gripsInWindow(cand, first, second);
    std::printf("pencere ici nokta=%zu\n", picked.size());
    check(picked.size() == 2, "pencere yalnizca icindeki 2 noktayi seciyor");
    check(gripIsSelected(picked, GripRef{colA, false}) && gripIsSelected(picked, GripRef{colA, true}),
          "secilen noktalar colA'nin from+to uclari");

    // 3) Surukleme yonu ters (sag-alt -> sol-ust) ayni sonucu vermeli.
    auto reversed = gripsInWindow(cand, second, first);
    check(reversed == picked, "ters yonde surukleme ayni pencereyi secıyor");

    // 4) WINDOW kesin: pencerenin DISINDA kalan nokta (colB) secilmemeli.
    check(!gripIsSelected(picked, GripRef{colB, false}),
          "pencere disindaki komsu nokta secilmiyor");

    // 5) Alt + pencere = ESKI SECIMI BIRAK.
    std::vector<GripRef> existing{GripRef{colB, false}, GripRef{colB, true}};
    auto replaced = mergeGripSelection(existing, picked, /*additive=*/false);
    check(replaced.size() == 2 && gripIsSelected(replaced, GripRef{colA, false}) &&
              !gripIsSelected(replaced, GripRef{colB, false}),
          "Alt+pencere (additive=false) eski secimi birakip yenileri seciyor");

    // 6) Shift+Alt + pencere = ESKILERI KORU, yenileri EKLE (tekrarsiz).
    auto merged = mergeGripSelection(existing, picked, /*additive=*/true);
    std::printf("shift+alt sonrasi nokta=%zu\n", merged.size());
    check(merged.size() == 4, "Shift+Alt+pencere eski 2 noktayi koruyup 2 yenisini ekliyor");
    check(gripIsSelected(merged, GripRef{colB, true}) && gripIsSelected(merged, GripRef{colA, false}),
          "birlesimde hem eski hem yeni noktalar var");
    auto again = mergeGripSelection(merged, picked, /*additive=*/true);
    check(again.size() == merged.size(), "ayni pencereyi tekrar taramak nokta cogaltmiyor");

    // 7) Profil secimden cikarsa noktalari da duser (yalniz secili profiller).
    auto pruned = pruneGripSelection(merged, {colA});
    std::printf("colB secimden cikti -> kalan nokta=%zu\n", pruned.size());
    check(pruned.size() == 2 && gripIsSelected(pruned, GripRef{colA, true}) &&
              !gripIsSelected(pruned, GripRef{colB, false}),
          "secimden cikan profilin noktalari secimden dusuyor");

    // 8) Tek nokta toggle: ekle / cikar.
    std::vector<GripRef> single;
    toggleGripSelection(single, GripRef{beam, true});
    const bool added = gripIsSelected(single, GripRef{beam, true});
    toggleGripSelection(single, GripRef{beam, true});
    std::printf("toggle sonrasi nokta=%zu\n", single.size());
    check(added && single.empty(), "toggle ayni noktayi ekleyip cikariyor");

    // 9) Sinirda kalan nokta pencereye DAHIL (kapanis kurali: [sol, sag]).
    const auto edge = gripsInWindow(cand, aFrom, aTo);
    check(gripIsSelected(edge, GripRef{colA, false}) && gripIsSelected(edge, GripRef{colA, true}),
          "tam sinirdaki noktalar pencereye dahil");

    std::printf(failures == 0 ? "TUM TESTLER GECTI\n" : "%d TEST BASARISIZ\n", failures);
    return failures == 0 ? 0 : 1;
}
