#!/usr/bin/env bash
# WIN32 SOZDIZIMI KAPISI (ST-4 surec kurali)
#
# qt_main_window.cpp / renderer.cpp gibi WIN32-only dosyalar Linux'ta gercek
# derlemeye girmez; bu yuzden GUI degisiklikleri 163 testte yakalanmiyordu.
# Bu script o dosyalari iki modda denetler:
#
#   1) MINGW MODU (tercih edilen): x86_64-w64-mingw32-g++ + gercek mingw
#      windows.h + OCC ve Qt basliklari. Qt-mingw sysroot'u yoksa atlanir.
#   2) SHIM MODU (her zaman): host g++ -fsyntax-only + tools/winshim/windows.h
#      ve host Qt6 basliklari. Gercek Win32 ABI'sini degil, KODUN SOZDIZIMINI
#      ve kapsam/decl hatalarini yakalar (bu hatayi yakalayan sey budur).
#
# Kullanim: tools/win_syntax_check.sh [dosya...]
#   (varsayilan: tum WIN32-only dosyalar — model-maker exe kaynaklari)
# Cikis: 0 = yesil, 1 = hata.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

FILES=("$@")
if [ ${#FILES[@]} -eq 0 ]; then
    FILES=(src/main.cpp
           src/application.cpp
           src/qt_main_window.cpp
           src/two_point_view.cpp
           src/opengl_render_backend.cpp
           src/component_dialog_widget.cpp
           src/component_live_preview_widget.cpp
           src/component_option_icons.cpp
           src/drawing_sheet_widget.cpp
           src/renderer.cpp
           src/view_cube_renderer.cpp
           src/gdi_render_backend.cpp
           src/force_diagram.cpp)
fi

QT_BASE=""
for d in /usr/include/x86_64-linux-gnu/qt6 /usr/include/qt6; do
    [ -d "$d" ] && QT_BASE="$d" && break
done
if [ -z "$QT_BASE" ]; then
    echo "HATA: Qt6 basliklari bulunamadi (qt6-base-dev kurulu mu?)" >&2
    exit 1
fi

INC=(-Iinclude -Ithird_party -Ilibs/mm_contract/include -I/usr/include/opencascade
     -I"$QT_BASE" -I"$QT_BASE/QtCore" -I"$QT_BASE/QtGui" -I"$QT_BASE/QtWidgets")

status=0
for f in "${FILES[@]}"; do
    echo "== shim modu: $f"
    if ! g++ -fsyntax-only -std=c++20 -fPIC -DMM_HAS_OCC \
         -Werror=return-type -Itools/winshim "${INC[@]}" "$f"; then
        status=1
    fi
done

# MINGW MODU (opsiyonel): Qt-for-mingw sysroot verilirse calisir.
MINGW_QT="${MM_MINGW_QT:-}"
if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1 && [ -n "$MINGW_QT" ] && [ -d "$MINGW_QT" ]; then
    for f in "${FILES[@]}"; do
        echo "== mingw modu: $f"
        if ! x86_64-w64-mingw32-g++ -fsyntax-only -std=c++20 \
             -D_WIN32 -DWIN32 -DUNICODE -D_UNICODE -DMM_HAS_OCC \
             -Werror=return-type \
             -Iinclude -Ithird_party -Ilibs/mm_contract/include \
             -I"$MINGW_QT/include" -I"$MINGW_QT/include/QtCore" \
             -I"$MINGW_QT/include/QtGui" -I"$MINGW_QT/include/QtWidgets" \
             "$f"; then
            status=1
        fi
    done
else
    echo "-- mingw modu ATLANDI (MM_MINGW_QT tanimli degil; Qt-for-mingw sysroot yok)"
fi

if [ $status -eq 0 ]; then
    echo "WIN32 SOZDIZIMI: YESIL"
else
    echo "WIN32 SOZDIZIMI: KIRMIZI" >&2
fi
exit $status
