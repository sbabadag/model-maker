#!/usr/bin/env python3
"""KAYIT/OKUMA GUARD kablolama testi (kaynak kilidi).

Kullanici bildirimi: "kaydedilen portal dosyasinda sonradan attigim capraz
elemanlar da yuklendiginde farkli yerlerde cikti — kayit/okuma da bir problem
var". MMW8 mesh'i + profilin parametrelerini birlikte saklar; Application::
openDocument her profilli model icin recomputeCutTarget cagirip mesh'i
PARAMETRELERDEN yeniden uretir. Mesh ile parametreler ayrisMissA eleman
acilista kayar.

Bu test iki seyi kilitler:
  (1) acilista kaydedilmis bbox saklanir ve yeniden uretim onu toleransin
      uzerinde oynatiyorsa KAYIT korunur (reload_mesh_guard.hpp);
  (2) parametrelerden yeniden ureten TUM yollar kesit anchor'ini da gecirir
      (creation ile regeneration ayni kesit yerlesimini kullanir).
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
failures = []


def check(condition, label):
    print(("  ok   " if condition else "  FAIL ") + label)
    if not condition:
        failures.append(label)


guard = (ROOT / "include/model_maker/reload_mesh_guard.hpp").read_text()
app = (ROOT / "src/application.cpp").read_text()

print("reload mesh guard kablolama:")

# --- (1) cekirdek sozlesme ------------------------------------------------
check("struct MeshExtent" in guard, "guard: MeshExtent tanimli")
check("reloadKeepsSavedMesh" in guard, "guard: reloadKeepsSavedMesh var")
check("toleranceMm = 0.5" in guard, "guard: varsayilan tolerans 0.5 mm")
check("return false;" in guard.split("reloadKeepsSavedMesh")[1][:400],
      "guard: gecersiz extent -> kaydedilen korunmaz")

# --- (2) Application baglantisi ------------------------------------------
check('#include "model_maker/reload_mesh_guard.hpp"' in app,
      "application.cpp guard basligini include ediyor")

open_doc = app.split("void Application::openDocument()")[1]
open_doc = open_doc.split("\nvoid Application::")[0]
check("const mm::WireframeModel savedModel = document_.models()[i];" in open_doc,
      "acilis: kaydedilmis model kopyalanir")
check("const mm::MeshExtent savedExtent = mm::meshExtent(savedModel.vertices());" in open_doc,
      "acilis: kaydedilmis bbox olculur")
check("mm::reloadKeepsSavedMesh(savedExtent" in open_doc,
      "acilis: guard karari kullanilir")
check("document_.replaceModel(*idxOpt, {std::move(restored)});" in open_doc,
      "acilis: kaydedilen geometri geri yazilir")
check("LOAD-KEEP-SAVED-MESH" in open_doc,
      "acilis: korunan eleman sayisi log'a yazilir")
check("solidStatusMessage_" in open_doc,
      "acilis: kullaniciya kalici durum mesaji verilir")
# Kaydedilen kopya yeniden uretimden ONCE alinmali.
saved_pos = open_doc.find("const mm::WireframeModel savedModel")
regen_pos = open_doc.find("recomputeCutTarget(savedModelId)")
check(saved_pos != -1 and regen_pos != -1 and saved_pos < regen_pos,
      "acilis: kopya recompute'tan ONCE alinir")

# --- (3) anchor tutarliligi ----------------------------------------------
def function_body(name):
    marker = "Application::" + name + "("
    idx = app.find(marker)
    if idx < 0:
        return ""
    start = app.rfind("\n", 0, app.rfind("\n", 0, idx))  # signature line
    end = app.find("\n}\n", idx)
    return app[start:end]

for fn in ("ensureCutSolid", "rebuildCutBaseSolid", "reExtrudeProfileGripCore"):
    body = function_body(fn)
    check(body != "", fn + ": govde bulundu")
    check("props.sectionAnchor" in body,
          fn + ": kesit anchor'i extrude'a gecirilir")

# Kaydedilmis parametrelerden ureten tum 5-argumanli cagrilar anchor'siz kalmamali.
calls = re.findall(r"extrudeProfileSolid\(\s*\*profile,\s*from,\s*to,\s*props\.profileRotation,\s*"
                   r"Vec3\{props\.sectionNormalX,\s*props\.sectionNormalY,\s*"
                   r"props\.sectionNormalZ\}([^;]*)\)", app, re.S)
check(len(calls) >= 1, "parametrelerden ureten cagrilar bulundu")
check(all("sectionAnchor" in c for c in calls),
      "parametrelerden ureten cagrilarin hepsi anchor gecirir")

if failures:
    print("reload mesh guard kablolama: %d FAILED" % len(failures))
    sys.exit(1)
print("reload mesh guard kablolama: tum kontroller gecti")
