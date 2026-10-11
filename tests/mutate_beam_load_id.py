#!/usr/bin/env python3
# MUTASYON KOSUCUSU — Phase 2B (beamLoads_/occShapes_ EntityId anahtari).
# Amac: tests/beam_load_id_test.cpp'nin, indeks-anahtarli yanlis davranisi
# (silme sonrasi BASKA varliga baglanma) ve yasam dongusu temizligini
# GERCEKTEN yakaladigini kanitlamak. Her mutasyonda kaynak gecici olarak
# bozulur, test derlenip kosulur, BASARISIZ olmasi beklenir; sonra dosya
# BAYT-BIREBIR geri yazilir (git checkout KULLANILMAZ — Phase 2A/2B
# degisiklikleri commit'siz oldugu icin silinirdi).
import os
import subprocess
import sys

ROOT = "/home/ubuntu/model-maker"
DOC = os.path.join(ROOT, "src/document.cpp")
BUILD = os.path.join(ROOT, "build")
TEST = os.path.join(BUILD, "model_maker_beam_load_id_test")

MUTATIONS = [
    ("M1 clear() yukleri temizlemez",
     "    beamLoads_.clear(); // yeni belge: kiris yukleri de sifirlanir\n",
     ""),
    ("M2 load() yukleri temizlemez (onceki belgeden sizar)",
     "    // sizmamalari icin SIFIRLANIR.\n    beamLoads_.clear();\n",
     "    // sizmamalari icin SIFIRLANIR.\n"),
    ("M3 INDEKS anahtarli (eski hatanin taklidi): id yerine indeksle sakla/oku",
     "void Document::setBeamLoad(EntityId entityId, BeamLoad load) {\n"
     "    // Gecersiz kimlik (0) kaydedilmez — sentinel degere yuk baglanmaz.\n"
     "    if (entityId == kInvalidEntityId) return;\n"
     "    beamLoads_[entityId] = load;\n"
     "}\n\n"
     "std::optional<BeamLoad> Document::getBeamLoad(EntityId entityId) const {\n"
     "    auto it = beamLoads_.find(entityId);\n",
     "void Document::setBeamLoad(EntityId entityId, BeamLoad load) {\n"
     "    const auto idx = indexOfEntity(entityId); if (!idx) return;\n"
     "    beamLoads_[*idx] = load;\n"
     "}\n\n"
     "std::optional<BeamLoad> Document::getBeamLoad(EntityId entityId) const {\n"
     "    const auto idx = indexOfEntity(entityId); if (!idx) return std::nullopt;\n"
     "    auto it = beamLoads_.find(*idx);\n"),
]


def run(cmd):
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)


def build_and_run():
    b = run(["cmake", "--build", BUILD, "--target", "model_maker_beam_load_id_test", "-j2"])
    if b.returncode != 0:
        return "DERLEME-HATASI", b.stdout[-400:]
    r = subprocess.run([TEST], capture_output=True, text=True)
    return r.returncode, r.stdout


def main():
    original = open(DOC, "rb").read()
    caught = 0
    try:
        # Referans: mutasyonsuz hal gecmeli.
        rc, out = build_and_run()
        print(f"[REFERANS] rc={rc} -> {'GECTI' if rc == 0 else 'DUSTU'}")
        if rc != 0:
            print("Referans hal GECMIYOR — mutasyon testi anlamsiz.")
            return 1
        for name, old, new in MUTATIONS:
            text = original.decode()
            if old not in text:
                print(f"[ATLANDI] {name}: hedef metin bulunamadi")
                continue
            mutated = text.replace(old, new, 1)
            open(DOC, "w").write(mutated)
            rc, out = build_and_run()
            status = "YAKALANDI" if (rc != 0) else "KACTI"
            print(f"[{status}] {name} (rc={rc})")
            if rc != 0:
                caught += 1
    finally:
        open(DOC, "wb").write(original)
    rc, out = build_and_run()
    print(f"[GERI YAZILDI] rc={rc} -> {'GECTI' if rc == 0 else 'DUSTU'}")
    print(f"{caught}/{len(MUTATIONS)} mutasyon yakalandi")
    return 0 if (caught == len(MUTATIONS) and rc == 0) else 1


if __name__ == "__main__":
    sys.exit(main())
