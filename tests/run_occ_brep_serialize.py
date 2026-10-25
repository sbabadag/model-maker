#!/usr/bin/env python3
"""B-Rep SERIALIZER round-trip kosucusu (Phase 3.4A).

BAGIMSIZ test: `tests/occ_brep_serialize_test.cpp` + `src/occ_brep_serialize.cpp`
GERCEK OCCT ile derlenir ve calistirilir. Uygulama geometri modulu gerektirmez
(test yalnizca OCCT ilkelleri + serializer kullanir).

Kullanim: run_occ_brep_serialize.py <build_dir> [--cxx PATH]
                      [--occt-include DIR] [--occt-libdir DIR]
"""
import glob
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

OCC_LIBS = ["TKernel", "TKMath", "TKG2d", "TKG3d", "TKGeomBase", "TKBRep",
            "TKGeomAlgo", "TKTopAlgo", "TKPrim", "TKBO", "TKBool",
            "TKShHealing", "TKMesh", "TKXSBase"]


def detect_occt_include():
    for cand in ("/usr/include/opencascade", "/usr/local/include/opencascade"):
        if os.path.exists(os.path.join(cand, "BinTools.hxx")):
            return cand
    for cand in glob.glob("/usr/include/opencascade*"):
        if os.path.exists(os.path.join(cand, "BinTools.hxx")):
            return cand
    return None


def detect_occt_libdir():
    for pat in ("/usr/lib/x86_64-linux-gnu/libTKernel.so*",
                "/usr/lib/aarch64-linux-gnu/libTKernel.so*",
                "/usr/lib64/libTKernel.so*", "/usr/lib/libTKernel.so*"):
        hit = sorted(glob.glob(pat))
        if hit:
            return os.path.dirname(hit[0])
    return None


def main(argv):
    if not argv:
        print("usage: run_occ_brep_serialize.py <build_dir> [--cxx ...] [--occt-include ...] [--occt-libdir ...]",
              file=sys.stderr)
        return 2
    build_dir = argv[0]
    opts = {"cxx": None, "occt_include": None, "occt_libdir": None}
    i = 1
    while i < len(argv):
        a = argv[i]
        if a in ("--cxx", "--occt-include", "--occt-libdir"):
            if i + 1 >= len(argv):
                print(f"{a} needs a value", file=sys.stderr)
                return 2
            opts[a[2:].replace("-", "_")] = argv[i + 1]
            i += 2
        else:
            print(f"unknown argument: {a}", file=sys.stderr)
            return 2

    cxx = opts["cxx"] or shutil.which("g++") or shutil.which("c++")
    inc = opts["occt_include"] or detect_occt_include()
    libdir = opts["occt_libdir"] or detect_occt_libdir()
    if not cxx:
        print("C++ derleyici bulunamadi", file=sys.stderr)
        return 2
    if not inc:
        print("OCCT baslik dizini bulunamadi (--occt-include)", file=sys.stderr)
        return 2
    if not libdir:
        print("OCCT kutuphane dizini bulunamadi (--occt-libdir)", file=sys.stderr)
        return 2

    os.makedirs(build_dir, exist_ok=True)
    exe = os.path.join(build_dir, "occ_brep_serialize_test")
    cmd = [cxx, "-std=c++20", "-O1", "-DMM_HAS_OCC", "-D_USE_MATH_DEFINES",
           "-I" + os.path.join(ROOT, "include"), "-I" + inc,
           os.path.join(ROOT, "tests", "occ_brep_serialize_test.cpp"),
           os.path.join(ROOT, "src", "occ_brep_serialize.cpp"),
           os.path.join(ROOT, "src", "sha256.cpp"),
           "-o", exe, "-L" + libdir]
    cmd += ["-l" + lib for lib in OCC_LIBS]
    print("derliyor: %s" % cxx)
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        sys.stdout.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        print("derleme BASARISIZ", file=sys.stderr)
        return 2

    print("calistiriliyor: %s" % exe)
    run = subprocess.run([exe], capture_output=True, text=True)
    sys.stdout.write(run.stdout)
    sys.stderr.write(run.stderr)
    if run.returncode != 0:
        print(f"SERIALIZER TEST BASARISIZ (exit={run.returncode})", file=sys.stderr)
        return 1
    print("SERIALIZER TEST OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
