// ASSEMBLY & PAPER SPACE (P5) — cekirdek test.
//
// Part/Assembly iliskileri, model tabanli orto projeksiyon (Camera::standard
// ViewRotation ile AYNI rotasyon — tek kaynak), Paper Space gorunus listesi.
// OCC'siz, Linux'ta kosar.

#include "model_maker/assembly.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

static bool near2(const Vec2& a, const Vec2& b, double tol = 1e-9) {
    return std::abs(a.x - b.x) < tol && std::abs(a.y - b.y) < tol;
}

int main() {
    // 1) Assembly ekleme + bulma + silme.
    AssemblyModel model;
    Assembly a;
    a.name = L"Kafes Kirişi";
    a.mainPart = EntityId{1};
    a.parts = {EntityId{1}, EntityId{2}, EntityId{3}};
    model.addAssembly(a);
    check(model.size() == 1, "assembly eklendi");
    check(model.findAssembly(L"Kafes Kirişi") != nullptr, "findAssembly bulur");
    check(model.findAssembly(L"Yok") == nullptr, "findAssembly yok -> nullptr");

    // 2) Part/Assembly iliskileri.
    check(model.addPart(L"Kafes Kirişi", EntityId{4}), "part eklendi");
    check(model.addPart(L"Kafes Kirişi", EntityId{4}), "yinelenen part reddedilir (idempotent)");
    check(model.findAssembly(L"Kafes Kirişi")->parts.size() == 4, "4 parca");
    check(model.setMainPart(L"Kafes Kirişi", EntityId{2}), "main part degistirildi");
    check(model.findAssembly(L"Kafes Kirişi")->mainPart == EntityId{2}, "main part degeri");
    check(model.removePart(L"Kafes Kirişi", EntityId{4}), "part silindi");
    check(model.findAssembly(L"Kafes Kirişi")->parts.size() == 3, "silme sonrasi 3 parca");

    // 3) Ters sorgu: bir parca hangi assembly'lerde.
    {
        Assembly b;
        b.name = L"Catı Makası";
        b.parts = {EntityId{7}, EntityId{3}};
        model.addAssembly(b);
        const auto containing = model.assembliesContaining(EntityId{3});
        check(containing.size() == 2, "parca 3 iki assembly'de");
        const auto one = model.assembliesContaining(EntityId{7});
        check(one.size() == 1 && one[0] == L"Catı Makası", "parca 7 tek assembly'de");
    }

    // 4) Orto projeksiyon: Top identity (X->x, Y->y).
    check(near2(orthoProject({3.0, 4.0, 5.0}, OrthoView::Top), {3.0, 4.0}),
          "Top: Z atilir (x=X, y=Y)");

    // 5) Orto projeksiyon AFINDIR (paralellik korunur): orta nokta projeksiyonu
    //    = ucların projeksiyonunun orta noktasi (perspektif yok).
    {
        const Vec3 a{1, 2, 3}, b{5, -1, 4};
        const Vec3 mid{(a.x + b.x) * 0.5, (a.y + b.y) * 0.5, (a.z + b.z) * 0.5};
        for (const OrthoView v : {OrthoView::Top, OrthoView::Front, OrthoView::Right,
                                  OrthoView::Isometric}) {
            const Vec2 pa = orthoProject(a, v);
            const Vec2 pb = orthoProject(b, v);
            const Vec2 pm = orthoProject(mid, v);
            const Vec2 midOfEnds{(pa.x + pb.x) * 0.5, (pa.y + pb.y) * 0.5};
            check(near2(pm, midOfEnds), "orto projeksiyon afin (paralellik)");
        }
    }

    // 6) Gorunusler birbirinden AYRI (Top/Front/Right/Iso ayri projeksiyonlar).
    {
        const Vec3 p{1.0, 2.0, 3.0};
        const Vec2 top = orthoProject(p, OrthoView::Top);
        const Vec2 front = orthoProject(p, OrthoView::Front);
        const Vec2 right = orthoProject(p, OrthoView::Right);
        const Vec2 iso = orthoProject(p, OrthoView::Isometric);
        check(!(near2(top, front) && near2(top, right) && near2(top, iso)),
              "gorunusler birbirinden ayri");
    }

    // 7) Paper Space gorunus listesi.
    PaperSpace paper;
    DrawingView v1;
    v1.assemblyName = L"Kafes Kirişi";
    v1.view = OrthoView::Front;
    v1.placement = {100.0, 200.0};
    v1.scale = 0.5;
    paper.addView(v1);
    check(paper.size() == 1, "gorunus eklendi");
    check(paper.views()[0].view == OrthoView::Front, "gorunus turu korunur");
    check(paper.views()[0].scale == 0.5, "olcek korunur");
    check(paper.removeView(0), "gorunus silindi");
    check(paper.size() == 0, "silme sonrasi bos");
    check(!paper.removeView(0), "gecersiz silme false");

    std::printf("%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
