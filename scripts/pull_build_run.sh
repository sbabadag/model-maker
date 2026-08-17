#!/usr/bin/env bash
# model-maker nightly: pull (Windows değişiklikleri) + derle + çalıştır
# Kullanım: bash scripts/pull_build_run.sh
set -u

REPO=/workspace/model-maker
BUILD=/tmp/mm-build

cd "$REPO" || exit 1

echo "== 1) Fetch =="
git fetch origin || exit 1

echo "== 2) Branch ve çalışma ağacı kontrolü =="
BRANCH=$(git branch --show-current)
if [ "$BRANCH" != "hermes-nightly" ]; then
    echo "HATA: hermes-nightly üzerinde değilsiniz: $BRANCH" >&2
    exit 1
fi
if git rev-parse -q --verify MERGE_HEAD >/dev/null; then
    echo "HATA: yarım kalmış merge var, önce çözdürün" >&2
    exit 1
fi
dirty=$(git status --porcelain | grep -vE '^.. build-debug/|^\?\?' | head -1)
if [ -n "$dirty" ]; then
    echo "HATA: commit edilmemiş değişiklik var: $dirty" >&2
    exit 1
fi

echo "== 3) origin/main -> hermes-nightly merge =="
MAIN_TIP=$(git log -1 --format=%h origin/main)
if git merge origin/main -m "merge: pull Windows commits from main ($MAIN_TIP)"; then
    echo "merge OK (veya zaten güncel: $MAIN_TIP)"
else
    echo "HATA: çakışma çıktı - bana iletin, elle çözerim" >&2
    exit 1
fi
if git ls-files -u | grep -q .; then
    echo "HATA: çözülmemiş çakışma kaldı" >&2
    exit 1
fi

echo "== 4) Derleme =="
cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null || exit 1
cmake --build "$BUILD" \
    --target model_maker_core model_maker_render_prep_benchmark model_maker_spatial_query_benchmark \
    -j"$(nproc)" || exit 1

echo
echo "== 5) Benchmark: render prep =="
"$BUILD/model_maker_render_prep_benchmark" || exit 1
echo
echo "== 6) Benchmark: spatial query =="
"$BUILD/model_maker_spatial_query_benchmark" || exit 1

echo
echo "== 7) Correctness probları =="
for probe in mm_verify_camera mm_probe_effcache mm_probe_task2 mm_probe_trim mm_probe_polyline mm_probe_undo mm_probe_delta_index mm_probe_snap mm_probe_pending_overlap; do
    src="/opt/data/$probe.cpp"
    [ -f "$src" ] || { echo "ATLA: $src yok" >&2; continue; }
    g++ -std=c++20 -O2 -I include "$src" "$BUILD/libmodel_maker_core.a" -o "/opt/data/$probe" || exit 1
    echo -n "$probe: "
    "/opt/data/$probe" | tail -1
done

echo
echo "== 8) Exe (yalnızca Qt bulunan ortamda) =="
if cmake --build "$BUILD" --target model-maker -j"$(nproc)" >/dev/null 2>&1; then
    echo "model-maker.exe derlendi, başlatılıyor..."
    "$BUILD/model-maker" &
    echo "model-maker başlatıldı"
else
    echo "ATLA: model-maker exe hedefi yok — Qt6 bu makinede kurulu değil."
    echo "Windows'ta scripts/pull_build_run.ps1 son adımda exe'yi başlatır."
fi

echo
echo "== TAMAM: pull + build + run başarılı =="
