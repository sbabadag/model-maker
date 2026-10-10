// Included by run_gumball_interaction.py after actual production method bodies.
static int checks = 0, failures = 0;
static void check(bool condition, const char* message) {
    ++checks;
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
static bool close(mm::Vec3 a, mm::Vec3 b) {
    return std::abs(a.x-b.x) < 1e-8 && std::abs(a.y-b.y) < 1e-8 && std::abs(a.z-b.z) < 1e-8;
}
static void seed(mm::Application& app) {
    app.document_.addModel(mm::WireframeModel::line({0,0,0}, {3,4,0}));
    app.updateGumball();
}
int main() {
    using namespace mm;
    {
        Application a; seed(a);
        int calls = 0;
        a.parameterRequest_ = [&](const std::wstring&, const std::wstring&, std::wstring& out) {
            ++calls; check(!captured, "mouse capture released before modal dialog");
            out = L"5"; return true;
        };
        a.gumballBeginDrag(600,400,GumballHandle::AxisX);
        a.onLeftButtonUp(600,400);
        check(calls == 1, "single click opens numeric edit box");
        check(close(a.document_.models()[0].vertices()[0], {3,4,0}), "click +5 translates along local X");
        check(a.document_.undo(), "numeric translation has one undo");
        check(close(a.document_.models()[0].vertices()[0], {0,0,0}), "undo restores numeric translation");
        check(!a.document_.undo(), "no extra empty undo from mouse-down");
    }
    {
        Application a; seed(a);
        auto p = a.gumballProject(a.gumballOrigin_);
        a.parameterRequest_ = [](const auto&, const auto&, auto&) { return false; };
        a.gumballBeginDrag(static_cast<int>(p.x),static_cast<int>(p.y),GumballHandle::ScaleUniform);
        check(close(a.document_.models()[0].vertices()[1], {3,4,0}), "uniform scale mouse-down never changes geometry");
        a.onLeftButtonUp(static_cast<int>(p.x),static_cast<int>(p.y));
        check(close(a.document_.models()[0].vertices()[1], {3,4,0}), "cancel leaves geometry untouched");
        check(!a.document_.canUndo(), "cancelled dialog creates no undo entry");
    }
    for (auto handle : {GumballHandle::PlaneXY, GumballHandle::PlaneYZ, GumballHandle::PlaneZX}) {
        Application a; seed(a);
        int k = static_cast<int>(handle) - 3;
        Vec3 expected = a.gumballFrame_[k] * -1.5 + a.gumballFrame_[(k+1)%3] * 2.25;
        int calls = 0;
        a.parameterRequest_ = [&](const std::wstring&, const std::wstring&, std::wstring& out) {
            if (++calls > 1) return false;
            out = L" -1,5 ; +2.25 "; return true;
        };
        a.gumballBeginDrag(600,400,handle);
        a.onLeftButtonUp(600,400);
        check(calls == 1, "plane edit accepts two signed decimal-comma values");
        check(close(a.document_.models()[0].vertices()[0], expected), "plane edit uses the two local axes");
        check(a.document_.undo(), "plane edit undo exists");
        check(close(a.document_.models()[0].vertices()[0], {0,0,0}), "plane edit undo restores geometry");
        check(!a.document_.undo(), "plane edit creates exactly one undo");
    }
    {
        Application a; seed(a);
        const Vec3 expected = a.gumballFrame_[0]*2 + a.gumballFrame_[1]*-3 + a.gumballFrame_[2]*4;
        int calls = 0;
        a.parameterRequest_ = [&](const auto&, const auto&, auto& out) {
            if (++calls > 1) return false;
            out = L"2;-3;4"; return true;
        };
        a.gumballBeginDrag(600,400,GumballHandle::Center);
        a.onLeftButtonUp(600,400);
        check(close(a.document_.models()[0].vertices()[0], expected), "center edit translates in local XYZ");
    }
    for (auto mode : {EditMode::Draw2D, EditMode::View3D}) {
        Application a; seed(a); a.mode_ = mode;
        for (double zoom : {0.2, 1.0, 5.0}) {
            a.camera_.zoomBy(zoom);
            const auto c = a.gumballProject(a.gumballOrigin_);
            const auto p = a.gumballProject(a.gumballOrigin_ + Vec3{a.gumballWorldLength(),0,0});
            check(std::abs(std::hypot(p.x-c.x,p.y-c.y)-82.0)<1e-7, "compact gumball remains 82px when zoom changes");
        }
    }
    for (int axis=0; axis<3; ++axis) {
        Application a; seed(a);
        Vec3 expected = a.gumballFrame_[axis] * -2.5;
        a.parameterRequest_ = [](const auto&, const auto&, auto& out) { out=L"-2,5"; return true; };
        a.gumballBeginDrag(600,400,static_cast<GumballHandle>(axis));
        a.gumballDragMove(602,401); // tiny wobble must still be a click
        check(close(a.document_.models()[0].vertices()[0], {0,0,0}), "click wobble does not transform");
        a.onLeftButtonUp(602,401);
        check(close(a.document_.models()[0].vertices()[0], expected), "all axes accept negative local distance");
    }
    for (int axis=0; axis<3; ++axis) {
        Application a; seed(a);
        const Vec3 pivot=a.gumballOrigin_;
        const Vec3 offset = axis==0 ? a.gumballFrame_[0]*2.5 :
            axis==1 ? a.gumballFrame_[2]*2.5 : a.gumballFrame_[1]*-2.5;
        int calls=0;
        a.parameterRequest_ = [&](const std::wstring& prompt, const auto&, auto& out) {
            ++calls;
            check(prompt.find(L"derece")!=std::wstring::npos, "ring edit labels degrees");
            a.gumballDragMove(900,700); // nested modal message must not drag
            out=L"-90"; return true;
        };
        a.gumballBeginDrag(600,400,static_cast<GumballHandle>(7+axis));
        a.onLeftButtonUp(600,400);
        check(calls==1, "ring click opens one dialog");
        check(close(a.document_.models()[0].vertices()[1], pivot+offset), "ring rotates around correct local axis and pivot");
        check(a.document_.undo(), "ring numeric undo");
        check(close(a.document_.models()[0].vertices()[1], {3,4,0}), "rotation undo restores endpoint");
        check(a.document_.redo(), "ring numeric redo");
        check(close(a.document_.models()[0].vertices()[1], pivot+offset), "rotation redo restores rotated endpoint");
    }
    for (auto handle : {GumballHandle::ScaleX,GumballHandle::ScaleY,GumballHandle::ScaleZ,GumballHandle::ScaleUniform}) {
        Application a; seed(a);
        a.parameterRequest_=[](const auto&,const auto&,auto& out) {out=L"2";return true;};
        a.gumballBeginDrag(600,400,handle); a.onLeftButtonUp(600,400);
        Vec3 expected = (handle==GumballHandle::ScaleX || handle==GumballHandle::ScaleUniform) ? Vec3{4.5,6,0} : Vec3{3,4,0};
        check(close(a.document_.models()[0].vertices()[1],expected), "scale edit respects local directions/pivot");
        check(a.document_.undo(), "scale click undo");
        check(close(a.document_.models()[0].vertices()[1],{3,4,0}), "scale undo restores endpoint");
    }
    for (const auto& bad : {L"",L"nan",L"inf",L"1e309",L"12mm",L"1;2",L"1.2.3"}) {
        Application a; seed(a); int calls=0;
        a.parameterRequest_=[&](const auto&,const auto&,auto& out) {out=bad;return ++calls==1;};
        a.gumballBeginDrag(600,400,GumballHandle::AxisX); a.onLeftButtonUp(600,400);
        check(calls==2, "invalid scalar reopens edit box for correction");
        check(!a.document_.canUndo(), "invalid scalar never mutates history");
        check(close(a.document_.models()[0].vertices()[1],{3,4,0}), "invalid scalar never mutates geometry");
    }
    for (const auto& bad : {L"-1",L"0",L"101"}) {
        Application a; seed(a); int calls=0;
        a.parameterRequest_=[&](const auto&,const auto&,auto& out) {out=bad;return ++calls==1;};
        a.gumballBeginDrag(600,400,GumballHandle::ScaleX); a.onLeftButtonUp(600,400);
        check(calls==2 && !a.document_.canUndo(), "invalid scales rejected instead of silently clamped");
    }
    for (const auto& bad : {L"1",L"1;",L"1;2;",L"1;2;3",L"1;nan",L";2"}) {
        Application a; seed(a); int calls=0;
        a.parameterRequest_=[&](const auto&,const auto&,auto& out) {out=bad;return ++calls==1;};
        a.gumballBeginDrag(600,400,GumballHandle::PlaneXY); a.onLeftButtonUp(600,400);
        check(calls==2 && !a.document_.canUndo(), "plane edit rejects malformed or incomplete vector atomically");
    }
    {
        Application a; seed(a); int calls=0;
        a.parameterRequest_=[&](const auto&,const auto&,auto&) {++calls;return false;};
        a.gumballBeginDrag(600,400,GumballHandle::AxisX);
        a.gumballDragMove(630,400); a.onLeftButtonUp(630,400);
        check(calls==0, "real drag does not open a numeric dialog");
        check(!close(a.document_.models()[0].vertices()[0],{0,0,0}), "real drag still moves geometry");
        check(a.document_.undo(), "drag still undoable in one step");
        check(close(a.document_.models()[0].vertices()[0],{0,0,0}), "drag undo restores original geometry");
        check(!a.document_.undo(), "drag only created one undo");
        a.parameterRequest_=[](const auto&,const auto&,auto&) {return false;};
        a.gumballBeginDrag(600,400,GumballHandle::AxisX); a.onLeftButtonUp(600,400);
        check(a.document_.canRedo(), "cancel preserves previous redo history");
        a.parameterRequest_=[](const auto&,const auto&,auto& out) {out=L"0";return true;};
        a.gumballBeginDrag(600,400,GumballHandle::AxisX); a.onLeftButtonUp(600,400);
        check(a.document_.canRedo(), "accepted zero preserves previous redo history");
    }
    // ---- SHIFT + GUMBALL = KOPYALA (yerel eksende) ----
    {
        Application a; seed(a); g_shiftDown = 0x8000;
        a.gumballBeginDrag(600,400,GumballHandle::AxisX);
        a.gumballDragMove(630,400);          // ilk hareket: kopya burada olusur
        a.gumballDragMove(640,400);          // ara adim kopya URETMEZ
        a.gumballDragMove(650,400);
        a.onLeftButtonUp(650,400);
        g_shiftDown = 0;
        check(a.document_.models().size() == 2, "shift+drag creates exactly one copy");
        check(close(a.document_.models()[0].vertices()[0], {0,0,0}), "shift+drag leaves the original in place");
        check(close(a.document_.models()[1].vertices()[0], {0,0,0}) == false, "shift+drag moves the copy");
        check(a.selectedModels_.size() == 1 && a.selectedModels_[0] == 1, "selection follows the copy");
        check(a.document_.undo(), "copy drag is one undo step");
        check(a.document_.models().size() == 1, "undo removes the copy");
        check(close(a.document_.models()[0].vertices()[0], {0,0,0}), "undo leaves the original untouched");
        check(!a.document_.canUndo(), "copy drag created exactly one undo entry");
        check(a.document_.redo() && a.document_.models().size() == 2, "redo restores the copy");
    }
    {
        Application a; seed(a);              // Shift YOK -> kopya olmamali
        a.gumballBeginDrag(600,400,GumballHandle::AxisX);
        a.gumballDragMove(630,400); a.onLeftButtonUp(630,400);
        check(a.document_.models().size() == 1, "drag without shift never copies");
        check(!close(a.document_.models()[0].vertices()[0], {0,0,0}), "drag without shift still moves");
        check(a.document_.undo(), "plain drag keeps one undo");
    }
    {
        Application a; seed(a); g_shiftDown = 0x8000;   // Escape: kopya iptal
        a.gumballBeginDrag(600,400,GumballHandle::AxisX);
        a.gumballDragMove(640,400);
        a.gumballCancelDrag();
        g_shiftDown = 0;
        check(a.document_.models().size() == 1, "escape cancels the shift copy");
        check(a.selectedModels_.size() == 1 && a.selectedModels_[0] == 0, "escape restores the original selection");
        check(close(a.document_.models()[0].vertices()[0], {0,0,0}), "escape leaves geometry untouched");
        check(a.document_.undo(), "cancelled copy keeps the net-zero undo entry");
        check(a.document_.models().size() == 1 && close(a.document_.models()[0].vertices()[0], {0,0,0}),
              "undo of a cancelled copy is a harmless no-op");
    }
    {
        Application a; seed(a); g_shiftDown = 0x8000;   // shift + sayisal kutu
        a.parameterRequest_ = [](const auto&, const auto&, auto& out) { out = L"5"; return true; };
        a.gumballBeginDrag(600,400,GumballHandle::AxisX);
        a.onLeftButtonUp(600,400);
        g_shiftDown = 0;
        check(a.document_.models().size() == 2, "shift + numeric entry copies");
        check(close(a.document_.models()[0].vertices()[0], {0,0,0}), "numeric copy keeps the original");
        check(close(a.document_.models()[1].vertices()[0], a.gumballFrame_[0]*5), "numeric copy takes the typed offset");
        check(a.document_.undo() && a.document_.models().size() == 1, "numeric copy undo removes only the copy");
    }
    {
        Application a; seed(a); g_shiftDown = 0x8000;   // shift + dondurme
        // Yerel X = kirisin kendi boyu; uc o eksenin UZERINDE oldugu icin
        // donmez. Yerel Z ekseni etrafinda donduruyoruz.
        const Vec3 pivot = a.gumballOrigin_;
        const Vec3 before = a.document_.models()[0].vertices()[1];
        const double radius = std::hypot(before.x - pivot.x, before.y - pivot.y);
        a.gumballBeginDrag(600,400,GumballHandle::RotZ);
        a.gumballDragMove(900,700); a.onLeftButtonUp(900,700);
        g_shiftDown = 0;
        const Vec3 after = a.document_.models()[1].vertices()[1];
        check(a.document_.models().size() == 2, "shift + rotation copies");
        check(close(a.document_.models()[0].vertices()[1], before), "rotation copy leaves the original");
        check(!close(after, before), "rotation copy actually turns");
        check(std::abs(std::hypot(after.x - pivot.x, after.y - pivot.y) - radius) < 1e-9,
              "rotation copy stays rigid around the pivot");
    }
    {
        Application a; seed(a); g_shiftDown = 0x8000;   // shift + olcek
        a.gumballBeginDrag(600,400,GumballHandle::ScaleUniform);
        a.gumballDragMove(700,600); a.onLeftButtonUp(700,600);
        g_shiftDown = 0;
        check(a.document_.models().size() == 2, "shift + scale copies");
        check(close(a.document_.models()[0].vertices()[1], {3,4,0}), "scale copy leaves the original");
        check(!close(a.document_.models()[1].vertices()[1], {3,4,0}), "scale copy is scaled");
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
