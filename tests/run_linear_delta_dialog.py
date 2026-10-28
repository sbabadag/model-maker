#!/usr/bin/env python3
"""Portable probe: Qt delta panel code from src/qt_main_window.cpp, verbatim.

Extracts `promptLinearDeltaDialog` from the real source (no copy/reimplementation)
and runs it against real Qt Widgets (offscreen) to prove:
  1) the panel builds without crashing,
  2) X/Y/Z (ve Adet) alanlari Application'in son degerleriyle on-dolu gelir,
  3) "Uygula" yazilan delta + adet degerini dondurur,
  4) "İki Nokta Seç" pickTwoPoints=true dondurur, delta'ya dokunmaz,
  5) "İptal" false dondurur (Application komutu iptal eder),
  6) TASIMADA "Adet" satiri gizlenir (allowCount=false) ve adet degismez,
  7) olcu sonrasi acilis (fromPickedPoints=true) olculen delta'yi aynen tasir.

This does NOT prove Windows rendering; it proves the widget code compiles and
its return contract is exactly what Application expects.
Usage: python3 tests/run_linear_delta_dialog.py [build-directory]
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"
src = (root / "src/qt_main_window.cpp").read_text(encoding="utf-8")

match = re.search(r"^bool promptLinearDeltaDialog\(.*?^}\n", src, re.M | re.S)
if not match:
    sys.exit("promptLinearDeltaDialog bulunamadi (kaynak degismis)")
body = match.group()

probe = r'''
#include <QApplication>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <cstdio>
#include <cmath>
#include <string>

struct Vec3 {
    double x{}, y{}, z{};
    Vec3 operator+(const Vec3& o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
    bool operator==(const Vec3&) const noexcept = default;
};

namespace mm {
struct LinearDeltaInput {
    Vec3 delta{};
    int count{1};
    bool pickTwoPoints{false};
    bool fromPickedPoints{false};
    bool allowCount{true};
};
constexpr int kLinearCopyMaxCount = 1000;
} // namespace mm
using mm::LinearDeltaInput;

__BODY__

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "GECTI" : "KALDI", what);
    if (!ok) ++failures;
}

// Panel exec() icinde calisir; secilen dugmeye gore otomatik yanit ver.
struct AutoResponder {
    std::string button;      // QPushButton metni ("" -> dogrudan accept)
    double x{}, y{}, z{};
    int count = 0;           // 0 -> adet alanina dokunma
    bool setValues = false;
    bool sawDialog = false;
    bool countVisible = true;
};

static void respond(AutoResponder* r) {
    for (QWidget* w : QApplication::topLevelWidgets()) {
        auto* dlg = qobject_cast<QDialog*>(w);
        if (!dlg || !dlg->isVisible()) continue;
        r->sawDialog = true;
        if (r->setValues) {
            const auto spins = dlg->findChildren<QDoubleSpinBox*>();
            if (spins.size() == 3) {
                spins[0]->setValue(r->x);
                spins[1]->setValue(r->y);
                spins[2]->setValue(r->z);
            } else {
                std::printf("KALDI  panelde 3 spin yok (%lld)\n",
                            static_cast<long long>(spins.size()));
                ++failures;
            }
        }
        const auto counts = dlg->findChildren<QSpinBox*>();
        if (counts.size() == 1) {
            r->countVisible = counts.front()->isVisible();
            if (r->count > 0) counts.front()->setValue(r->count);
        } else {
            std::printf("KALDI  panelde tek 'Adet' spin yok (%lld)\n",
                        static_cast<long long>(counts.size()));
            ++failures;
        }
        if (!r->button.empty()) {
            bool clicked = false;
            for (QPushButton* b : dlg->findChildren<QPushButton*>()) {
                if (b->text() == QString::fromStdString(r->button)) { b->click(); clicked = true; break; }
            }
            if (!clicked) { std::printf("KALDI  dugme bulunamadi\n"); ++failures; dlg->reject(); }
            return;
        }
        dlg->accept();
        return;
    }
    ++failures;
    std::printf("KALDI  panel penceresi bulunamadi\n");
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // A) Kopya: Uygula -> delta + adet okunur, Adet satiri gorunur.
    {
        AutoResponder r;
        r.setValues = true;
        r.x = 1250.0; r.y = -500.0; r.z = 300.0;
        r.count = 4;
        QTimer::singleShot(0, [&r]() { respond(&r); });
        LinearDeltaInput in;
        in.delta = Vec3{10.0, 20.0, 30.0};
        in.count = 2;
        in.allowCount = true;
        in.pickTwoPoints = true;   // Uygula secilirse false'a cekilmeli
        const bool ok = promptLinearDeltaDialog(nullptr, in);
        check(ok, "A: Uygula -> true dondu");
        check(r.sawDialog, "A: panel gercekten acildi");
        check(!in.pickTwoPoints, "A: Uygula secildiginde pick=false");
        check(in.delta == Vec3{1250.0, -500.0, 300.0}, "A: X/Y/Z alanlarindan okunan delta");
        check(in.count == 4, "A: Adet alanindan okunan kopya sayisi");
        check(r.countVisible, "A: kopyada Adet satiri gorunur");
    }
    // B) Iki Nokta Sec: pick=true, degerler korunur.
    {
        AutoResponder r;
        r.button = "\xc4\xb0ki Nokta Se\xc3\xa7";   // "İki Nokta Seç" (UTF-8)
        QTimer::singleShot(0, [&r]() { respond(&r); });
        LinearDeltaInput in;
        in.delta = Vec3{7.0, 8.0, 9.0};
        in.count = 3;
        const bool ok = promptLinearDeltaDialog(nullptr, in);
        check(ok, "B: Iki Nokta Sec -> true dondu");
        check(in.pickTwoPoints, "B: pickTwoPoints=true");
        check(in.delta == Vec3{7.0, 8.0, 9.0}, "B: delta degismedi (olcu modunda)");
        check(in.count == 3, "B: adet degismedi");
    }
    // C) Iptal.
    {
        AutoResponder r;
        r.button = "\xc4\xb0ptal";                  // "İptal"
        QTimer::singleShot(0, [&r]() { respond(&r); });
        LinearDeltaInput in;
        in.delta = Vec3{1.0, 2.0, 3.0};
        const bool ok = promptLinearDeltaDialog(nullptr, in);
        check(!ok, "C: Iptal -> false dondu");
        check(in.delta == Vec3{1.0, 2.0, 3.0}, "C: iptalde delta korunur");
    }
    // D) On-doldurma: onceki delta + adet panele yazilir.
    {
        AutoResponder r;
        QTimer::singleShot(0, [&r]() { respond(&r); });
        LinearDeltaInput in;
        in.delta = Vec3{-40.0, 0.0, 15.0};
        in.count = 6;
        const bool ok = promptLinearDeltaDialog(nullptr, in);
        check(ok, "D: on-doldurma ile Uygula -> true");
        check(in.delta == Vec3{-40.0, 0.0, 15.0}, "D: on-doldurulan delta geri okundu");
        check(in.count == 6, "D: on-doldurulan adet geri okundu");
    }
    // E) Olcu sonrasi acilis: olculen delta aynen doner.
    {
        AutoResponder r;
        QTimer::singleShot(0, [&r]() { respond(&r); });
        LinearDeltaInput in;
        in.delta = Vec3{2500.0, -750.0, 500.0};
        in.fromPickedPoints = true;
        const bool ok = promptLinearDeltaDialog(nullptr, in);
        check(ok, "E: olcu sonrasi panel Uygula -> true");
        check(!in.pickTwoPoints, "E: olcu sonrasi panelde pick=false");
        check(in.delta == Vec3{2500.0, -750.0, 500.0}, "E: olculen delta Uygula ile aynen dondu");
    }
    // F) TASIMA: Adet satiri gizli, adet degismez.
    {
        AutoResponder r;
        r.count = 9;              // gorunmese de yazilir;Application yok sayar
        QTimer::singleShot(0, [&r]() { respond(&r); });
        LinearDeltaInput in;
        in.delta = Vec3{0.0, 0.0, -1500.0};
        in.count = 1;
        in.allowCount = false;
        const bool ok = promptLinearDeltaDialog(nullptr, in);
        check(ok, "F: tasima paneli -> true");
        check(!r.countVisible, "F: tasimada Adet satiri gizli");
        check(in.count == 1, "F: tasimada adet degismez");
        check(in.delta == Vec3{0.0, 0.0, -1500.0}, "F: tasima deltasi okundu");
    }
    std::printf(failures == 0 ? "TUM KONTROLLER GECTI\n" : "%d KONTROL KALDI\n", failures);
    return failures == 0 ? 0 : 1;
}
'''.replace("__BODY__", body)

tmpdir = Path(tempfile.mkdtemp(prefix="mm_linear_delta_"))
probe_path = tmpdir / "probe.cpp"
probe_path.write_text(probe, encoding="utf-8")

cflags = subprocess.run(["pkg-config", "--cflags", "Qt6Widgets"],
                        capture_output=True, text=True, check=True).stdout.split()
libs = subprocess.run(["pkg-config", "--libs", "Qt6Widgets"],
                      capture_output=True, text=True, check=True).stdout.split()
exe = tmpdir / "probe"

env = os.environ.copy()
env["QT_QPA_PLATFORM"] = "offscreen"

print("== derleme")
compile_cmd = ["g++", "-std=c++20", "-O1", "-fPIC", str(probe_path), "-o", str(exe), *cflags, *libs]
compile_result = subprocess.run(compile_cmd, capture_output=True, text=True)
if compile_result.returncode != 0:
    print(compile_result.stdout)
    print(compile_result.stderr)
    sys.exit("derleme basarisiz")

print("== calistirma (offscreen)")
run = subprocess.run([str(exe)], capture_output=True, text=True, env=env, timeout=120)
print(run.stdout)
if run.stderr.strip():
    print(run.stderr[-2000:])
sys.exit(run.returncode)
