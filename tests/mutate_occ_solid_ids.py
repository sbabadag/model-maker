#!/usr/bin/env python3
"""NEGATIF KONTROLLER — OCC sahiplik/silindir harness'inin gercekten
yakaladigini kanitlar.

Her mutasyon uretim kaynagina GECICI olarak uygulanir, harness CALISTIRILIR ve
ilgili testin GERCEKTEN dustugu gosterilir; sonra dosya(lar) bayt-birebir geri
yazilir (sha256 ile dogrulanir). Mutasyonlar kalici DEGILDIR.

Mutasyonlar:
  NC1 (off-by-one)      : addCube sekli, `size() - 1` yerine `size() - 2` ile
                          anahtarlanir -> Test 1 (dogru EntityId<->sekil) DUSER.
  NC2 (operand tuketimi): addBooleanFuse operandlari sonuc anahtarlanmadan ONCE
                          silinir -> Test 6 (operand korunmasi) DUSER.
  NC3 (silindir dilimi) : Application::addCylinder 3-arg (R,H,Angle) cagirir ->
                          tam-silindir hacim/bbox testleri DUSER.
  NC4 (yardimci solid)  : solidCylinderSolid MakeCylinder(radius,radius,height) ->
                          Test 7 bbox yuksekligi DUSER.
  NC5 (yardimci hacim)  : solidCylinderVolume MakeCylinder(radius,radius,height) ->
                          Test 7 analitik hacim DUSER.
  NC6 (yanlis surum)    : store.find() aktif surum yerine EN ESKI surumu doner ->
                          Test 8 (find = aktif surum) DUSER.
  NC7 (surum kaybi)     : store.set() eski surumlerin UZERINE yazar ->
                          Test 8 (onceki surum korunur) DUSER.

Kullanim: mutate_occ_solid_ids.py <build_dir> [--cxx ...] [--occt-include ...]
"""
import hashlib
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import run_occ_solid_ids as h  # ayni cikarim mantigi (tek kaynak)

APP_CPP = h.APP_CPP
OCC_CPP = os.path.join(h.ROOT, "src", "occ_geometry.cpp")
STORE_CPP = os.path.join(h.ROOT, "src", "occ_solid_store.cpp")
ENTITY_ID_HPP = os.path.join(h.ROOT, "include", "model_maker", "entity_id.hpp")

# Mutasyonlarin dokunabilecegi uretim dosyalari (yedek/geri yazma kapsami).
ALL_FILES = (APP_CPP, OCC_CPP, STORE_CPP, ENTITY_ID_HPP)


