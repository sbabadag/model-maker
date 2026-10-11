#!/usr/bin/env python3
"""NEGATIF KONTROLLER — OCC sahiplik harness'inin gercekten yakaladigini kanitlar.

Her mutasyon uretim kaynagina GECICI olarak uygulanir, harness CALISTIRILIR ve
ilgili testin GERCEKTEN dustugu gosterilir; sonra dosya bayt-birebir geri
yazilir (hash ile dogrulanir). Mutasyonlar kalici DEGILDIR.

Mutasyonlar:
  NC1 (off-by-one): addCube'da sekil, `size() - 1` yerine `size() - 2` ile
      anahtarlanir → Test 1 (dogru EntityId<->sekil) DUSMELI.
  NC2 (operand tuketimi): addBooleanFuse'da operandlar sonuc anahtarlanmadan
      ONCE silinir → Test 6 (operand korunmasi) DUSMELI.

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


CUBE_KEY_OLD = "occShapes_[document_.modelId(document_.models().size() - 1)] = shape;"
CUBE_KEY_NEW = "occShapes_[document_.modelId(document_.models().size() - 2)] = shape;"

FUSE_KEY_OLD = "occShapes_[document_.modelId(document_.models().size() - 1)] = result;"
FUSE_KEY_NEW = ("occShapes_.erase(document_.modelId(0)); "
                "occShapes_.erase(document_.modelId(1)); "
                "occShapes_[document_.modelId(document_.models().size() - 1)] = result;")

MUTATIONS = [
    {
        "id": "NC1",
        "desc": "off-by-one: addCube sekli size()-2 ile anahtarlar",
        "fn": ("addCube", "void Application::addCube()"),
        "old": CUBE_KEY_OLD, "new": CUBE_KEY_NEW,
        "expect": ["FAIL cube id still holds the box",
                   "FAIL store has cube id"],
    },
    {
        "id": "NC2",
        "desc": "boolean operandlarini erken siler (tuketir)",
        "fn": ("addBooleanFuse", "void Application::addBooleanFuse()"),
        "old": FUSE_KEY_OLD, "new": FUSE_KEY_NEW,
        "expect": ["FAIL fuse: operand cube STILL in store",
                   "FAIL fuse: operand cylinder STILL in store",
                   "FAIL fuse operands preserved in store"],
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

    with open(APP_CPP, "r", encoding="utf-8") as fh:
        original = fh.read()
    original_hash = sha(APP_CPP)

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
            name, sig = mut["fn"]
            mutated = apply_in_span(original, sig, name, mut["old"], mut["new"])
            if mutated == original:
                print(f"{mut['id']}: mutasyon uygulanmadi (no-op) — HATA")
                failures += 1
                continue
            with open(APP_CPP, "w", encoding="utf-8") as fh:
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
            with open(APP_CPP, "w", encoding="utf-8") as fh:
                fh.write(original)
    finally:
        with open(APP_CPP, "w", encoding="utf-8") as fh:
            fh.write(original)

    restored_hash = sha(APP_CPP)
    if restored_hash != original_hash:
        print("KRITIK: dosya bayt-birebir geri yazilamadi!")
        return 1
    print(f"kaynak geri yazildi (sha256 esit: {restored_hash[:16]}...)")

    if failures:
        print(f"NEGATIF KONTROL BASARISIZ: {failures} mutasyon kacirildi")
        return 1
    print("NEGATIF KONTROLLER: OK (tum mutasyonlar yakalandi)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
