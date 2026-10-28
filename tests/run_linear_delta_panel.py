#!/usr/bin/env python3
"""Portable harness: the real Application delta-panel state machine.

Extracts `Application::requestLinearDeltaPanel` and `Application::applyLinearDelta`
VERBATIM from src/application.cpp (never reimplemented) and links them against a
minimal Application stub whose only fake is commitTransformPoint (the copy/move
geometry body, already covered by the existing ctest suite) plus the Win32/Qt
boundary (SetCursor/MessageBeep/update*).

Proves:
  1) panel "Tamam"      -> commitTransformPoint(delta) runs once, compound delta
                           equals the typed value, base point = origin,
  2) panel "Iki Nokta"  -> no commit, phase=BasePoint, pick mode on,
  3) panel "Iptal"      -> command cancelled, nothing committed,
  4) zero delta         -> nothing committed,
  5) last delta is remembered for the next panel (on-doldurma),
  6) MoveLinear and CopyLinear both go through the same single path,
and locks the two call sites in startTransformCommand / the Enter-selection
branch by source pattern (removing them breaks the feature silently).

Usage: python3 tests/run_linear_delta_panel.py [build-directory]
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
app = (root / "src/application.cpp").read_text(encoding="utf-8")

panel = re.search(r"^bool Application::requestLinearDeltaPanel\(.*?^}\n", app, re.M | re.S)
apply = re.search(r"^void Application::applyLinearDelta\(.*?^}\n", app, re.M | re.S)
if not panel or not apply:
    sys.exit("requestLinearDeltaPanel/applyLinearDelta bulunamadi (kaynak degismis)")

# KAYNAK KILIDI: panel iki yerden cagrilmali (komut baslangici + secim Enter'i).
start_fn = re.search(r"^void Application::startTransformCommand\(.*?^}\n", app, re.M | re.S)
assert start_fn, "startTransformCommand bulunamadi"
if "requestLinearDeltaPanel()" not in start_fn.group():
    sys.exit("KILIT: startTransformCommand paneli acmiyor")
commit_fn = re.search(r"^void Application::commitTransformPoint\(.*?^}\n", app, re.M | re.S)
if not commit_fn:
    sys.exit("KILIT: commitTransformPoint bulunamadi")
commit_body = commit_fn.group()
if "linearDeltaFromPick_ = true;" not in commit_body or "requestLinearDeltaPanel();" not in commit_body:
    sys.exit("KILIT: iki nokta OLCUSU panele tasinmiyor (olcum islem yapmamali)")
if app.count("requestLinearDeltaPanel();") < 3:
    sys.exit("KILIT: panel cagri yerleri eksik (baslangic + secim + olcu)")
if app.count("requestLinearDeltaPanel();") < 2:
    sys.exit("KILIT: secim-Enter dalinda panel cagrisi yok")

body_panel = panel.group()
body_apply = apply.group()

probe = r'''
#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
#include <vector>
#include <string>

struct Vec3 {
    double x{}, y{}, z{};
    Vec3 operator+(const Vec3& o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
    bool operator==(const Vec3&) const noexcept = default;
};

enum class TransformCommand { None, Move, Copy, Offset, Mirror, Delete, LinearArray,
                              PolarArray, Trim, Extend, Fillet, Rotate, Rotate3D,
                              CopyLinear, MoveLinear };
enum class TransformPhase { Selecting, BasePoint, RotateAxis, Destination };

constexpr int MB_ICONWARNING = 0;

struct Application {
    std::vector<std::size_t> selectedModels_;
    std::function<bool(Vec3&, bool&, bool)> linearDeltaRequest_;
    bool linearPickMode_{false};
    bool linearDeltaFromPick_{false};
    Vec3 lastLinearDelta_{0.0, 0.0, 0.0};
    TransformCommand transformCommand_{TransformCommand::None};
    TransformPhase transformPhase_{TransformPhase::Selecting};
    std::optional<Vec3> transformBase_;

    // --- fake sinir (gercek govde yok) ---
    std::vector<Vec3> committed;
    int cancels = 0;
    int beeps = 0;

    void commitTransformPoint(const Vec3& point) {
        committed.push_back(point);          // gercek govde: displacement = point - taban
        if (transformCommand_ == TransformCommand::MoveLinear) cancelTransformCommand();
    }
    void cancelTransformCommand() {
        ++cancels;
        transformCommand_ = TransformCommand::None;
        transformPhase_ = TransformPhase::Selecting;
        linearPickMode_ = false;
        linearDeltaFromPick_ = false;
        selectedModels_.clear();
    }
    void SetCursor(int) {}
    int currentCanvasCursor() const { return 0; }
    void updateControls() {}
    void updateStatus() {}
    void invalidateCanvas() {}
    void MessageBeep(int) { ++beeps; }

    bool requestLinearDeltaPanel();
    void applyLinearDelta(const Vec3& delta);
};

__BODY_PANEL__
__BODY_APPLY__

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "GECTI" : "KALDI", what);
    if (!ok) ++failures;
}

static Application make(TransformCommand cmd, std::size_t count = 1) {
    Application a;
    a.transformCommand_ = cmd;
    a.transformPhase_ = TransformPhase::BasePoint;
    a.selectedModels_.resize(count);
    for (std::size_t i = 0; i < count; ++i) a.selectedModels_[i] = i;
    return a;
}

int main() {
    // 1) Panel "Tamam": delta dogrudan uygulanir (kopya tek seferlik biter).
    {
        Application a = make(TransformCommand::CopyLinear, 2);
        bool asked = false;
        a.linearDeltaRequest_ = [&](Vec3& d, bool& pick, bool) {
            asked = true;
            pick = false;
            d = Vec3{1000.0, -250.0, 400.0};
            return true;
        };
        const bool ok = a.requestLinearDeltaPanel();
        check(ok, "A: panel Tamam -> true");
        check(asked, "A: panel kullaniciya soruldu");
        check(a.committed.size() == 1, "A: tek uygulama cagrisi");
        check(a.committed.size() == 1 && a.committed.front() == Vec3{1000.0, -250.0, 400.0},
              "A: yazilan X/Y/Z delta aynen uygulandi");
        check(a.transformCommand_ == TransformCommand::None, "A: islem sonrasi komut bitti");
        check(a.lastLinearDelta_ == Vec3{1000.0, -250.0, 400.0}, "A: delta hatirlandi (on-doldurma)");
    }
    // 2) Panel "Iki Nokta Sec": commit YOK, iki-tiklama modu + taban fazi.
    {
        Application a = make(TransformCommand::MoveLinear);
        a.linearDeltaRequest_ = [](Vec3&, bool& pick, bool) { pick = true; return true; };
        const bool ok = a.requestLinearDeltaPanel();
        check(ok, "B: iki nokta -> true");
        check(a.committed.empty(), "B: iki nokta modunda hemen islem yapilmaz");
        check(a.linearPickMode_, "B: pick modu acik");
        check(a.transformPhase_ == TransformPhase::BasePoint, "B: 1. nokta bekleniyor");
        check(!a.transformBase_, "B: taban henuz yok");
        check(a.transformCommand_ == TransformCommand::MoveLinear, "B: komut hala aktif");
    }
    // 3) Iptal: komut kapanir, hicbir sey uygulanmaz.
    {
        Application a = make(TransformCommand::CopyLinear);
        a.linearDeltaRequest_ = [](Vec3&, bool&, bool) { return false; };
        const bool ok = a.requestLinearDeltaPanel();
        check(!ok, "C: iptal -> false");
        check(a.committed.empty(), "C: iptalde islem yok");
        check(a.transformCommand_ == TransformCommand::None, "C: iptalde komut bitti");
    }
    // 4) Sifir delta: sessiz kopya yigini yok.
    {
        Application a = make(TransformCommand::CopyLinear);
        a.linearDeltaRequest_ = [](Vec3& d, bool& pick, bool) { pick = false; d = Vec3{}; return true; };
        a.requestLinearDeltaPanel();
        check(a.committed.empty(), "D: 0,0,0 delta -> islem yok");
        check(a.transformCommand_ == TransformCommand::None, "D: komut bitti");
    }
    // 5) MoveLinear ayni yoldan gecer.
    {
        Application a = make(TransformCommand::MoveLinear, 3);
        a.linearDeltaRequest_ = [](Vec3& d, bool&, bool) { d = Vec3{0.0, 0.0, -1500.0}; return true; };
        a.requestLinearDeltaPanel();
        check(a.committed.size() == 1, "E: tasima tek cagri");
        check(a.committed.size() == 1 && a.committed.front() == Vec3{0.0, 0.0, -1500.0},
              "E: Z yonunde tasima deltasi");
        check(a.transformCommand_ == TransformCommand::None, "E: tasima sonrasi komut bitti");
    }
    // 6) Secim yoksa panel acilmaz (bos kopya uretme).
    {
        Application a = make(TransformCommand::CopyLinear, 0);
        bool asked = false;
        a.linearDeltaRequest_ = [&](Vec3&, bool&, bool) { asked = true; return true; };
        const bool ok = a.requestLinearDeltaPanel();
        check(!ok && !asked, "F: secim yoksa panel istenmez");
        check(a.committed.empty(), "F: secim yoksa islem yok");
    }
    // 7) TABAN ORIJIN: displacement = nokta - taban kurali icin taban 0'a cekilir.
    {
        Application a = make(TransformCommand::CopyLinear);
        a.transformBase_ = Vec3{500.0, 500.0, 0.0};   // eski taban
        a.linearDeltaRequest_ = [](Vec3& d, bool&, bool) { d = Vec3{100.0, 0.0, 0.0}; return true; };
        a.requestLinearDeltaPanel();
        check(a.committed.size() == 1 && a.committed.front() == Vec3{100.0, 0.0, 0.0},
              "G: panel deltasi tabandan bagimsiz (X=100)");
    }
    // 8) OLCU MODU: "Iki Nokta Sec" islem YAPMAZ; iki nokta beklenir.
    {
        Application a = make(TransformCommand::CopyLinear);
        a.linearDeltaRequest_ = [](Vec3&, bool& pick, bool) { pick = true; return true; };
        a.requestLinearDeltaPanel();
        check(a.committed.empty() && a.linearPickMode_, "H: olcu modu islem yapmaz, nokta bekler");
        check(a.lastLinearDelta_ == Vec3{}, "H: olcuden once delta degismez");
    }
    // 9) OLCU SONRASI: panel olculen delta ile yeniden acilir (fromPicked=true).
    {
        Application a = make(TransformCommand::CopyLinear);
        a.committed.clear();
        bool sawFromPicked = false;
        int calls = 0;
        a.linearDeltaRequest_ = [&](Vec3& d, bool& pick, bool fromPicked) {
            ++calls;
            if (calls == 1) { pick = true; return true; }         // once olcu iste
            sawFromPicked = fromPicked;                            // ikinci acilis
            pick = false;
            d = a.lastLinearDelta_;                                // panel olcuyu gosterir
            return true;                                           // Uygula
        };
        a.lastLinearDelta_ = Vec3{};                               // temiz basla
        a.requestLinearDeltaPanel();                               // 1) panel -> olcu
        check(a.linearPickMode_, "I: olcu modu acildi");
        // Uygulama ici: commitTransformPoint olcum dalini calistirir (asagida taklit).
        a.lastLinearDelta_ = Vec3{1200.0, 0.0, -800.0};            // 2. nokta - 1. nokta
        a.linearPickMode_ = false;
        a.linearDeltaFromPick_ = true;
        a.transformPhase_ = TransformPhase::Selecting;
        a.requestLinearDeltaPanel();                               // 2) panel Uygula
        check(calls == 2, "I: panel ikinci kez acildi");
        check(sawFromPicked, "I: ikinci acilis 'olculen delta' bayragiyla");
        check(a.committed.size() == 1 && a.committed.front() == Vec3{1200.0, 0.0, -800.0},
              "I: Uygula -> olculen delta uygulandi");
    }
    std::printf(failures == 0 ? "TUM KONTROLLER GECTI\n" : "%d KONTROL KALDI\n", failures);
    return failures == 0 ? 0 : 1;
}
'''.replace("__BODY_PANEL__", body_panel).replace("__BODY_APPLY__", body_apply)

tmpdir = Path(tempfile.mkdtemp(prefix="mm_linear_panel_"))
probe_path = tmpdir / "probe.cpp"
probe_path.write_text(probe, encoding="utf-8")
exe = tmpdir / "probe"

print("== derleme")
compiled = subprocess.run(["g++", "-std=c++20", "-O1", "-Wall", "-Wextra",
                           str(probe_path), "-o", str(exe)],
                          capture_output=True, text=True)
if compiled.returncode != 0:
    print(compiled.stdout)
    print(compiled.stderr)
    sys.exit("derleme basarisiz")
if compiled.stderr.strip():
    print(compiled.stderr)

print("== calistirma")
run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=120)
print(run.stdout)
if run.stderr.strip():
    print(run.stderr[-2000:])
sys.exit(run.returncode)
