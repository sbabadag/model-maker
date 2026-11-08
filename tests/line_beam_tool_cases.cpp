// Included by run_line_beam_tool.py after the real commitPoint body.
static int checks = 0, failures = 0;
static void check(bool condition, const char* message) {
    ++checks;
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
static bool close(mm::Vec3 a, mm::Vec3 b) {
    return std::abs(a.x-b.x) < 1e-8 && std::abs(a.y-b.y) < 1e-8 &&
           std::abs(a.z-b.z) < 1e-8;
}
int main() {
    using namespace mm;
    {   // B6: panelde AKTIF PROFIL varken Cizgi -> DUZ cizgi (profil atama YOK)
        Application a;
        a.setPendingProfileName("HEB240");
        a.selectTool(DrawTool::Line);
        a.commitPoint({0,0,0});
        a.commitPoint({3,0,0});
        check(a.profileAssignCalls == 0,
              "Cizgi aktif profili yok sayar (profil atamasi cagirilmaz)");
        check(a.document_.models().size() == 1, "Cizgi tek duz cizgi uretir");
        if (!a.document_.models().empty()) {
            const WireframeModel& m = a.document_.models().front();
            check(m.properties().profileName.empty(),
                  "cizgi profilsizdir (profileName bos)");
            const auto v = m.vertices();
            check(v.size() == 2 && close(v[0], {0,0,0}) && close(v[1], {3,0,0}),
                  "cizgi uclari (0,0,0)-(3,0,0)");
        }
    }
    {   // B6: profil YOKKEN Cizgi -> duz cizgi (regresyon korumasi)
        Application a;
        a.selectTool(DrawTool::Line);
        a.commitPoint({0,0,0});
        a.commitPoint({3,0,0});
        check(a.profileAssignCalls == 0 && a.document_.models().size() == 1,
              "profil yokken Cizgi duz cizgi uretir");
    }
    {   // B6: Polyline aktif profili yok sayar (tek obje olarak uzar)
        Application a;
        a.setPendingProfileName("HEB240");
        a.selectTool(DrawTool::Polyline);
        a.commitPoint({0,0,0});
        a.commitPoint({1,1,0});
        a.commitPoint({2,0,0});
        check(a.profileAssignCalls == 0 && a.document_.models().size() == 1,
              "Polyline aktif profili yok sayar");
        if (!a.document_.models().empty()) {
            const auto v = a.document_.models().front().vertices();
            check(v.size() == 3 && close(v[0], {0,0,0}) && close(v[1], {1,1,0}) &&
                      close(v[2], {2,0,0}),
                  "Polyline 3 koseyi tek obje olarak toplar");
        }
    }
    {   // B6: Dikdortgen aktif profili yok sayar
        Application a;
        a.setPendingProfileName("HEB240");
        a.selectTool(DrawTool::Rectangle);
        a.commitPoint({0,0,0});
        a.commitPoint({4,2,0});
        check(a.profileAssignCalls == 0 && a.document_.models().size() == 1,
              "Dikdortgen aktif profili yok sayar");
    }
    {   // B6: Daire aktif profili yok sayar
        Application a;
        a.setPendingProfileName("HEB240");
        a.selectTool(DrawTool::Circle);
        a.commitPoint({0,0,0});
        a.commitPoint({5,0,0});
        check(a.profileAssignCalls == 0 && a.document_.models().size() == 1,
              "Daire aktif profili yok sayar");
    }
    {   // B6: KIRIS + aktif profil -> profil atamasi cagrilir (Beam araci)
        Application a;
        a.setPendingProfileName("HEB240");
        a.selectTool(DrawTool::Beam);
        a.commitPoint({0,0,0});
        a.commitPoint({4,0,0});
        check(a.profileAssignCalls == 1, "Kiris aktif profili uygular");
        check(a.lastProfileName == "HEB240", "Kiris dogru profili kullanir");
        check(a.lastProfileFrom && a.lastProfileTo &&
                  close(*a.lastProfileFrom, {0,0,0}) && close(*a.lastProfileTo, {4,0,0}),
              "Kiris uclari dogru gecirilir");
    }
    {   // B6: KIRIS profilsiz -> hicbir sey uretmez (savunma; Qt kapisi ayrica)
        Application a;
        a.selectTool(DrawTool::Beam);
        a.commitPoint({0,0,0});
        a.commitPoint({4,0,0});
        check(a.profileAssignCalls == 0 && a.document_.models().empty(),
              "Kiris profilsiz geometri uretmez");
    }
    {   // B6: Kolon HALA profili kullanir (placeColumn akisi degismez)
        Application a;
        a.setPendingProfileName("HEB240");
        a.selectTool(DrawTool::Column);
        a.commitPoint({5,6,7});
        check(a.lastPlacedColumn && close(*a.lastPlacedColumn, {5,6,7}),
              "Kolon tek tikla placeColumn akisina gider");
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
