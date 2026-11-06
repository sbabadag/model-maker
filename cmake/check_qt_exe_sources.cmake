# Qt exe kaynak kapısı: Qt kabuk kaynakları (#include "model_maker/X.hpp")
# bir X'i kullandığında, src/X.cpp diskte varsa ve hem MODELMAKER_QT_SOURCES'ta
# hem de kütüphanelerde (mm_foundation / mm_component_contract / model_maker_core)
# YOKSA, model-maker exe'si Windows'ta TANIMSIZ SEMBOL ile linklenemez.
#
# Vaka (UI-2): ribbon_widget.cpp yalnız model_maker_ribbon_widget_test hedefine
# eklenmişti; qt_main_window.cpp RibbonWidget kurduğu için exe linki patladı.
# Bu hata Linux'ta görünmez (exe WIN32-only) — bu kontrol onu Linux ctest'te yakalar.
#
# Kullanım: cmake -DSRC_DIR=<repo> -DQT_SOURCES=<list> -DLIB_SOURCES=<list> -P check_qt_exe_sources.cmake
# QT_SOURCES = MODELMAKER_QT_SOURCES, LIB_SOURCES = üç kütüphanenin SOURCES özellikleri.
set(_missing "")
foreach(_qtf IN LISTS QT_SOURCES)
    if(NOT _qtf MATCHES "\\.cpp$")
        continue()
    endif()
    file(STRINGS "${SRC_DIR}/${_qtf}" _lines REGEX "#include \"model_maker/[A-Za-z0-9_]+\\.hpp\"")
    foreach(_l IN LISTS _lines)
        string(REGEX MATCH "model_maker/[A-Za-z0-9_]+" _hdr "${_l}")
        string(REPLACE "model_maker/" "" _base "${_hdr}")
        set(_cpp "src/${_base}.cpp")
        if(NOT EXISTS "${SRC_DIR}/${_cpp}")
            continue()
        endif()
        list(FIND QT_SOURCES "${_cpp}" _inqt)
        list(FIND LIB_SOURCES "${_cpp}" _inlib)
        if(_inqt EQUAL -1 AND _inlib EQUAL -1)
            list(APPEND _missing "'${_cpp}' (kullanıldı: ${_qtf})")
        endif()
    endforeach()
endforeach()
if(_missing)
    message(FATAL_ERROR "Qt exe kaynak hatası — widget .cpp'si MODELMAKER_QT_SOURCES'a eklenmemiş: ${_missing}")
endif()
message(STATUS "OK: Qt kabuk kaynaklarının widget .cpp'leri exe hedefinde ya da bir kütüphanede")
