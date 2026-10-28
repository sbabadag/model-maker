#!/usr/bin/env python3
"""Portable probe: Qt delta panel code from src/qt_main_window.cpp, verbatim.

Extracts `promptLinearDeltaDialog` from the real source (no copy/reimplementation)
and runs it against real Qt Widgets (offscreen) to prove:
  1) the panel builds without crashing,
  2) X/Y/Z spin values are pre-filled from the Application's last delta,
  3) "Uygula" returns the typed delta,
  4) "İki Nokta Seç" returns pickTwoPoints=true and leaves delta untouched,
  5) "İptal" returns false (Application then cancels the command).

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
    bool setValues = false;
    bool sawDialog = false;
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
    {
        AutoResponder r;
        r.setValues = true;
        r.x = 1250.0; r.y = -500.0; r.z = 300.0;
        QTimer::singleShot(0, [&r]() { respond(&r); });
        Vec3 delta{10.0, 20.0, 30.0};
        bool pick = true;   // panel Uygula derse false'a cekilmeli
        const bool ok = promptLinearDeltaDialog(nullptr, delta, pick, false);
        check(ok, "A: Uygula -> true dondu");
        check(r.sawDialog, "A: panel gercekten acildi");
        check(!pick, "A: Uygula secildiginde pick=false");
        check(delta == Vec3{1250.0, -500.0, 300.0}, "A: X/Y/Z alanlarindan okunan delta");
    }
    {
        Vec3 delta{7.0, 8.0, 9.0};
        bool pick = false;
        AutoResponder r;
        r.button = "\xc4\xb0ki Nokta Se\xc3\xa7";   // "İki Nokta Seç" (UTF-8)
        QTimer::singleShot(0, [&r]() { respond(&r); });
        // Karsilastirma QString::fromStdString ile yapilir -> UTF-8 ayni olmali.
        const bool ok = promptLinearDeltaDialog(nullptr, delta, pick, false);
        check(ok, "B: Iki Nokta Sec -> true dondu");
        check(pick, "B: pickTwoPoints=true");
        check(delta == Vec3{7.0, 8.0, 9.0}, "B: delta degismedi (iki nokta modunda)");
    }
    {
        Vec3 delta{1.0, 2.0, 3.0};
        bool pick = false;
        AutoResponder r;
        r.button = "\xc4\xb0ptal";                  // "İptal"
        QTimer::singleShot(0, [&r]() { respond(&r); });
        const bool ok = promptLinearDeltaDialog(nullptr, delta, pick, false);
        check(!ok, "C: Iptal -> false dondu");
        check(delta == Vec3{1.0, 2.0, 3.0}, "C: iptalde delta korunur");
    }
    {
        // On-doldurma: panel onceki deltayi gostermeli (kullanici tekrar yazmasin).
        Vec3 delta{-40.0, 0.0, 15.0};
        bool pick = false;
        AutoResponder r;
        QTimer::singleShot(0, [&r]() { respond(&r); });
        const bool ok = promptLinearDeltaDialog(nullptr, delta, pick, false);
        check(ok, "D: on-doldurma ile Uygula -> true");
        check(delta == Vec3{-40.0, 0.0, 15.0}, "D: on-doldurulan deger geri okundu");
    }
    {
        // E: OLCU SONRASI panel (fromPickedPoints=true): degerler OLÇULEN delta
        // olmali ve Uygula'ya basilmadan islem yapilmaz (Application tarafi
        // applyLinearDelta'yi yalniz true donunce cagirir).
        Vec3 delta{2500.0, -750.0, 500.0};
        bool pick = false;
        AutoResponder r;
        QTimer::singleShot(0, [&r]() { respond(&r); });
        const bool ok = promptLinearDeltaDialog(nullptr, delta, pick, true);
        check(ok, "E: olcu sonrasi panel Uygula -> true");
        check(!pick, "E: olcu sonrasi panelde pick=false");
        check(delta == Vec3{2500.0, -750.0, 500.0}, "E: olculen delta Uygula ile aynen dondu");
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