def sha(path):
    with open(path, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


def apply_in_span(text, fn_sig, name, old, new):
    """`name` govdesinin ARALIGI icinde ilk `old` -> `new` degisimini uygular."""
    i, k = h.find_span(text, fn_sig, name)
    span = text[i:k]
    if old not in span:
        raise h.HarnessError(f"{name}: mutasyon hedefi bulunamadi -> {old!r}")
    new_span = span.replace(old, new, 1)
    return text[:i] + new_span + text[k:]


CUBE_KEY_OLD = "putOccSolid(document_.models().size() - 1, shape);"
CUBE_KEY_NEW = "putOccSolid(document_.models().size() - 2, shape);"

FUSE_KEY_OLD = "putOccSolid(document_.models().size() - 1, result);"
# Operandlari erken "tuketir": belge-sahipli magazayi sonucu yazmadan ONCE
# temizler (magaza uzerinden budama — eski map erase'in karsiligi).
FUSE_KEY_NEW = ("if (auto* __s = occSolidStore()) __s->clear(); "
                "putOccSolid(document_.models().size() - 1, result);")

CYL_CALL_OLD = "BRepPrimAPI_MakeCylinder(1.3, 3.0).Shape()"
CYL_CALL_NEW = "BRepPrimAPI_MakeCylinder(1.3, 1.3, 3.0).Shape()"

HELPER_SOLID_OLD = "BRepPrimAPI_MakeCylinder(radius, height).Shape()"
HELPER_SOLID_NEW = "BRepPrimAPI_MakeCylinder(radius, radius, height).Shape()"

# NOT: ayni metin iki yardimci govdede de geciyor; apply_in_span ilgili govde
# araligina sinirladigi icin her biri tek basina hedeflenir.
MUTATIONS = [
    {
        "id": "NC1",
        "desc": "off-by-one: addCube sekli size()-2 ile anahtarlar",
        "file": APP_CPP,
        "fn": ("addCube", "void Application::addCube()"),
        "old": CUBE_KEY_OLD, "new": CUBE_KEY_NEW,
        "expect": ["FAIL cube id still holds the box",
                   "FAIL store has cube id"],
    },
    {
        "id": "NC2",
        "desc": "boolean operandlarini erken siler (tuketir)",
        "file": APP_CPP,
        "fn": ("addBooleanFuse", "void Application::addBooleanFuse()"),
        "old": FUSE_KEY_OLD, "new": FUSE_KEY_NEW,
        "expect": ["FAIL fuse: operand cube STILL in store",
                   "FAIL fuse: operand cylinder STILL in store",
                   "FAIL fuse operands preserved in store"],
    },
    {
        "id": "NC3",
        "desc": "addCylinder 3-arg (R,H,Angle) dilime doner",
        "file": APP_CPP,
        "fn": ("addCylinder", "void Application::addCylinder()"),
        "old": CYL_CALL_OLD, "new": CYL_CALL_NEW,
        "expect": ["FAIL cylinder = full cylinder pi*r^2*h",
                   "FAIL cylinder bbox Y spans full diameter"],
    },
    {
        "id": "NC4",
        "desc": "solidCylinderSolid MakeCylinder(radius,radius,height) (dilim)",
        "file": OCC_CPP,
        "fn": ("solidCylinderSolid",
               "WireframeModel solidCylinderSolid(double radius, double height, "
               "double faceDeflection,"),
        "old": HELPER_SOLID_OLD, "new": HELPER_SOLID_NEW,
        "expect": ["FAIL solidCylinderSolid bbox height"],
    },
    {
        "id": "NC5",
        "desc": "solidCylinderVolume MakeCylinder(radius,radius,height) (dilim)",
        "file": OCC_CPP,
        "fn": ("solidCylinderVolume",
               "double solidCylinderVolume(double radius, double height)"),
        "old": HELPER_SOLID_OLD, "new": HELPER_SOLID_NEW,
        "expect": ["FAIL solidCylinderVolume(1.5,5.0)"],
    },
    # --- Phase 3.2A: SURUM davranisi ---
    {
        # Aktif surum yerine EN ESKI surumu dondurur: ayni kimlik altinda yeni
        # geometri yazildiktan sonra find() yanlis (eski) geometriyi verir.
        "id": "NC6",
        "desc": "find() aktif surum yerine EN ESKI surumu doner",
        "file": STORE_CPP,
        "fn": ("find", "const TopoDS_Shape* OccSolidStore::find(EntityId id) const"),
        "old": "return shapeOfVersion(it->second, it->second.active);",
        "new": "return it->second.versions.empty() ? nullptr : "
               "&it->second.versions.front().shape;",
        "expect": ["FAIL find() = active = small box (vol 1.0), not the old cube"],
    },
    {
        # Eski surumlerin UZERINE yazar (versiyonlama kaybolur): "onceki surum
        # korunur" sozu cignenir.
        "id": "NC7",
        "desc": "set() eski surumlerin uzerine yazar (versiyonlama kaybi)",
        "file": STORE_CPP,
        "fn": ("set", "VersionId OccSolidStore::set(EntityId id, const TopoDS_Shape& shape)"),
        "old": "Slot& slot = slots_[id];",
        "new": "Slot& slot = slots_[id]; slot.versions.clear();",
        "expect": ["FAIL TWO versions preserved after 2nd write",
                   "FAIL v1 still retrievable by VersionId"],
    },
    {
        # Sayac ILERLETILMEZ -> her set AYNI id'yi uretir: "asla tekrar
        # kullanilmaz" sozu cignenir (gozlemlenebilir regresyon).
        "id": "NC8",
        "desc": "set() surum sayacini ILERLETMEZ (ayni id tekrar uretilir)",
        "file": STORE_CPP,
        "fn": ("set", "VersionId OccSolidStore::set(EntityId id, const TopoDS_Shape& shape)"),
        "old": "    nextVersionId_ = advanceVersionIdCounter(nextVersionId_);",
        "new": "    // mutasyon: sayac ilerletilmez",
        "expect": ["FAIL VersionIds strictly increasing (no reuse)",
                   "FAIL all VersionIds distinct (no duplicate allocation)"],
    },
    {
        # Sayac kMax'ta KELEPCELENIR (eski hatali davranis) -> tukenme YOK,
        # ayni id tekrar uretilir. Saf sinir testi (Test 10) bunu yakalar.
        "id": "NC9",
        "desc": "surum sayaci kMax'ta kelepcelenir (tukenme YOK, tekrar uretim)",
        "file": ENTITY_ID_HPP,
        "fn": ("advanceVersionIdCounter",
               "inline VersionId advanceVersionIdCounter(VersionId current) noexcept"),
        "old": "    if (current >= kMax) return kInvalidVersionId;               // kMax son gecerli id",
        "new": "    if (current >= kMax) return kMax;  // mutasyon: kelepcele",
        "expect": ["FAIL counter advance(max) -> 0 (EXHAUSTED, does not reuse max)",
                   "FAIL counter advance(max) does NOT stay at max"],
    },
]


def run_harness(passthrough):
    # Kaynak-sekil kapisi KAPATILIR: mutasyonun RUNTIME davranisi sinanmali.
    proc = subprocess.run([sys.executable, os.path.join(HERE, "run_occ_solid_ids.py")]
                          + passthrough + ["--no-shape-check"],
                          capture_output=True, text=True)
    return proc.returncode, proc.stdout + proc.stderr


def main(argv):
    if not argv:
        print("usage: mutate_occ_solid_ids.py <build_dir> [harness args...]",
              file=sys.stderr)
        return 2
    passthrough = argv

    # Iki kaynak dosyayi da yedekle (mutasyonlar farkli dosyalari hedefler).
    originals = {}
    hashes = {}
    for path in ALL_FILES:
        with open(path, "r", encoding="utf-8") as fh:
            originals[path] = fh.read()
        hashes[path] = sha(path)

    # Referans: mutasyonsuz hal GECMELI.
    rc, out = run_harness(passthrough)
    if rc != 0:
        print("REFERANS (mutasyonsuz) harness BASARISIZ — once bunu duzelt:")
        print(out)
        return 1
    print("referans (mutasyonsuz): OK")

    failures = 0
    try:
        for mut in MUTATIONS:
            path = mut["file"]
            name, sig = mut["fn"]
            mutated = apply_in_span(originals[path], sig, name, mut["old"], mut["new"])
            if mutated == originals[path]:
                print(f"{mut['id']}: mutasyon uygulanmadi (no-op) — HATA")
                failures += 1
                continue
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(mutated)
            rc, out = run_harness(passthrough)
            caught = rc != 0 and all(e in out for e in mut["expect"])
            print(f"{mut['id']} ({mut['desc']}): "
                  f"{'YAKALANDI' if caught else 'KACIRILDI'} (exit={rc})")
            if not caught:
                failures += 1
                print("  beklenen basarisizlik satirlari:")
                for e in mut["expect"]:
                    print(f"    {'OK ' if e in out else 'EKSIK'} {e}")
                print("  --- harness cikisi ---")
                print("\n".join("  " + ln for ln in out.splitlines() if "FAIL" in ln))
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(originals[path])
    finally:
        for path in ALL_FILES:
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(originals[path])

    for path in ALL_FILES:
        if sha(path) != hashes[path]:
            print(f"KRITIK: {path} bayt-birebir geri yazilamadi!")
            return 1
    print(f"kaynaklar geri yazildi (sha256 esit: "
          f"{hashes[APP_CPP][:12]}..., {hashes[OCC_CPP][:12]}...)")

    if failures:
        print(f"NEGATIF KONTROL BASARISIZ: {failures} mutasyon kacirildi")
        return 1
    print("NEGATIF KONTROLLER: OK (tum mutasyonlar yakalandi)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
