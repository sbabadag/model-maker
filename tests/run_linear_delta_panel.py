#!/usr/bin/env python3
"""Portable harness: the real Application delta-panel state machine + kopya adedi.

Extracts `requestLinearDeltaPanel`, `applyLinearDelta`, `copySelectionBy` and
`publishCopyStatus` VERBATIM from src/application.cpp (never reimplemented) and
links them against a minimal Application stub whose only fakes are
`commitTransformPoint` (the copy/move geometry body, already covered by the
ctest suite), a tiny Document double (records copy offsets) and the Win32/Qt
boundary (SetCursor/MessageBeep/update*).

Proves:
  1) panel "Uygula"      -> delta uygulanir, adet=1'de tek kopya,
  2) panel "Iki Nokta"   -> OLCU modu: islem YOK, nokta beklenir,
  3) olcu sonrasi panel  -> olculen delta ile yeniden acilir (fromPickedPoints),
  4) panel "Iptal"       -> komut kapanir, islem yok,
  5) sifir delta         -> islem yok,
  6) ADET=N              -> N kopya, ofsetler k*delta, TEK undo adimi,
  7) tasimada adet yok sayilir (tek tasima),
  8) secim yoksa panel istenmez,
  9) adet alt/ust siniri (0 -> 1, 5000 -> kLinearCopyMaxCount),
and locks the call sites + adet wiring by source pattern (silinirse sessizce bozulur).

Usage: python3 tests/run_linear_delta_panel.py [build-directory]
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
app = (root / "src/application.cpp").read_text(encoding="utf-8")


def grab(pattern: str, what: str) -> str:
    m = re.search(pattern, app, re.M | re.S)
    if not m:
        sys.exit(f"{what} bulunamadi (kaynak degismis)")
    return m.group()


panel = grab(r"^bool Application::requestLinearDeltaPanel\(.*?^}\n", "requestLinearDeltaPanel")
apply = grab(r"^void Application::applyLinearDelta\(.*?^}\n", "applyLinearDelta")
copy_step = grab(r"^Application::CopyStepInfo Application::copySelectionBy\(.*?^}\n", "copySelectionBy")
copy_status = grab(r"^void Application::publishCopyStatus\(.*?^}\n", "publishCopyStatus")

# --- KAYNAK KILITLERI ---
start_fn = grab(r"^void Application::startTransformCommand\(.*?^}\n", "startTransformCommand")
if "requestLinearDeltaPanel()" not in start_fn:
    sys.exit("KILIT: startTransformCommand paneli acmiyor")
commit_body = grab(r"^void Application::commitTransformPoint\(.*?^}\n", "commitTransformPoint")
if "linearDeltaFromPick_ = true;" not in commit_body or "requestLinearDeltaPanel();" not in commit_body:
    sys.exit("KILIT: iki nokta OLCUSU panele tasinmiyor (olcum islem yapmamali)")
if app.count("requestLinearDeltaPanel();") < 3:
    sys.exit("KILIT: panel cagri yerleri eksik (baslangic + secim + olcu)")
if "mm::linearCopyOffsets" not in apply:
    sys.exit("KILIT: ADET k*delta ofsetleri kullanilmiyor")
if apply.count("pushUndoSnapshot") != 1:
    sys.exit("KILIT: adetli kopya TEK undo kaydi acmali")

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

using EntityId = std::size_t;

enum class TransformCommand { None, Move, Copy, Offset, Mirror, Delete, LinearArray,
                              PolarArray, Trim, Extend, Fillet, Rotate, Rotate3D,
                              CopyLinear, MoveLinear };
enum class TransformPhase { Selecting, BasePoint, RotateAxis, Destination };

constexpr int MB_ICONWARNING = 0;

namespace mm {
struct LinearDeltaInput {
    Vec3 delta{};
    int count{1};
    bool pickTwoPoints{false};
    bool fromPickedPoints{false};
    bool allowCount{true};
};
constexpr int kLinearCopyMaxCount = 1000;
inline std::vector<Vec3> linearCopyOffsets(const Vec3& delta, int count) {
    std::vector<Vec3> offsets;
    if (count < 1) return offsets;
    const int limited = count > kLinearCopyMaxCount ? kLinearCopyMaxCount : count;
    for (int k = 1; k <= limited; ++k)
        offsets.push_back(Vec3{delta.x * k, delta.y * k, delta.z * k});
    return offsets;
}
} // namespace mm
using mm::LinearDeltaInput;

// Minimal Document double: kopya cagrilarini ve ofsetlerini kaydeder.
struct Document {
    std::vector<int> models_{0, 1};   // iki model (indeks)
    std::vector<int> instances_{};
    std::vector<int> bolts_{};
    std::vector<Vec3> copyOffsets;

    const std::vector<int>& models() const { return models_; }
    const std::vector<int>& componentInstances() const { return instances_; }
    const std::vector<int>& boltGroups() const { return bolts_; }
    EntityId modelId(std::size_t index) const { return index; }
    void copyModels(const std::vector<std::size_t>& indices, const Vec3& displacement) {
        copyOffsets.push_back(displacement);
        for (std::size_t i = 0; i < indices.size(); ++i)
            models_.push_back(static_cast<int>(models_.size()));
    }
};

// UCS DELTA DONUSUMU (2026-10-31): applyLinearDelta artik panel deltasini
// (is duzlemi eksenleri) dunya vektorune cevirir. Harness'ta geometri
// cekirdegi yok; ayni sozlesmeyi burada taklit ediyoruz.
namespace mm {
struct WorkPlane {
    ::Vec3 u{1.0, 0.0, 0.0};
    ::Vec3 v{0.0, 1.0, 0.0};
    ::Vec3 normal{0.0, 0.0, 1.0};
};
inline Vec3 workPlaneDeltaToWorld(const WorkPlane& p, const Vec3& d) noexcept {
    return Vec3{p.u.x * d.x + p.v.x * d.y + p.normal.x * d.z,
                p.u.y * d.x + p.v.y * d.y + p.normal.y * d.z,
                p.u.z * d.x + p.v.z * d.y + p.normal.z * d.z};
}
inline Vec3 worldDeltaToWorkPlane(const WorkPlane& p, const Vec3& d) noexcept {
    return Vec3{p.u.x * d.x + p.u.y * d.y + p.u.z * d.z,
                p.v.x * d.x + p.v.y * d.y + p.v.z * d.z,
                p.normal.x * d.x + p.normal.y * d.y + p.normal.z * d.z};
}
} // namespace mm

struct Application {
    mm::WorkPlane workPlane_;
    Document document_;
    std::vector<std::size_t> selectedModels_;
    std::vector<std::size_t> pendingCopySelection_;
    std::function<bool(LinearDeltaInput&)> linearDeltaRequest_;
    bool linearPickMode_{false};
    bool linearDeltaFromPick_{false};
    Vec3 lastLinearDelta_{0.0, 0.0, 0.0};
    int lastLinearCount_{1};
    TransformCommand transformCommand_{TransformCommand::None};
    TransformPhase transformPhase_{TransformPhase::Selecting};
    std::optional<Vec3> transformBase_;

    // --- fake sinir (gercek govde yok) ---
    std::vector<Vec3> committed;
    int undos = 0;
    int cancels = 0;
    int beeps = 0;
    std::wstring lastStatus;

    void commitTransformPoint(const Vec3& point) { committed.push_back(point); }
    void pushUndoSnapshot() { ++undos; }
    std::size_t rebindCopiedComponents(std::size_t, const std::vector<EntityId>&) { return 0; }
    void publishStatus(const std::wstring& text) { lastStatus = text; }
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
    void applyLinearDelta(const Vec3& delta, int count);
    struct CopyStepInfo { std::size_t models{}, instances{}, bolts{}, rebound{}; };
    CopyStepInfo copySelectionBy(const Vec3& displacement);
    void publishCopyStatus(const CopyStepInfo& info, int copies);
};

__PANEL__
__APPLY__
__COPYSTEP__
__COPYSTATUS__

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
    // A) Panel "Uygula": delta aynen, adet=1 -> tek kopya.
    {
        Application a = make(TransformCommand::CopyLinear, 2);
        bool asked = false;
        a.linearDeltaRequest_ = [&](LinearDeltaInput& in) {
            asked = true;
            in.delta = Vec3{1000.0, -250.0, 400.0};
            in.count = 1;
            return true;
        };
        check(a.requestLinearDeltaPanel(), "A: panel Uygula -> true");
        check(asked, "A: panel kullaniciya soruldu");
        check(a.document_.copyOffsets.size() == 1, "A: adet=1 -> tek kopya");
        check(a.document_.copyOffsets.size() == 1 &&
              a.document_.copyOffsets.front() == Vec3{1000.0, -250.0, 400.0},
              "A: yazilan X/Y/Z delta aynen uygulandi");
        check(a.transformCommand_ == TransformCommand::None, "A: islem sonrasi komut bitti");
        check(a.lastLinearDelta_ == Vec3{1000.0, -250.0, 400.0}, "A: delta hatirlandi (on-doldurma)");
    }
    // B) OLCU MODU: "Iki Nokta Sec" islem YAPMAZ; iki nokta beklenir.
    {
        Application a = make(TransformCommand::MoveLinear);
        a.linearDeltaRequest_ = [](LinearDeltaInput& in) { in.pickTwoPoints = true; return true; };
        check(a.requestLinearDeltaPanel(), "B: iki nokta -> true");
        check(a.committed.empty() && a.document_.copyOffsets.empty(),
              "B: olcu modunda hemen islem yapilmaz");
        check(a.linearPickMode_, "B: pick modu acik");
        check(a.transformPhase_ == TransformPhase::BasePoint, "B: 1. nokta bekleniyor");
        check(!a.transformBase_, "B: taban henuz yok");
        check(a.transformCommand_ == TransformCommand::MoveLinear, "B: komut hala aktif");
    }
    // C) Iptal.
    {
        Application a = make(TransformCommand::CopyLinear);
        a.linearDeltaRequest_ = [](LinearDeltaInput&) { return false; };
        check(!a.requestLinearDeltaPanel(), "C: iptal -> false");
        check(a.committed.empty() && a.document_.copyOffsets.empty(), "C: iptalde islem yok");
        check(a.transformCommand_ == TransformCommand::None, "C: iptalde komut bitti");
    }
    // D) Sifir delta.
    {
        Application a = make(TransformCommand::CopyLinear);
        a.linearDeltaRequest_ = [](LinearDeltaInput& in) { in.delta = Vec3{}; in.count = 5; return true; };
        a.requestLinearDeltaPanel();
        check(a.document_.copyOffsets.empty(), "D: 0,0,0 delta -> islem yok");
        check(a.transformCommand_ == TransformCommand::None, "D: komut bitti");
    }
    // E) TASIMA: adet yok sayilir, tek commit.
    {
        Application a = make(TransformCommand::MoveLinear, 3);
        a.linearDeltaRequest_ = [](LinearDeltaInput& in) {
            in.delta = Vec3{0.0, 0.0, -1500.0};
            in.count = 7;               // tasimada yok sayilmali
            return true;
        };
        a.requestLinearDeltaPanel();
        check(a.committed.size() == 1, "E: tasima tek cagri");
        check(a.committed.size() == 1 && a.committed.front() == Vec3{0.0, 0.0, -1500.0},
              "E: Z yonunde tasima deltasi");
        check(a.document_.copyOffsets.empty(), "E: tasimada kopya yok");
        check(a.transformCommand_ == TransformCommand::None, "E: tasima sonrasi komut bitti");
    }
    // F) Secim yoksa panel istenmez.
    {
        Application a = make(TransformCommand::CopyLinear, 0);
        bool asked = false;
        a.linearDeltaRequest_ = [&](LinearDeltaInput&) { asked = true; return true; };
        check(!a.requestLinearDeltaPanel() && !asked, "F: secim yoksa panel istenmez");
    }
    // G) ADET=3: ofsetler delta, 2*delta, 3*delta; TEK undo.
    {
        Application a = make(TransformCommand::CopyLinear, 2);
        a.linearDeltaRequest_ = [](LinearDeltaInput& in) {
            in.delta = Vec3{500.0, 0.0, 0.0};
            in.count = 3;
            return true;
        };
        a.requestLinearDeltaPanel();
        const auto& offsets = a.document_.copyOffsets;
        check(offsets.size() == 3, "G: adet=3 -> 3 kopya adimi");
        check(offsets.size() == 3 && offsets[0] == Vec3{500.0, 0.0, 0.0} &&
              offsets[1] == Vec3{1000.0, 0.0, 0.0} && offsets[2] == Vec3{1500.0, 0.0, 0.0},
              "G: ofsetler k*delta (500 / 1000 / 1500)");
        check(a.undos == 1, "G: adetli kopya tek undo adimi");
        check(a.lastLinearCount_ == 3, "G: adet hatirlandi (on-doldurma)");
        check(a.document_.models().size() == 2 + 2 * 3, "G: 2 secili x 3 adet = 6 yeni model");
        check(!a.pendingCopySelection_.empty(), "G: secim uretilen kopyalara gecer");
    }
    // H) Adet 0 -> 1 kopya (guvenli taban).
    {
        Application a = make(TransformCommand::CopyLinear, 1);
        a.linearDeltaRequest_ = [](LinearDeltaInput& in) {
            in.delta = Vec3{100.0, 0.0, 0.0};
            in.count = 0;
            return true;
        };
        a.requestLinearDeltaPanel();
        check(a.document_.copyOffsets.size() == 1, "H: adet=0 -> tek kopya");
    }
    // I) Adet ust sinir kirpma.
    {
        Application a = make(TransformCommand::CopyLinear, 1);
        a.linearDeltaRequest_ = [](LinearDeltaInput& in) {
            in.delta = Vec3{1.0, 0.0, 0.0};
            in.count = 5000;
            return true;
        };
        a.requestLinearDeltaPanel();
        check(a.document_.copyOffsets.size() == static_cast<std::size_t>(mm::kLinearCopyMaxCount),
              "I: adet ust sinirla kirpildi");
    }
    // J) OLCU SONRASI: panel olculen delta ile yeniden acilir (fromPickedPoints).
    {
        Application a = make(TransformCommand::CopyLinear, 1);
        bool sawFromPicked = false;
        a.lastLinearDelta_ = Vec3{1200.0, 0.0, -800.0};   // 2. nokta - 1. nokta
        a.linearDeltaFromPick_ = true;
        a.transformPhase_ = TransformPhase::Selecting;
        a.linearDeltaRequest_ = [&](LinearDeltaInput& in) {
            sawFromPicked = in.fromPickedPoints;
            in.count = 1;
            return true;
        };
        a.requestLinearDeltaPanel();
        check(sawFromPicked, "J: panel 'olculen delta' bayragiyla acildi");
        check(a.document_.copyOffsets.size() == 1 &&
              a.document_.copyOffsets.front() == Vec3{1200.0, 0.0, -800.0},
              "J: Uygula -> olculen delta uygulandi");
    }
    std::printf(failures == 0 ? "TUM KONTROLLER GECTI\n" : "%d KONTROL KALDI\n", failures);
    return failures == 0 ? 0 : 1;
}
'''.replace("__PANEL__", panel).replace("__APPLY__", apply) \
   .replace("__COPYSTEP__", copy_step).replace("__COPYSTATUS__", copy_status)

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
