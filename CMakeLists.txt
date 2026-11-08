cmake_minimum_required(VERSION 3.20)

set(QT_ROOT "C:/Qt/6.9.3/mingw_64" CACHE PATH "Qt6 root directory")

project(ModelMaker VERSION 0.1.0 LANGUAGES CXX)

# Elle -DOCC_ROOT yolunda (Windows) app NORMALDE OCC'ye baglanmaz; kopru
# (mm_occ.dll, LoadLibrary) kullanilir. Ama OCC ve app AYNI toolchain ile
# derleniyorsa (orn. hepsi MSYS2 ucrt64) dogrudan baglamak guvenli ve TAM
# ozellik verir (profil ekstruzyonu dahil). Bu modda `-static` BIRAKILIR —
# aksi halde ld, OCC'nin libTK*.dll.a import kutuphanelerini bulamaz
# ("have you installed the static version of the TKernel library?").
option(MM_OCC_LINK_DIRECT
    "Link the manual -DOCC_ROOT OCC libs directly into the Qt app too (only when OCC and the app share one toolchain)" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
# AUTOUIC yalniz .ui dosyasi varsa anlamli. Repo'da .ui yok; buna ragmen
# AUTOUIC acikken CMake, uic'i configure sirasinda test eder ve bozuk/kismi
# Qt kurulumlarinda (orn. Qt6Core.dll prefix karisikligi -> 0xc0000139)
# configure'u tamamen kirar. Bu yuzden .ui bulunmadikca kapali tutulur.
file(GLOB_RECURSE MODELMAKER_UI_FILES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.ui"
     "${CMAKE_CURRENT_SOURCE_DIR}/resources/*.ui")
if(MODELMAKER_UI_FILES)
    set(CMAKE_AUTOUIC ON)
else()
    set(CMAKE_AUTOUIC OFF)
endif()

# Qt6 (optional for the ribbon UI). Override with -DQT_ROOT=<path>.
list(PREPEND CMAKE_PREFIX_PATH "${QT_ROOT}")
find_package(Qt6 QUIET COMPONENTS Widgets Core Gui)

# ============================================================================
# mm_foundation — yaprak saf tipler (OCCT'siz, Qt'siz, Document'siz).
# entity_id.hpp header-only'dir. Studio bunu Qt/OCCT cekmeden derleyebilir.
# ============================================================================
add_library(mm_foundation
    src/geometry.cpp
    src/reference_geometry.cpp
    src/contact_resolver.cpp
    src/sha256.cpp
)
target_include_directories(mm_foundation PUBLIC include third_party)
target_compile_options(mm_foundation PRIVATE -Wall -Wextra -Wpedantic)

# ============================================================================
# mm_component_contract — component veri modeli + generator'lar (saf; OCC/Qt/Document'siz).
# CompParamSpec, GenerationPlan, ComponentRegistry, ComponentDialogSchema,
# generateEndPlate/generateFinPlate/generateHaunch40 vb. generator'lar burada.
# ============================================================================
add_library(mm_component_contract
    src/cut_modifier.cpp
    src/bolt.cpp
    src/constraint_system.cpp
    src/magnetic_frame.cpp
    src/end_plate.cpp
    src/component_engine.cpp
    src/component_dialog.cpp
    src/component_dialog_146_canvas.cpp
    src/component_framework.cpp
    src/component_catalog.cpp
    src/component_codec.cpp
    src/component_package.cpp
    src/component_preview.cpp
    src/component_checks.cpp
    src/component_preview_auto.cpp
    src/component_schematic.cpp
    src/recipe.cpp
)
target_include_directories(mm_component_contract PUBLIC include third_party ${CMAKE_CURRENT_SOURCE_DIR}/libs/mm_contract/include)
target_link_libraries(mm_component_contract PUBLIC mm_foundation)
target_compile_options(mm_component_contract PRIVATE -Wall -Wextra -Wpedantic)

add_library(model_maker_core
    src/grid.cpp
    src/camera.cpp
    src/view_cube.cpp
    src/ribbon_layout.cpp
    src/command_registry.cpp
    src/command_catalog.cpp
    src/performance.cpp
    src/dxf.cpp
    src/document.cpp
    src/drafting.cpp
    src/profile_database.cpp
    src/occ_store_null.cpp
    src/mmw8_codec.cpp
    src/ucs.cpp
    src/component.cpp
    src/geometry_reference_resolver.cpp
    src/component_recompute.cpp
    src/component_rebind.cpp
    src/axis_delta.cpp
    src/linear_delta.cpp
    src/component_glyph.cpp
    src/component_input_builder.cpp
    src/assembly.cpp
    src/bridge_package.cpp
    src/bridge_component_mapper.cpp
    src/bridge_native_mapper.cpp
    src/bridge_native_bind.cpp
    src/bridge_nc146.cpp
    src/user_catalog.cpp
    src/component_presets.cpp
    src/marking.cpp
    src/fabrication.cpp
    src/drawing.cpp
    src/drawing_layout.cpp
    src/drawing_set.cpp
    src/sections.cpp
    src/paper_editing.cpp
    src/drawing_builder.cpp)
target_include_directories(model_maker_core PUBLIC include third_party)
target_link_libraries(model_maker_core PUBLIC mm_foundation mm_component_contract)
target_compile_options(model_maker_core PRIVATE -Wall -Wextra -Wpedantic)

set(MODELMAKER_QT_SOURCES
    src/qt_main_window.cpp
    include/model_maker/qt_main_window.hpp
    src/two_point_view.cpp
    include/model_maker/two_point_view.hpp
    src/opengl_render_backend.cpp
    include/model_maker/opengl_render_backend.hpp
    src/component_dialog_widget.cpp
    src/component_live_preview_widget.cpp
    include/model_maker/component_live_preview_widget.hpp
    src/component_option_icons.cpp
    include/model_maker/component_option_icons.hpp
    include/model_maker/component_dialog_widget.hpp
    src/drawing_sheet_widget.cpp
    include/model_maker/drawing_sheet_widget.hpp
    src/ribbon_widget.cpp
    include/model_maker/ribbon_widget.hpp
    src/properties_panel.cpp
    include/model_maker/properties_panel.hpp
    src/profile_picker_dialog.cpp
    include/model_maker/profile_picker_dialog.hpp)
set(MODELMAKER_QT_MISSING "")
foreach(qt_file ${MODELMAKER_QT_SOURCES})
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${qt_file}")
        list(APPEND MODELMAKER_QT_MISSING "${qt_file}")
    endif()
endforeach()
if(WIN32 AND Qt6_FOUND AND MODELMAKER_QT_MISSING STREQUAL "") # GUI Win32+GDI: yalniz Windows
    add_executable(model-maker
        src/main.cpp
        src/application.cpp
        ${MODELMAKER_QT_SOURCES}
        src/renderer.cpp
        src/view_cube_renderer.cpp
        src/gdi_render_backend.cpp
        src/force_diagram.cpp
        src/gui_test_runner.cpp
        resources/tekla146.qrc
    )
    target_include_directories(model-maker PRIVATE include)
    target_link_libraries(model-maker PRIVATE model_maker_core Qt6::Widgets Qt6::Core Qt6::Gui
        gdi32 comdlg32 comctl32 msimg32 opengl32 dwmapi)
    if(MM_OCC_LINK_DIRECT)
        # OCC dogrudan baglanir: -static OLMAMALI — OCC'nin libTK*.dll.a
        # import kutuphaneleri ancak dinamik modda cozulur. C++ runtime
        # (libgcc/libstdc++) yine statik kalir; winpthread MSYS2 PATH'inden.
        target_link_options(model-maker PRIVATE
            -static-libgcc -static-libstdc++ -Wl,--subsystem,windows)
    else()
        target_link_options(model-maker PRIVATE
            -static -static-libgcc -static-libstdc++ -Wl,--subsystem,windows)
    endif()
    target_compile_definitions(model-maker PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX QT_NO_ENTRYPOINT)
    target_compile_options(model-maker PRIVATE -Wall -Wextra -Wpedantic)

    # --- 3Dconnexion SpaceMouse (Navlib SDK 4.x) — Windows-only ---
    # SDK basliklari + TDxNavLib.lib third_party/3dxware altindadir (kullanici
    # 3DxWare_SDK_v4-0-6_r22071'den kopyaladi). Navlib fonksiyonlari (NlCreate
    # vb.) weak-import'tur — surucu/navlib.dll yoksa sessizce devre disi kalir.
    if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/3dxware/inc/SpaceMouse/IAccessors.hpp")
        set(MODELMAKER_SPACEMOUSE_READY TRUE)
        target_include_directories(model-maker PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/3dxware/inc
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/3dxware/inc/navlib)
        # SpaceMouse kaynaklarini listeye ekle
        target_sources(model-maker PRIVATE src/spacemouse.cpp)
        # SpaceMouse kodu YALNIZCA SDK varken derlenir: hem baslik guardlari
        # hem uygulama ici guardlar bu makroya bakar (MM_HAS_OCC gibi).
        # SDK yoksa _WIN32 olsa bile navlib basliklari include EDILMEZ.
        target_compile_definitions(model-maker PRIVATE MM_HAS_SPACEMOUSE)
        # Statik thuvasya TDxNavLib.lib (navlib fonksiyon govdeleri)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/3dxware/lib/TDxNavLib.lib")
            target_link_libraries(model-maker PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/3dxware/lib/TDxNavLib.lib")
        endif()
        message(STATUS "3Dconnexion SpaceMouse SDK dogrudan baglaniyor (Navlib 4.x)")
    else()
        message(STATUS "3Dconnexion SpaceMouse SDK bulunamadi (third_party/3dxware) — SpaceMouse kapali")
    endif()
else()
    message(WARNING "Not Windows, Qt6 not found or Qt sources missing (${MODELMAKER_QT_MISSING}) - "
                    "skipping the model-maker executable; core library, tests and "
                    "benchmarks still build.")
endif()

# GT-1 (INBOX 6): masaustunde PENCERE ACAN tekrarlanabilir GUI senaryolari.
# Varsayilan kosuda YOKLAR (spec: "varsayılan koşuda değil"); yalniz
# MM_GUI_TESTS=ON ile configure edilince kayitli olurlar ve `ctest -L gui`
# ile kosarlar (kullanici izni / DURAK). Is bitince ayni build dizinini
# MM_GUI_TESTS=OFF ile yeniden configure et (gece modu pencere acmasin).
option(MM_GUI_TESTS "Masaustu GUI test senaryolarini kaydet (gt1)" OFF)
if(MM_GUI_TESTS AND WIN32 AND TARGET model-maker)
    foreach(scenario smoke_line b6_line_ignores_profile b2b7_line_visible
                     b2b7_line_visible_gl
                     b1_catalog_search h1_view_to_surface h4_held_snap_release_distance
                     h6_held_snap_visual_hold
                     t9_haunch40_e2e t9_endplate_e2e)
        add_test(NAME model_maker_gt1_${scenario}
            COMMAND model-maker
                --test-script ${CMAKE_CURRENT_SOURCE_DIR}/tests/gt1/${scenario}.json
                --out ${CMAKE_CURRENT_BINARY_DIR}/gt1-out/${scenario})
        set_tests_properties(model_maker_gt1_${scenario} PROPERTIES LABELS gui)
    endforeach()
endif()

# --- OpenCASCADE (OCC) — 3B kati modelleme (istege bagli) ---
# Uc yol:
# 1) vcpkg (ONERILEN, Windows): configure'i vcpkg toolchain dosyasiyla
#    yap — find_package(OpenCASCADE) vcpkg kurulumunu bulur, uygulama
#    OCC'yi DOGRUDAN baglar (ayni mingw1310 derleyici ailesi — DLL/runtime
#    catismasi yok, statik triplet ile exe'ye gomulur).
# 2) Linux: -DOCC_ROOT=<prefix> (kaynaktan kurulan OCCT — config saglikli).
# 3) Windows/MSYS2: OpenCASCADEConfig VTK hedeflerini (TKIVtk) referans
#    verir ama VTK zinciri yuklenmemistir; import kutuphanelerine dogrudan
#    baglaniriz (mm_occ.dll koprusu icin — uygulama OCC'siz kalir).
set(OCC_ROOT "" CACHE PATH "OpenCASCADE install prefix")
if(OCC_ROOT)
    list(PREPEND CMAKE_PREFIX_PATH "${OCC_ROOT}")
endif()

set(MODELMAKER_OCC_READY FALSE)
if(WIN32 AND OCC_ROOT)
    find_path(OCC_INCLUDE_DIR NAMES BRepPrimAPI_MakeBox.hxx
              PATH_SUFFIXES include/opencascade HINTS ${OCC_ROOT} NO_DEFAULT_PATH)
    if(OCC_INCLUDE_DIR)
        # OCCT 7.9 (MSYS2): STEP toolkit'leri tek TKDESTEP altinda
        # (eski TKSTEPBase/Attr/209/STEP kaldirildi).
        set(OCC_MANUAL_LIBS TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep
            TKGeomAlgo TKTopAlgo TKPrim TKBO TKBool TKShHealing TKMesh
            TKXSBase TKDESTEP)
        set(OCC_LIB_DIR ${OCC_ROOT}/lib)
        set(MODELMAKER_OCC_READY TRUE)
        message(STATUS "OpenCASCADE bulundu (el ile baglanti): ${OCC_INCLUDE_DIR}")
    else()
        message(STATUS "OCC_ROOT verildi ama opencascade basliklari bulunamadi: ${OCC_ROOT}")
    endif()
elseif(UNIX)
    # Linux: sistem OCCT (apt opencascade). Debian'daki OpenCASCADEConfig,
    # Visualization/TKIVtk hedefleri icin VTK gerektirdiginden find_package
    # kirilir; gerekli modulleri (Visualization/Draw HARIC) dogrudan baglariz.
    find_path(OCC_INCLUDE_DIR NAMES BRepPrimAPI_MakeBox.hxx
              PATH_SUFFIXES opencascade
              HINTS /usr/include /usr/local/include)
    find_library(OCC_TKERNEL_LIB NAMES TKernel
                 PATHS /usr/lib /usr/local/lib PATH_SUFFIXES x86_64-linux-gnu aarch64-linux-gnu)
    if(OCC_INCLUDE_DIR AND OCC_TKERNEL_LIB)
        set(OCC_MANUAL_LIBS TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep
            TKGeomAlgo TKTopAlgo TKPrim TKBO TKBool TKShHealing TKMesh
            TKXSBase TKDESTEP)
        get_filename_component(OCC_LIB_DIR "${OCC_TKERNEL_LIB}" DIRECTORY)
        set(MODELMAKER_OCC_READY TRUE)
        message(STATUS "OpenCASCADE bulundu (Linux el ile baglanti): ${OCC_INCLUDE_DIR}")
    else()
        message(STATUS "OpenCASCADE bulunamadi — OCC modulu kapali (vcpkg toolchain dosyasi ya da -DOCC_ROOT)")
    endif()
else()
    find_package(OpenCASCADE QUIET)
    if(OpenCASCADE_FOUND)
        set(MODELMAKER_OCC_READY TRUE)
        message(STATUS "OpenCASCADE bulundu: ${OpenCASCADE_DIR}")
    else()
        message(STATUS "OpenCASCADE bulunamadi — OCC modulu kapali (vcpkg toolchain dosyasi ya da -DOCC_ROOT)")
    endif()
endif()

option(BUILD_OCC_BRIDGE_DLL
    "Build mm_occ.dll — the C-API bridge for the Qt app (use MSYS2 toolchain)" OFF)

if(MODELMAKER_OCC_READY)
    add_library(model_maker_occ src/occ_geometry.cpp src/occ_solid_store.cpp src/occ_brep_serialize.cpp src/bolt_geometry.cpp src/end_plate_geometry.cpp src/occ_contact_resolver.cpp src/component_materializer.cpp src/bridge_occ_adapter.cpp src/weld_geometry.cpp)
    target_link_libraries(model_maker_occ PUBLIC model_maker_core)
    target_include_directories(model_maker_occ PUBLIC include)
    if(OCC_INCLUDE_DIR)
        target_include_directories(model_maker_occ PUBLIC ${OCC_INCLUDE_DIR})
    endif()
    if(OpenCASCADE_FOUND)
        target_link_libraries(model_maker_occ PUBLIC ${OpenCASCADE_LIBRARIES})
    else()
        target_link_libraries(model_maker_occ PUBLIC ${OCC_MANUAL_LIBS})
        if(DEFINED OCC_LIB_DIR AND NOT OCC_LIB_DIR STREQUAL "")
            target_link_directories(model_maker_occ PUBLIC ${OCC_LIB_DIR})
        elseif(OCC_ROOT)
            target_link_directories(model_maker_occ PUBLIC ${OCC_ROOT}/lib)
        endif()
    endif()
    target_compile_definitions(model_maker_occ PUBLIC MM_HAS_OCC)
    if(WIN32)
        # OCCT basliklari siki ISO modunda M_PI/M_PI_2 gormez.
        target_compile_definitions(model_maker_occ PUBLIC _USE_MATH_DEFINES)
    endif()
    target_compile_options(model_maker_occ PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Uygulama OCC'yi CONFIG yolunda (vcpkg/Linux) VEYA MM_OCC_LINK_DIRECT ile
# elle (-DOCC_ROOT, tek toolchain) yolunda dogrudan kullanir. Aksi halde
# MSYS2 koprusu (mm_occ.dll) LoadLibrary ile yuklenir.
if((OpenCASCADE_FOUND OR (MODELMAKER_OCC_READY AND MM_OCC_LINK_DIRECT)) AND TARGET model-maker)
    target_link_libraries(model-maker PRIVATE model_maker_occ)
    target_compile_definitions(model-maker PRIVATE MM_HAS_OCC)
    # Sürüm metni pencere başlığındaki OCC göstergesine gider (build_info.hpp).
    # OpenCASCADE_VERSION her config'de tanımlı olmayabilir — o zaman "?" kalır.
    if(DEFINED OpenCASCADE_VERSION AND NOT OpenCASCADE_VERSION STREQUAL "")
        target_compile_definitions(model-maker PRIVATE MM_OCC_VERSION="${OpenCASCADE_VERSION}")
    endif()
    if(OpenCASCADE_FOUND)
        message(STATUS "Uygulama OCC'yi dogrudan bagliyor (config/ayni toolchain)")
    else()
        message(STATUS "Uygulama OCC'yi dogrudan bagliyor (elle -DOCC_ROOT + MM_OCC_LINK_DIRECT)")
    endif()
endif()

# mm_occ.dll: Qt uygulamasinin LoadLibrary ile yukledigi C-API koprusu.
# YALNIZ MSYS2 toolchain'iyle derlenir (build-occ'de -DBUILD_OCC_BRIDGE_DLL=ON).
if(MODELMAKER_OCC_READY AND WIN32 AND BUILD_OCC_BRIDGE_DLL)
    add_library(model_maker_occ_bridge SHARED src/occ_bridge.cpp src/occ_geometry.cpp src/bolt_geometry.cpp src/end_plate_geometry.cpp)
    # Kopru YALNIZ modelleme cekirdegini baglar — TKDESTEP/TKXSBase'i
    # DISARIDA birak. STEP kuyrugu TKService->TKDraw->tcl/tk86 ve VTK'ye
    # uzandigi icin (MSYS2'nin OCCT paketi) izole occ\ dizininde yukleme
    # zinciri kiriliyordu (hata 203). Kutu/silindir icin gerek yok.
    set(OCC_BRIDGE_LIBS TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep
        TKGeomAlgo TKTopAlgo TKPrim TKBO TKBool TKShHealing)
    target_link_libraries(model_maker_occ_bridge PUBLIC model_maker_core ${OCC_BRIDGE_LIBS})
    target_link_directories(model_maker_occ_bridge PUBLIC ${OCC_ROOT}/lib)
    target_include_directories(model_maker_occ_bridge PUBLIC include ${OCC_INCLUDE_DIR})
    target_compile_definitions(model_maker_occ_bridge PRIVATE MM_HAS_OCC _USE_MATH_DEFINES MM_OCC_NO_STEP)
    # GCC runtime'ini DLL icine gom — Qt uygulamasinin (GCC 13) libstdc++'si
    # ile MSYS2'nin (GCC 16) libstdc++'si ayni dizinde cakisinca cokme oluyor.
    target_link_options(model_maker_occ_bridge PRIVATE -static-libgcc -static-libstdc++)
    set_target_properties(model_maker_occ_bridge PROPERTIES OUTPUT_NAME "mm_occ")
endif()

include(CTest)
if(BUILD_TESTING)
    # Qt exe kaynak kapısı: kabuk kaynaklarının kullandığı widget .cpp'si exe
    # hedefinde (MODELMAKER_QT_SOURCES) ya da bir kütüphanede olmalı. Aksi halde
    # Windows exe linki tanımsız sembol verir (ribbon_widget yalnız test hedefine
    # eklenmişti). Hata Linux'ta görünmez (exe WIN32-only) — burada yakalanır.
    get_target_property(_mmf_srcs mm_foundation SOURCES)
    get_target_property(_mmc_srcs mm_component_contract SOURCES)
    get_target_property(_core_srcs model_maker_core SOURCES)
    add_test(NAME model_maker_qt_exe_sources_ok
        COMMAND ${CMAKE_COMMAND}
            -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            "-DQT_SOURCES=${MODELMAKER_QT_SOURCES}"
            "-DLIB_SOURCES=${_mmf_srcs};${_mmc_srcs};${_core_srcs}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_qt_exe_sources.cmake)
    # H-1: Yuzeye Gore Gorunus tiklama->callback sozlesmesi + tani loglari.
    # SAF Python (derleme yok, build dizini kullanilmaz) -> Windows'ta da
    # kosar (push kapisi bu makinede tam ctest'tir). Mutasyonla dogrulanir
    # (callback cagrisi silinirse kirmizi).
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME model_maker_view_to_surface
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_view_to_surface.py
                ${CMAKE_CURRENT_BINARY_DIR})
    endif()
    # Portable harness compiles actual Win32 gumball method bodies with only
    # the UI boundary substituted. Native Qt/Win32 visual smoke is separate.
    if(UNIX AND NOT CMAKE_CROSSCOMPILING)
        find_package(Python3 QUIET COMPONENTS Interpreter)
        if(Python3_Interpreter_FOUND)
            add_test(NAME model_maker_gumball_interaction
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_gumball_interaction.py
                    ${CMAKE_CURRENT_BINARY_DIR})
            # B6: Cizgi/Polyline/Dikdortgen/Daire aktif profili YOK SAYAR;
            # profilli uye yalniz Kolon (placeColumn) ve Kiris (DrawTool::Beam
            # -> assignProfileToLine) araclarindan gelir. Gercek
            # Application::commitPoint govdesi sahte Win32 sinirinda derlenir
            # (gumball deseni); OCC atama yolu kayit stub'iyla dogrulanir.
            add_test(NAME model_maker_line_beam_tool
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_line_beam_tool.py
                    ${CMAKE_CURRENT_BINARY_DIR})
            # Dogrusal kopya/tasima delta paneli: Application durum makinesi
            # (gercek govdeler) + Qt panel sozlesmesi (offscreen).
            add_test(NAME model_maker_linear_delta_panel
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_linear_delta_panel.py
                    ${CMAKE_CURRENT_BINARY_DIR})
            add_test(NAME model_maker_linear_delta_dialog
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_linear_delta_dialog.py
                    ${CMAKE_CURRENT_BINARY_DIR})
            add_test(NAME model_maker_two_point_view
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_two_point_view.py
                    ${CMAKE_CURRENT_BINARY_DIR})
            add_test(NAME model_maker_multi_viewport
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_multi_viewport.py
                    ${CMAKE_CURRENT_BINARY_DIR})
            add_test(NAME model_maker_startup_gpu
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_startup_gpu.py
                    ${CMAKE_CURRENT_BINARY_DIR})
            # F10 bir SISTEM TUSUDUR: WM_SYSKEYDOWN'da islenmeli. Sekil
            # kontrolu + gercek togglePolarTracking govdesinin davranisi.
            add_test(NAME model_maker_polar_f10
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_polar_f10.py
                    ${CMAKE_CURRENT_BINARY_DIR})
            # Calisma duzlemi hayaleti: her pencerede cizilir, secilemez ve
            # snap'lenemez. Sekil kontrolu + gercek cizim blogunun davranisi.
            add_test(NAME model_maker_workplane_ghost
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_workplane_ghost.py
                    ${CMAKE_CURRENT_BINARY_DIR})
            # Tekla gridi gorunus duzlemine gore cizilir: pencerede yalniz
            # XY / XZ / YZ gridi gorunur, "3B grid" OLUSMAZ (dokuman kurali).
            add_test(NAME model_maker_grid_view_plane
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_grid_view_plane.py
                    ${CMAKE_CURRENT_BINARY_DIR})
            # MODERN VIEWCUBE kablolamasi: kup gercekten ciziliyor mu (yalniz
            # kendi paneli), birakmada animasyonlu hedef uygulanıyor mu, yuz
            # kose kumeleri normalleriyle uyusuyor mu (Top/Front karisikligi
            # regresyonu), olu bildirimler kaldirildi mi.
            add_test(NAME model_maker_view_cube
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_view_cube.py)
            # Plan / iki noktali (gorunus duzlemi) pencerelerinde PROFIL UC
            # TUTAMAKLARI gorunur: eski `!draft.viewSlab` kosulu onlari
            # tamamen gizliyordu. Dilim disindaki katilar yine atlanir.
            add_test(NAME model_maker_grip_slab_views
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_grip_slab_views.py
                    ${CMAKE_CURRENT_BINARY_DIR})
            # Snap isaretcileri: kullanici "buyuklugunu iki katina buyutelim" dedi.
            # Tek kaynak cizim fonksiyonu + her sembol icin olcegin TAM 2x oldugu
            # (gercek govde, kayit tutan GDI siniri).
            add_test(NAME model_maker_snap_marker_scale
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_snap_marker_scale.py
                    ${CMAKE_CURRENT_BINARY_DIR})
            # Ctrl+Tab / Ctrl+Shift+Tab gorunus gecisi: tek yol komut kaydi
            # (view.next/view.previous). Win32/Qt iki yonlendirici kaynak sekli
            # ile kilitli (TAB erken reddi kaldirildi, WM_KEYDOWN Ctrl-gate'li).
            add_test(NAME model_maker_view_tab_cycle
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_view_tab_cycle.py
                    ${CMAKE_CURRENT_SOURCE_DIR})
            # Ekran donus merkezi (R tusu + Ctrl+sol tus): R merkez secer,
            # Ctrl+sol surukleme o merkez etrafinda dondurur, Ctrl+TIK
            # secimi bozulmaz. Sekil + gercek clearRotateCenter/resetView
            # govdelerinin davranisi.
            add_test(NAME model_maker_rotate_center
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_rotate_center.py
                    ${CMAKE_CURRENT_BINARY_DIR})
            # Ortho + OSNAP: F8 acikken obje snap'i ortho'yu EZER (AutoCAD
            # osnap > ortho). Sekil kontrolu + gercek applyOrthoConstraint
            # govdesinin cagri sozlesmesi (tum dallarda preserve=true).
            add_test(NAME model_maker_ortho_snap
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_ortho_snap.py
                    ${CMAKE_CURRENT_BINARY_DIR})
            # TP (gecici izleme noktasi): TAB ile koy, Esc ile sifirla.
            # Dwell (450 ms otomatik kayit) KALDIRILDI. Sekil + gercek
            # acquireTemporaryTrackingPoint/clearTemporaryTracking davranisi.
            add_test(NAME model_maker_tp_tab
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tp_tab.py
                    ${CMAKE_CURRENT_BINARY_DIR})
            # Iki noktali gorunus: tasima/kopyalama noktalari is duzlemine iz
            # dusurulur (projected). Sekil + gercek SnapEngine/Camera davranisi.
            add_test(NAME model_maker_view_workplane_move
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_view_workplane_move.py
                    ${CMAKE_CURRENT_BINARY_DIR})
            # Profil kesiti referans normali: "profil ust basligi workplane ile
            # paralel". Uygulama baglantisi (referans uretimden gecirilir +
            # katiyla saklanir + gumball saklanani kullanir) sekil kontrolu.
            add_test(NAME model_maker_profile_section_reference
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_profile_section_reference.py)
            # KONTROL NOKTASI COKLU SECIMI: Alt pencere / Shift+Alt pencere,
            # move + move-delta'nin kumeyi tasimasi, yalniz secili profillerin
            # noktalarinin secilebilmesi (uygulama baglantisi sekil testi).
            add_test(NAME model_maker_grip_selection_wiring
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_grip_selection_wiring.py)
            # KAYIT/OKUMA GUARD kablolamasi: acilista kaydedilmis mesh dogrulanir,
            # yeniden uretim elemani kaydiriyorsa KAYIT korunur; parametrelerden
            # ureten yollar kesit anchor'ini da gecirir.
            add_test(NAME model_maker_reload_mesh_guard_wiring
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_reload_mesh_guard_wiring.py)
            # Props paneli = TEK profil kaynagi (ribbon profil cubugu kaldirildi):
            # secim yokken aktif profil cizimi belirler, secim varken secime atar;
            # satirda ENTER (returnPressed/editingFinished) dogrudan uygular.
            add_test(NAME model_maker_props_profile_panel
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_props_profile_panel.py
                    ${CMAKE_CURRENT_BINARY_DIR})
            # Props paneli ozellikleri SECILI elemana uygulanir; ozellikle KOLON
            # BOYU (Kolon Us/Alt) artik secili kolona uygulanir (eskiden yalniz
            # sonraki yerlesim icin saklaniyordu, secimden hic okunmuyordu) ve
            # Renk satirinda ByLayer secmek override'i temizler. Sekil + gercek
            # applyColumnLevels/selectedEntityColumnLevels govdelerinin davranisi.
            add_test(NAME model_maker_props_column_levels
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_props_column_levels.py
                    ${CMAKE_CURRENT_BINARY_DIR})
            # A (veri butunlugu): props paneli YALNIZ UYGULA'da yazar — rotasyon
            # odak-cikisi (editingFinished) ile DEGIL, kirli bayragi (valueChanged)
            # + UYGULA ile uygulanir; coklu secimde degistirilmeyen alan (rotasyon/
            # kolon kotu) yazilmaz. Doldurma tum alanlarda QSignalBlocker.
            add_test(NAME model_maker_props_panel_apply_only
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_props_panel_apply_only.py
                    ${CMAKE_CURRENT_BINARY_DIR})
            # Gorunum komutlari TEK menude + TEK ribbon sekmesinde toplanir ve
            # gorunus ekleme akisi PLAN gorunusu (XY'ye PARALEL, verilen Z
            # kotunda) uretir; kot 0/negatif olabilir (requestLevelParameter).
            add_test(NAME model_maker_view_menu
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_view_menu.py
                    ${CMAKE_CURRENT_BINARY_DIR})
            # Derin zoom'da referans katmani maliyeti: grid akslari ekran
            # uzayinda kirpilir + UCS glifi EKRAN-OLCEKLI olur. Sekil kontrolu
            # + uretim grid blogunun GERCEK Camera ile sahte kaydedici GDI
            # sinirinda davranisi (cizilen toplam uzunluk zoom'dan bagimsiz).
            add_test(NAME model_maker_zoom_reference_clip
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_zoom_reference_clip.py
                    ${CMAKE_CURRENT_BINARY_DIR})
            # SANAL PROFIL MERKEZI'nin UI baglantisi: tur tablolari (etiket/
            # marker/renk), TEK KAYNAK maske boyutu, Win32 OSNAP listesi +
            # onay kutusu dizisi kilidi (static_assert) ve Qt kurdele menusu.
            # Davranis katmani model_maker_profile_center_snap_test'te.
            add_test(NAME model_maker_profile_center_snap
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_profile_center_snap.py
                    ${CMAKE_CURRENT_BINARY_DIR})
        endif()
    endif()

    add_executable(model_maker_view_definition_test tests/view_definition_test.cpp)
    target_link_libraries(model_maker_view_definition_test PRIVATE model_maker_core)
    add_test(NAME model_maker_view_definition_test COMMAND model_maker_view_definition_test)

    add_executable(model_maker_color_contrast_test tests/color_contrast_test.cpp)
    target_include_directories(model_maker_color_contrast_test PRIVATE include)
    add_test(NAME model_maker_color_contrast_test COMMAND model_maker_color_contrast_test)

    add_executable(model_maker_profile_test tests/profile_database_test.cpp)
    target_link_libraries(model_maker_profile_test PRIVATE model_maker_core)
    add_test(NAME model_maker_profile_test COMMAND model_maker_profile_test)

    # Profil kesit cercevesi kurali (alt baslik XY'ye paralel) — OCC'siz de
    # kosar; kati uretimi ve gumball ayni fonksiyonu kullanir.
    add_executable(model_maker_section_frame_test tests/section_frame_test.cpp)
    target_link_libraries(model_maker_section_frame_test PRIVATE model_maker_core)
    add_test(NAME model_maker_section_frame_test COMMAND model_maker_section_frame_test)

    # Yüzeye göre görünüş (Tekla "view to surface"): yüz çerçevesi + 100/100 mm
    # varsayılan derinlik. Çekirdek (OCC'siz) — komutun kanıtı burada.
    add_executable(model_maker_view_face_test tests/view_face_test.cpp)
    target_link_libraries(model_maker_view_face_test PRIVATE model_maker_core)
    add_test(NAME model_maker_view_face_test COMMAND model_maker_view_face_test)

    # MM-5 adim 1: ComponentGenerator type-erased cagrilabilir. Fonksiyon
    # isaretcisi kayitlari AYNEN calisir; paket yukleyici recipe'yi capture eden
    # lambda kaydedebilir; bos/istisna firlatan uretec sessiz basari URETMEZ.
    add_executable(model_maker_component_generator_type_test
        tests/component_generator_type_test.cpp)
    target_link_libraries(model_maker_component_generator_type_test PRIVATE model_maker_core)
    add_test(NAME model_maker_component_generator_type_test
             COMMAND model_maker_component_generator_type_test)

    # MM-5 fix (item 4): alin levhasi kiris kesimi (PlaneCut, levhanin kiris yuzu).
    add_executable(model_maker_end_plate_cut_test tests/end_plate_cut_test.cpp)
    target_link_libraries(model_maker_end_plate_cut_test PRIVATE model_maker_core)
    add_test(NAME model_maker_end_plate_cut_test COMMAND model_maker_end_plate_cut_test)

    # MODELDEN KURULAN GIRDI ALTIN TESTI: Document -> componentInputFromDocument
    # -> generateHaunch40/generateEndPlate -> digest. Altin testlerin kor noktasi
    # (elde kurulan girdi) kapandi; GUI'nin kullandigi yolun birebir aynisi kosar.
    add_executable(model_maker_component_model_input_golden_test
        tests/component_model_input_golden_test.cpp)
    target_link_libraries(model_maker_component_model_input_golden_test PRIVATE model_maker_core)
    target_compile_definitions(model_maker_component_model_input_golden_test PRIVATE
        MM_CONTRACT_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden"
        MM_PROFILES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/profiles")
    add_test(NAME model_maker_component_model_input_golden_test
             COMMAND model_maker_component_model_input_golden_test)

    # E / secenek C: baglanti ucu GEOMETRIDEN — ayni kiris iki cizim yonuyle
    # (axisFrom/axisTo yer degistirmis) AYNI MemberGeometry + AYNI digest;
    # belirsiz (iki uc esit uzaklik) ACIK ret. End Plate rot 0/90 + Haunch 40.
    add_executable(model_maker_component_connection_end_test
        tests/component_connection_end_test.cpp)
    target_link_libraries(model_maker_component_connection_end_test PRIVATE model_maker_core)
    target_compile_definitions(model_maker_component_connection_end_test PRIVATE
        MM_PROFILES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/profiles")
    add_test(NAME model_maker_component_connection_end_test
             COMMAND model_maker_component_connection_end_test)

    # A (veri butunlugu): profileRotation belgede HAM saklanir (270 -> 270);
    # kanoniklestirme (cift-simetrik 270->90) YALNIZ komponent girdisi/digest
    # icindir — belgeye/panele/kayda kanonik deger GERI YAZILMAZ.
    add_executable(model_maker_rotation_no_writeback_test
        tests/rotation_no_writeback_test.cpp)
    target_link_libraries(model_maker_rotation_no_writeback_test PRIVATE model_maker_core)
    add_test(NAME model_maker_rotation_no_writeback_test
             COMMAND model_maker_rotation_no_writeback_test)

    # MM-7 item 2: VARSAYILAN End Plate kapsam testi — tum IPE/HEA/HEB kiris
    # kesitleri x {M16,M20,M24} x {HEA300,HEB300}: varsayilan plan ya checks'ten
    # gecer ya da ACIK "desteklenmiyor" verir (sessiz/beklenmeyen ret yasak).
    add_executable(model_maker_component_endplate_coverage_test
        tests/component_endplate_coverage_test.cpp)
    target_link_libraries(model_maker_component_endplate_coverage_test PRIVATE model_maker_core)
    target_compile_definitions(model_maker_component_endplate_coverage_test PRIVATE
        MM_PROFILES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/profiles")
    add_test(NAME model_maker_component_endplate_coverage_test
             COMMAND model_maker_component_endplate_coverage_test)

    # MM-7 duzeltme: bolt_rows_reduced UYARI check'i + ACIK satir fail-closed.
    add_executable(model_maker_component_bolt_rows_reduced_test
        tests/component_bolt_rows_reduced_test.cpp)
    target_link_libraries(model_maker_component_bolt_rows_reduced_test PRIVATE model_maker_core)
    target_compile_definitions(model_maker_component_bolt_rows_reduced_test PRIVATE
        MM_PROFILES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/profiles")
    add_test(NAME model_maker_component_bolt_rows_reduced_test
             COMMAND model_maker_component_bolt_rows_reduced_test)

    # Kesit referans normali: "profil ust basligi workplane ile paralel".
    # Secim (UCS vs otomatik gorunus duzlemi) + MMW6 kalicilik + eski dosya
    # uyumlulugu + donusumlerde normalin donmesi. OCC'siz, Linux'ta kosar.
    add_executable(model_maker_section_normal_test tests/section_normal_test.cpp)
    target_link_libraries(model_maker_section_normal_test PRIVATE model_maker_core)
    add_test(NAME model_maker_section_normal_test COMMAND model_maker_section_normal_test)

    # HIZLI SECIM ("ayni profilleri sec"): Document::modelsWithSameProfile —
    # ayni profilAd'li (buyuk/kucuk harf duyarsiz) tum modellerin indeksleri.
    # OCC'siz, Linux'ta kosar.
    add_executable(model_maker_select_same_profile_test tests/select_same_profile_test.cpp)
    target_link_libraries(model_maker_select_same_profile_test PRIVATE model_maker_core)
    add_test(NAME model_maker_select_same_profile_test COMMAND model_maker_select_same_profile_test)

    # Dikme (perpendicular) izleme kurali: iki izleme noktasina dik ayak
    # gorunur + snap noktasi olur — OCC'siz de kosar.
    add_executable(model_maker_perpendicular_tracking_test tests/perpendicular_tracking_test.cpp)
    target_link_libraries(model_maker_perpendicular_tracking_test PRIVATE model_maker_core)
    add_test(NAME model_maker_perpendicular_tracking_test COMMAND model_maker_perpendicular_tracking_test)

    # Ekran donus merkezi (R tusu): secilen pivot etrafinda yorunge donusu.
    # Pivot EKRANDA sabit kalir, aci semantigi degismez; pivot temizleyen/
    # koruyan yollar (reset/setView/setViewBasis vs pan/zoom) kilitlenir.
    # OCC'siz, Linux'ta kosar; gercek Camera::rotateAbout()'i cagirir.
    add_executable(model_maker_orbit_pivot_test tests/orbit_pivot_test.cpp)
    target_link_libraries(model_maker_orbit_pivot_test PRIVATE model_maker_core)
    add_test(NAME model_maker_orbit_pivot_test COMMAND model_maker_orbit_pivot_test)

    # TURNTABLE ORBIT (profesyonel 3B navigasyon): yaw dunya Z, pitch kamera X,
    # dunya-yukari Z korunur, roll birikmez, pitch ±89° sinirli. OCC'siz.
    add_executable(model_maker_turntable_orbit_test tests/turntable_orbit_test.cpp)
    target_link_libraries(model_maker_turntable_orbit_test PRIVATE model_maker_core)
    add_test(NAME model_maker_turntable_orbit_test COMMAND model_maker_turntable_orbit_test)

    # Ortho + OSNAP kurali: F8 acikken obje snap'i ortho'yu EZER (AutoCAD
    # osnap > ortho), serbest imlec ve grid snap'i yine eksene kilitlenir —
    # 2B ve 3B (dunya + is duzlemi eksenleri). OCC'siz de kosar; gercek
    # applyOrtho()/applyOrtho3D() fonksiyonlarini cagirir.
    add_executable(model_maker_ortho_snap_core_test tests/ortho_snap_core_test.cpp)
    target_link_libraries(model_maker_ortho_snap_core_test PRIVATE model_maker_core)
    add_test(NAME model_maker_ortho_snap_core_test COMMAND model_maker_ortho_snap_core_test)

    # Iki noktali gorunuste tasima/kopyalama: secilen nokta IS DUZLEMINE iz
    # dusurulur (normal bileseni atilir). Gorunus duzleminin normali = bakis
    # yonu oldugu icin ekranda bir sey oynamaz; ama duzlem disi geometriye
    # snap'te nesne derinlige kaymaz. OCC'siz de kosar.
    add_executable(model_maker_view_workplane_move_test tests/view_workplane_move_test.cpp)
    target_link_libraries(model_maker_view_workplane_move_test PRIVATE model_maker_core)
    add_test(NAME model_maker_view_workplane_move_test COMMAND model_maker_view_workplane_move_test)

    # TEKLA-TARZI YAPI GRIDI: koordinat dili ("N*deger" tekrari, X/Y artimsal,
    # Z mutlak), X/Y/Z etiketleri, Left/Below - Right/Above uzantilari, origin
    # offset, spesifikasyondan turetim, MMW7 kalicilik + MMW5/6 geriye donuk
    # uyumluluk ve grid ekle/degistir/sil undo'su. OCC'siz/platformdan bagimsiz.
    # KOMUT KAYDI (UI-2): tek komut tablosu — id/ad/ipucu/ikon/kisayol/tur/
    # etkinlik + serit ve menu planlari. Qt'siz/windows.h'siz: 'G' cakismasi
    # gibi hatalar burada TESTLE yakalanir.
    add_executable(model_maker_command_registry_test tests/command_registry_test.cpp)
    target_link_libraries(model_maker_command_registry_test PRIVATE model_maker_core)
    target_compile_options(model_maker_command_registry_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_command_registry_test COMMAND model_maker_command_registry_test)

    # KOMUT KATALOĞU (UI-2): gercek komut tablosu + serit/menu plani
    # sozlesmeleri ('G' hatasi, sekme duzeni, menu tam envanteri).
    add_executable(model_maker_command_catalog_test tests/command_catalog_test.cpp)
    target_link_libraries(model_maker_command_catalog_test PRIVATE model_maker_core)
    target_compile_options(model_maker_command_catalog_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_command_catalog_test COMMAND model_maker_command_catalog_test)

    add_executable(model_maker_grid_test tests/grid_test.cpp)
    target_link_libraries(model_maker_grid_test PRIVATE model_maker_core)
    add_test(NAME model_maker_grid_test COMMAND model_maker_grid_test)

    # TEKLA -> MODEL MAKER BRIDGE: surumlu paket okuyucu + semantik esleyici
    # (import preview). OCC'siz; fixture paketini okur ve dogrular.
    add_executable(model_maker_bridge_package_reader_test tests/bridge_package_reader_test.cpp)
    target_link_libraries(model_maker_bridge_package_reader_test PRIVATE model_maker_core)
    target_compile_definitions(model_maker_bridge_package_reader_test PRIVATE
        MM_BRIDGE_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tools/model-maker-bridge/fixtures")
    add_test(NAME model_maker_bridge_package_reader_test COMMAND model_maker_bridge_package_reader_test)

    # Bridge V3.1 Stage 2: generated 144/146/40 legacy packages fed through
    # the EXISTING C++ V3 parser and semantic preview. OCC-independent.
    add_executable(model_maker_bridge_v31_package_test tests/bridge_v31_package_test.cpp)
    target_link_libraries(model_maker_bridge_v31_package_test PRIVATE model_maker_core)
    target_compile_definitions(model_maker_bridge_v31_package_test PRIVATE
        MM_BRIDGE_V31_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/tools/tekla_bridge_v31/examples")
    add_test(NAME model_maker_bridge_v31_package_test COMMAND model_maker_bridge_v31_package_test)

    # V3.1 Stage 3C: source-observed-only hole drilling guard, without OCC.
    # Optional live 146 package can be passed to executable for manual smoke.
    add_executable(model_maker_bridge_auto_drill_policy_test
        tests/bridge_auto_drill_policy_test.cpp)
    target_link_libraries(model_maker_bridge_auto_drill_policy_test PRIVATE model_maker_core)
    add_test(NAME model_maker_bridge_auto_drill_policy_test
        COMMAND model_maker_bridge_auto_drill_policy_test)
    # Stage 4: independently verified DSTV NC1 -> native editable 146 inputs.
    add_executable(model_maker_bridge_nc146_test tests/bridge_nc146_test.cpp)
    target_link_libraries(model_maker_bridge_nc146_test PRIVATE model_maker_core)
    add_test(NAME model_maker_bridge_nc146_test COMMAND model_maker_bridge_nc146_test)


    # TEKLA -> NATIVE PARAMETRIC COMPONENT esleme: 146 paketini okur, parametreleri
    # provenance ile cikarir, native generator esdegerligini degerlendirir. OCC'siz.
    add_executable(model_maker_bridge_native_mapper_test tests/bridge_native_mapper_test.cpp)
    target_link_libraries(model_maker_bridge_native_mapper_test PRIVATE model_maker_core)
    target_compile_definitions(model_maker_bridge_native_mapper_test PRIVATE
        MM_BRIDGE_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tools/model-maker-bridge/fixtures")
    target_compile_options(model_maker_bridge_native_mapper_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_bridge_native_mapper_test COMMAND model_maker_bridge_native_mapper_test)

    # TEKLA -> NATIVE uye baglama: Tekla GUID -> EntityId esleme (oncelik: kalici
    # GUID, kullanici secimi; belirsizde otomatik secim YOK) + MMW8 round-trip.
    add_executable(model_maker_bridge_native_bind_test tests/bridge_native_bind_test.cpp)
    target_link_libraries(model_maker_bridge_native_bind_test PRIVATE model_maker_core)
    target_compile_options(model_maker_bridge_native_bind_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_bridge_native_bind_test COMMAND model_maker_bridge_native_bind_test)

    # KULLANICI COMPONENT KATALOGU: kalici/tasinabilir derived sablonlar
    # (JSON round-trip, benzersiz typeId, family generator miras, register,
    # derive, missing-family fallback). OCC'siz.
    add_executable(model_maker_user_catalog_test tests/user_catalog_test.cpp)
    target_link_libraries(model_maker_user_catalog_test PRIVATE model_maker_core)
    target_compile_options(model_maker_user_catalog_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_user_catalog_test COMMAND model_maker_user_catalog_test)

    # COMPONENT AYAR SETLERI (Tekla Save / Save as / Load): adlandirilmis parametre
    # setleri, JSON gidis-donus, bilesen basina izole, 'standard' ozel adi. OCC'siz.
    add_executable(model_maker_component_presets_test tests/component_presets_test.cpp)
    target_link_libraries(model_maker_component_presets_test PRIVATE model_maker_core)
    target_compile_options(model_maker_component_presets_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_component_presets_test COMMAND model_maker_component_presets_test)

    # NUMBERING / MARKING CORE (P1): P1/P2.. A1/A2.. kullaniciya gorunen mark, EntityId'den ayri.
    add_executable(model_maker_marking_test tests/marking_test.cpp)
    target_link_libraries(model_maker_marking_test PRIVATE model_maker_core)
    target_compile_options(model_maker_marking_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_marking_test COMMAND model_maker_marking_test)

    # FABRICATION FEATURES (P2A): Document -> tipli imalat ozellikleri (delik/levha/uye datumu), uydurma YOK.
    add_executable(model_maker_fabrication_test tests/fabrication_test.cpp)
    target_include_directories(model_maker_fabrication_test PRIVATE tests)
    target_link_libraries(model_maker_fabrication_test PRIVATE model_maker_core)
    target_compile_options(model_maker_fabrication_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_fabrication_test COMMAND model_maker_fabrication_test)

    # DRAWING P2A: otomatik imalat olculeri + akilli yerlesim (fixture: uc levhasi + 2 bagimsiz grup + oteli levha).
    add_executable(model_maker_drawing_fab_test tests/drawing_fab_test.cpp)
    target_include_directories(model_maker_drawing_fab_test PRIVATE tests)
    target_link_libraries(model_maker_drawing_fab_test PRIVATE model_maker_core)
    target_compile_options(model_maker_drawing_fab_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_drawing_fab_test COMMAND model_maker_drawing_fab_test)

    # DRAWING P2D: gorunus basina olcek + en az buyutme (tek pafta sigdirma kullanici vakasi).
    add_executable(model_maker_drawing_sheet_fit_test tests/drawing_sheet_fit_test.cpp)
    target_include_directories(model_maker_drawing_sheet_fit_test PRIVATE tests)
    target_link_libraries(model_maker_drawing_sheet_fit_test PRIVATE model_maker_core)
    target_compile_options(model_maker_drawing_sheet_fit_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_drawing_sheet_fit_test COMMAND model_maker_drawing_sheet_fit_test)

    # COMPONENT KOPYA (C3): kopyalanan elemanlar component/bulon olarak da kopyalanir.
    add_executable(model_maker_component_copy_test tests/component_copy_test.cpp)
    target_link_libraries(model_maker_component_copy_test PRIVATE model_maker_core)
    target_compile_options(model_maker_component_copy_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_component_copy_test COMMAND model_maker_component_copy_test)

    # COMPONENT REBIND (C4): kopyalanan component yeni konumdaki ana/ara parcalara baglanir.
    add_executable(model_maker_component_rebind_test tests/component_rebind_test.cpp)
    target_link_libraries(model_maker_component_rebind_test PRIVATE model_maker_core)
    target_compile_options(model_maker_component_rebind_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_component_rebind_test COMMAND model_maker_component_rebind_test)

    # DOGRUSAL KOPYA/TASIMA: "dx,dy,dz" oteleme cozumleyici (X/Y/Z mesafeli komutlar).
    add_executable(model_maker_axis_delta_test tests/axis_delta_test.cpp)
    target_link_libraries(model_maker_axis_delta_test PRIVATE model_maker_core)
    target_compile_options(model_maker_axis_delta_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_axis_delta_test COMMAND model_maker_axis_delta_test)

    # DOGRUSAL KOPYA ADEDI: k*delta ofsetleri (adet N -> N kopya, tek undo).
    add_executable(model_maker_linear_delta_offsets_test tests/linear_delta_offsets_test.cpp)
    target_link_libraries(model_maker_linear_delta_offsets_test PRIVATE model_maker_core)
    target_compile_options(model_maker_linear_delta_offsets_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_linear_delta_offsets_test COMMAND model_maker_linear_delta_offsets_test)

    # DRAWING P2C: smart sheet manager (tek pafta kalite kapisi + bagli coklu pafta seti, JSON, referans).
    add_executable(model_maker_drawing_set_test tests/drawing_set_test.cpp)
    target_include_directories(model_maker_drawing_set_test PRIVATE tests)
    target_link_libraries(model_maker_drawing_set_test PRIVATE model_maker_core)
    target_compile_options(model_maker_drawing_set_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_drawing_set_test COMMAND model_maker_drawing_set_test)

    # DRAWING P2D: belirleyici en-iyi sigdirma (detaylari ayni paftada toplama, sabit/otomatik kagit, durum/plan).
    add_executable(model_maker_drawing_p2d_sheet_test tests/drawing_p2d_sheet_test.cpp)
    target_include_directories(model_maker_drawing_p2d_sheet_test PRIVATE tests)
    target_link_libraries(model_maker_drawing_p2d_sheet_test PRIVATE model_maker_core)
    target_compile_options(model_maker_drawing_p2d_sheet_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_drawing_p2d_sheet_test COMMAND model_maker_drawing_p2d_sheet_test)

    # FABRICATION P2D: bagimsiz (elle acilmis) deliklerin envanteri + kaynak tabanli olculendirme.
    add_executable(model_maker_p2d_holes_test tests/p2d_holes_test.cpp)
    target_include_directories(model_maker_p2d_holes_test PRIVATE tests)
    target_link_libraries(model_maker_p2d_holes_test PRIVATE model_maker_core)
    target_compile_options(model_maker_p2d_holes_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_p2d_holes_test COMMAND model_maker_p2d_holes_test)

    # DRAWING P2D/P2: pencere-kesisme secimi + snap'li grup tasima cekirdegi (Qt'siz).
    add_executable(model_maker_drawing_p2d_select_test tests/drawing_p2d_select_move_test.cpp)
    target_include_directories(model_maker_drawing_p2d_select_test PRIVATE tests)
    target_link_libraries(model_maker_drawing_p2d_select_test PRIVATE model_maker_core)
    target_compile_options(model_maker_drawing_p2d_select_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_drawing_p2d_select_test COMMAND model_maker_drawing_p2d_select_test)

    # ILERLEME: assembly cizimi uretiminin GERCEK asamalarini bildirir; sonucu degistirmez (Qt'siz).
    add_executable(model_maker_drawing_set_progress_test tests/drawing_set_progress_test.cpp)
    target_include_directories(model_maker_drawing_set_progress_test PRIVATE tests)
    target_link_libraries(model_maker_drawing_set_progress_test PRIVATE model_maker_core)
    target_compile_options(model_maker_drawing_set_progress_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_drawing_set_progress_test COMMAND model_maker_drawing_set_progress_test)

    # DRAWING P2B: paper-space 2B duzenleme cekirdegi (hit-test, tasima, sekil, undo/redo, kalicilik).
    add_executable(model_maker_paper_editing_test tests/paper_editing_test.cpp)
    target_include_directories(model_maker_paper_editing_test PRIVATE tests)
    target_link_libraries(model_maker_paper_editing_test PRIVATE model_maker_core)
    target_compile_options(model_maker_paper_editing_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_paper_editing_test COMMAND model_maker_paper_editing_test)

    # DRAWING FOUNDATION (P1): Document -> mark -> Front/Top/Side -> A3 yerlesim -> mark/olcu -> JSON.
    add_executable(model_maker_drawing_test tests/drawing_test.cpp)
    target_link_libraries(model_maker_drawing_test PRIVATE model_maker_core)
    target_compile_options(model_maker_drawing_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_drawing_test COMMAND model_maker_drawing_test)
    add_executable(model_maker_sections_test tests/sections_test.cpp)
    target_link_libraries(model_maker_sections_test PRIVATE model_maker_core)
    target_compile_options(model_maker_sections_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_sections_test COMMAND model_maker_sections_test)
    # EĞİK KİRİŞTE HAUNCH 40: düşey alın levhası (kolona oturur), düşey kesim,
    # düşey ön kenarlı gövde. Yatay kiriş regresyonu aynı testte.
    add_executable(model_maker_haunch_inclined_test tests/haunch_inclined_test.cpp)
    target_link_libraries(model_maker_haunch_inclined_test PRIVATE model_maker_core)
    target_compile_options(model_maker_haunch_inclined_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_haunch_inclined_test COMMAND model_maker_haunch_inclined_test)

    # PORTAL / EAVES (gerçek kullanıcı modeli): dönük kolon + Top ankrajlı eğik
    # kirişte alın levhası kolon yüzüne oturur ve kiriş levhanın arka yüzünde
    # kesilir. Önizleme ile modelin aynı yeri göstermesini kilitler.
    add_executable(model_maker_haunch_portal_face_test tests/haunch_portal_face_test.cpp)
    target_link_libraries(model_maker_haunch_portal_face_test PRIVATE model_maker_core)
    target_compile_options(model_maker_haunch_portal_face_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_haunch_portal_face_test COMMAND model_maker_haunch_portal_face_test)

    # GRID KESISIM SNAP TOLERANSI: tolerans disindaki aks kesisimine snap YOK,
    # obje snap'i grid'i ezer (Selahattin 2026-10-31).
    add_executable(model_maker_grid_snap_distance_test tests/grid_snap_distance_test.cpp)
    target_link_libraries(model_maker_grid_snap_distance_test PRIVATE model_maker_core)
    target_compile_options(model_maker_grid_snap_distance_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_grid_snap_distance_test COMMAND model_maker_grid_snap_distance_test)

    # IS-DUZLEMI (UCS): egik duzlemde atilan profilin KESITI duzlemde kalmali
    # (ref normal = is-duzlemi normali; kolonda duzlem-ici u).
    add_executable(model_maker_workplane_member_orientation_test tests/workplane_member_orientation_test.cpp)
    target_link_libraries(model_maker_workplane_member_orientation_test PRIVATE model_maker_core)
    target_compile_options(model_maker_workplane_member_orientation_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_workplane_member_orientation_test COMMAND model_maker_workplane_member_orientation_test)

    # YAN/ON GORUNUSTE SNAP: is duzlemi bakisa paralel kalirsa snap tamamen
    # olur; bakisa DIK duzlem (fromViewDirection) sart.
    add_executable(model_maker_side_view_snap_test tests/side_view_snap_test.cpp)
    target_link_libraries(model_maker_side_view_snap_test PRIVATE model_maker_core)
    target_compile_options(model_maker_side_view_snap_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_side_view_snap_test COMMAND model_maker_side_view_snap_test)

    # UCS DELTA: kopyala/tasima paneli ve "dx,dy,dz" girdisi is duzlemi
    # eksenlerinde; dunya donusumu tek kaynak (round-trip).
    add_executable(model_maker_workplane_delta_test tests/workplane_delta_test.cpp)
    target_link_libraries(model_maker_workplane_delta_test PRIVATE model_maker_core)
    target_compile_options(model_maker_workplane_delta_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_workplane_delta_test COMMAND model_maker_workplane_delta_test)

    # VIEWCUBE: yuz hedefleri + ISO yerlesimi (bos yuz / yanlis gorunus regresyonu).
    add_executable(model_maker_viewcube_layout_test tests/viewcube_layout_test.cpp)
    target_link_libraries(model_maker_viewcube_layout_test PRIVATE model_maker_core)
    target_compile_options(model_maker_viewcube_layout_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_viewcube_layout_test COMMAND model_maker_viewcube_layout_test)

    # UYE UC MERKEZI (grip tutamagi kesit ortasinda; eksen ucunda degil)
    add_executable(model_maker_member_end_center_test tests/member_end_center_test.cpp)
    target_link_libraries(model_maker_member_end_center_test PRIVATE model_maker_core)
    target_compile_options(model_maker_member_end_center_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_member_end_center_test COMMAND model_maker_member_end_center_test)

    # GORUNUS PENCERESI SNAP IZDUSUMU (plan/yan gorunuste nokta gorunus
    # duzleminde kalmali; is duzlemine izdusum ekranda kaydiriyordu)
    add_executable(model_maker_view_plane_snap_projection_test tests/view_plane_snap_projection_test.cpp)
    target_link_libraries(model_maker_view_plane_snap_projection_test PRIVATE model_maker_core)
    target_compile_options(model_maker_view_plane_snap_projection_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_view_plane_snap_projection_test COMMAND model_maker_view_plane_snap_projection_test)

    # SNAP OVERRIDE MASKESI (komut icinde sag-tik popup ile tur ac/kapat)
    add_executable(model_maker_snap_override_mask_test tests/snap_override_mask_test.cpp)
    target_link_libraries(model_maker_snap_override_mask_test PRIVATE model_maker_core)
    target_compile_options(model_maker_snap_override_mask_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_snap_override_mask_test COMMAND model_maker_snap_override_mask_test)

    # KONTROL NOKTASI COKLU SECIMI: Alt pencere (eskileri birak) / Shift+Alt
    # pencere (eskileri koru, yenileri ekle) + yalniz secili profillerin
    # noktalarinin secilebilmesi. Gercek kamera projeksiyonuyla olculur.
    add_executable(model_maker_grip_selection_test tests/grip_selection_test.cpp)
    target_link_libraries(model_maker_grip_selection_test PRIVATE model_maker_core)
    target_compile_options(model_maker_grip_selection_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_grip_selection_test COMMAND model_maker_grip_selection_test)

    # KAYIT/OKUMA GUARD: acilista parametrelerden yeniden uretim kaydedilmis
    # mesh'i toleransin uzerinde oynatiyorsa KAYIT korunur (eleman kaymaz).
    # "sonradan attigim capraz elemanlar yuklendiginde farkli yerlerde cikti".
    add_executable(model_maker_reload_mesh_guard_test tests/reload_mesh_guard_test.cpp)
    target_link_libraries(model_maker_reload_mesh_guard_test PRIVATE model_maker_core)
    target_compile_options(model_maker_reload_mesh_guard_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_reload_mesh_guard_test COMMAND model_maker_reload_mesh_guard_test)

    # KARARLI VARLIK KIMLIGI (EntityId): vektor indeksi kalici kimlik DEGIL;
    # kimlik ekleme/silme/degistirme/undo/redo boyunca DOGRU varlikla eslesir,
    # tekil ve monoton artar. 1:1 replace kimligi korur, kopya yeni kimlik alir,
    # MMW1-7 yukleme deterministik/catismasiz kimlik uretir. OCC'siz kosar.
    add_executable(model_maker_entity_id_test tests/entity_id_test.cpp)
    target_link_libraries(model_maker_entity_id_test PRIVATE model_maker_core)
    add_test(NAME model_maker_entity_id_test COMMAND model_maker_entity_id_test)

    # KIRIS YUKU (BeamLoad) KIMLIK (EntityId) ANAHTARI + yasam dongusu:
    # indeks-tabanli anahtarin sessiz hatasi (silme/siralama sonrasi kaydin
    # BASKA varliga baglanmasi) kilitlenir; clear()/load() temizligi ve yeni
    # varligin devralmamasi dogrulanir. OCC'siz/platformdan bagimsiz.
    add_executable(model_maker_beam_load_id_test tests/beam_load_id_test.cpp)
    target_link_libraries(model_maker_beam_load_id_test PRIVATE model_maker_core)
    add_test(NAME model_maker_beam_load_id_test COMMAND model_maker_beam_load_id_test)

    # BELGE-SAHIPLI B-Rep MAGAZASI (opak OccStore): Document yasam dongusu
    # (kur/yik/tasi), clear()/load()'un magazayi SIFIRLAMASI ve no-op store
    # davranisi. OCC'siz/platformdan bagimsiz kosar; GERCEK OCC magazasi
    # (OccSolidStore) OCC'li harness'te sinanir.
    add_executable(model_maker_occ_store_test tests/occ_store_test.cpp)
    target_link_libraries(model_maker_occ_store_test PRIVATE model_maker_core)
    add_test(NAME model_maker_occ_store_test COMMAND model_maker_occ_store_test)

    # MMW8 dosya codec'i (OCCT'siz): binary B-Rep blob'lari + SHA-256 butunluk
    # + EntityId/nextEntityId + ileriye-uyumlu bloklar. Phase 3.4B.
    add_executable(model_maker_mmw8_codec_test tests/mmw8_codec_test.cpp)
    target_link_libraries(model_maker_mmw8_codec_test PRIVATE model_maker_core)
    add_test(NAME model_maker_mmw8_codec_test COMMAND model_maker_mmw8_codec_test)

    # MMW8 Document entegrasyonu (Phase 3.4C) — OCC-KAPALI senaryolar: EntityId/
    # nextEntityId/ozellik/grid round-trip, undo temizligi, atomik yukleme,
    # MMW1->MMW8 yukari donusum. Gercek B-Rep round-trip OCC'li harness'te.
    add_executable(model_maker_mmw8_document_test tests/mmw8_document_test.cpp)
    target_link_libraries(model_maker_mmw8_document_test PRIVATE model_maker_core)
    add_test(NAME model_maker_mmw8_document_test COMMAND model_maker_mmw8_document_test)

    # Kontur (siluet + gercek kenar) ureticisi: GL Solid yolunun her karede
    # kosturdugu pass'in tek kaynagi. Eski std::map uygulamasiyla esdegerlik,
    # determinizm, workspace yeniden kullanimi ve oryantasyon duyarliligi
    # kilitlenir. OCC'siz/platformdan bagimsiz.
    add_executable(model_maker_contour_builder_test tests/contour_builder_test.cpp)
    target_link_libraries(model_maker_contour_builder_test PRIVATE model_maker_core)
    add_test(NAME model_maker_contour_builder_test COMMAND model_maker_contour_builder_test)

    # Kontur pass'inin kare maliyeti + onbellek anahtarinin gecerliligi
    # (oryantasyon tabani zoom'da sabit, rotate'te degisken). Zamanlama
    # yalniz RAPORLANIR; test esdegerlik ve degismezlik uzerinden gecer.
    add_executable(model_maker_zoom_cost_probe tests/zoom_cost_probe.cpp)
    target_link_libraries(model_maker_zoom_cost_probe PRIVATE model_maker_core)
    add_test(NAME model_maker_zoom_cost_probe COMMAND model_maker_zoom_cost_probe 2000)

    # SNAP DERINLIK TERCIHI + SOLID GORUNURLUK FILTRESI (plan gorunusu):
    #  esit ekran mesafesinde IZLEYICIYE YAKIN aday kazanir (gorunur ust kenar,
    #  altindaki gizli kenari yener); yuzu olmayan modelde (cizgi/polyline) ya
    #  da normal yonu ic/dis olan OCC katisinda kenarlar ELENMEZ. OCC'siz kosar;
    #  gercek SnapEngine::snap3D()'yi cagirir.
    add_executable(model_maker_snap_depth_pick_test tests/snap_depth_pick_test.cpp)
    target_link_libraries(model_maker_snap_depth_pick_test PRIVATE model_maker_core)
    add_test(NAME model_maker_snap_depth_pick_test COMMAND model_maker_snap_depth_pick_test)

    # SNAP AYARLARI (UI-6a): tasinabilir serilestirme + varsayilan degerler.
    # Dort deger (marker olcegi, kalem kalinligi, tutma suresi, tolerans carpani)
    # kullanicidan degistirilebilir ve oturumlar arasi hatirlanir. Header-only;
    # serialize/parse gidis-donusu + clamp davranisi kilitlenir.
    add_executable(model_maker_snap_settings_test tests/snap_settings_test.cpp)
    target_include_directories(model_maker_snap_settings_test PRIVATE include)
    add_test(NAME model_maker_snap_settings_test COMMAND model_maker_snap_settings_test)

    # YAPI GRIDI TUM SNAP TURLERINE YAPISMALI (kullanici sarti): aks cizgileri
    # uc/orta/en yakin/dikme adaylari uretir; her turun kapisi snap maskesi;
    # kesisimler SnapType::Grid kalir; obje snap'i grid'i ezer; eski toleranssiz
    # kesisim davranisi korunur. OCC'siz kosar; GERCEK snap/snap3D cagirir.
    add_executable(model_maker_grid_snap_types_test tests/grid_snap_types_test.cpp)
    target_link_libraries(model_maker_grid_snap_types_test PRIVATE model_maker_core)
    add_test(NAME model_maker_grid_snap_types_test COMMAND model_maker_grid_snap_types_test)

    # SANAL PROFIL MERKEZI (kullanici sarti: "profilin tam merkezinde bir sanal
    # snap noktasi olustur"): profil elemaninin sinir kutusu merkezi gercek bir
    # kose/kenar olmasa da snap noktasi olur. Yalniz profil elemanlari icin
    # uretilir; maske kapisi, uc onceligi, tolerans, coklu profil, 2B/3B ve
    # Wireframe/Solid esdegerligi kilitlenir. OCC'siz; gercek snap/snap3D.
    add_executable(model_maker_profile_center_snap_test tests/profile_center_snap_test.cpp)
    target_link_libraries(model_maker_profile_center_snap_test PRIVATE model_maker_core)
    add_test(NAME model_maker_profile_center_snap_test COMMAND model_maker_profile_center_snap_test)

    # MODERN VIEWCUBE: 26 yon (6 yuz + 12 kenar + 8 kose) + gecis animasyonu.
    # Kullanici sarti "kendi view cube umuzu modernlestirelim". Yuz hedefleri
    # Ctrl+1..7 ile AYNI matrise gider (tek kaynak), kenar/kose hedefleri ayni
    # turntable ailesinden turetilir; EN GUCLU INVARYANT: her hedefin rotasyon
    # matrisinin 3. satiri hedef yonudur. Slerp ara kareleri ortonormal ve tek
    # yonlu; hitTest yuz/kenar/kose/ISO'yu AYIRT eder ve GIZLI ozelligi secmez.
    # OCC'siz/platformdan bagimsiz; gercek ViewCube + gercek Camera.
    add_executable(model_maker_view_cube_targets_test tests/view_cube_targets_test.cpp)
    target_link_libraries(model_maker_view_cube_targets_test PRIVATE model_maker_core)
    add_test(NAME model_maker_view_cube_targets_test COMMAND model_maker_view_cube_targets_test)

    # OLcum probe'u: iki noktali XZ/YZ gorunusunde grid snap'i HANGI kotlere
    # gidiyor? ("sadece xy deki alt grid noktalarina snap oluyor" sikayetinin
    # kaniti). Gercek SnapEngine + Camera + grid; iddia yok — cikti raporlanir.
    add_executable(model_maker_grid_snap_view_plane_probe tests/grid_snap_view_plane_probe.cpp)
    target_link_libraries(model_maker_grid_snap_view_plane_probe PRIVATE model_maker_core)
    add_test(NAME model_maker_grid_snap_view_plane_probe COMMAND model_maker_grid_snap_view_plane_probe)

    # REFERANS KATMANI EKRAN-UZAYI: "zoom derinlestikce cok yavasliyor" sikayeti.
    # Sabit referans katmani (grid akslari + UCS glifi) DUNYA biriminde tanimli
    # oldugu icin kirpma/olcek kurali olmadan zoom arttikca cizilen ekran
    # uzunlugu patliyordu (olcum: 8.091 -> 1.864.015 px, 231x zoom). Test hem
    # kirpma/glif yardimcilarini dogrudan sinar hem GERCEK Camera + grid ile
    # derin zoom taramasinda cizilen toplamin SINIRLI kaldigini olcer.
    # OCC'siz/platformdan bagimsiz.
    add_executable(model_maker_reference_layer_test tests/reference_layer_test.cpp)
    target_link_libraries(model_maker_reference_layer_test PRIVATE model_maker_core)
    add_test(NAME model_maker_reference_layer_test COMMAND model_maker_reference_layer_test)

    # Derin zoom'da referans katmaninin cizilen EKRAN uzunlugu (olcum probe'u).
    # Zamanlama/iddia yok — olcumu tekrarlanabilir kilar ve dosyanin rotlamasini
    # onler. `zoom_deep_cost_probe` cikitisi referans_layer.hpp basligindaki
    # sayilarin kaynagidir.
    add_executable(model_maker_zoom_deep_cost_probe tests/zoom_deep_cost_probe.cpp)
    target_link_libraries(model_maker_zoom_deep_cost_probe PRIVATE model_maker_core)
    add_test(NAME model_maker_zoom_deep_cost_probe COMMAND model_maker_zoom_deep_cost_probe)

    # test_core.cpp Windows-only: renderer.hpp kosulsuz <windows.h> icerir.
    if(WIN32)
        add_executable(model_maker_tests tests/test_core.cpp)
        target_link_libraries(model_maker_tests PRIVATE model_maker_core)
        target_link_options(model_maker_tests PRIVATE -static -static-libgcc -static-libstdc++)
        target_compile_options(model_maker_tests PRIVATE -Wall -Wextra -Wpedantic)
        add_test(NAME model_maker_tests COMMAND model_maker_tests)
    else()
        message(STATUS "model_maker_tests (test_core.cpp) Windows-only — Linux'ta atlaniyor")
    endif()

    if(MODELMAKER_OCC_READY)
        add_executable(model_maker_occ_smoke tests/occ_smoke.cpp src/occ_bridge.cpp)
        target_link_libraries(model_maker_occ_smoke PRIVATE model_maker_occ)
        add_test(NAME model_maker_occ_smoke COMMAND model_maker_occ_smoke)
        # KOPYA + B-REP: kopya parcanin magazada B-Rep'i yok (parametre
        # degisikliginin birlesimi degistirmemesinin kok nedeni) + kopyanin
        # kalici profil ozelliklerinden yeniden uretilebilirligi.
        add_executable(model_maker_occ_copy_brep_gap_test tests/occ_copy_brep_gap_test.cpp)
        target_link_libraries(model_maker_occ_copy_brep_gap_test PRIVATE model_maker_occ)
        add_test(NAME model_maker_occ_copy_brep_gap_test
                 COMMAND model_maker_occ_copy_brep_gap_test)
        # TESHIS ARACI (test degil): kaydedilmis .mmw icin imalat/olcu raporu.
        # Kullanim: model_maker_drawing_fab_probe <model.mmw>
        add_executable(model_maker_drawing_fab_probe tools/drawing_fab_probe.cpp)
        target_link_libraries(model_maker_drawing_fab_probe PRIVATE model_maker_occ)
        if(UNIX)
            # ctest ortaminda OCCT paylasimli kutuphaneleri bul (Windows'ta
            # DLL'ler exe yaninda/ PATH'te olur — bu blok yalniz Linux).
            set_tests_properties(model_maker_occ_smoke PROPERTIES
                ENVIRONMENT "LD_LIBRARY_PATH=${OCC_ROOT}/lib:$ENV{LD_LIBRARY_PATH}")
        endif()

        # NATIVE BINARY B-Rep SERIALIZER round-trip (Phase 3.4A). Gercek OCCT
        # ile Create->Serialize->Deserialize->Validate->Boolean akisini sinar.
        add_executable(model_maker_occ_brep_serialize_test tests/occ_brep_serialize_test.cpp)
        target_link_libraries(model_maker_occ_brep_serialize_test PRIVATE model_maker_occ)
        add_test(NAME model_maker_occ_brep_serialize_test
                 COMMAND model_maker_occ_brep_serialize_test)
        if(UNIX)
            set_tests_properties(model_maker_occ_brep_serialize_test PROPERTIES
                ENVIRONMENT "LD_LIBRARY_PATH=${OCC_ROOT}/lib:$ENV{LD_LIBRARY_PATH}")
        endif()

        # KESME MODIFIER uygulamasi (Phase 4.2): Fitting "kisa taraf otomatik
        # atilir" + PlaneCut keepPositive + coklu modifier zinciri + base
        # immutabilitesi (gercek OCCT). Linux'ta OCC-disabled build'de kurulmaz;
        # orada tests/occ_cut_probe.cpp basligindaki g++ komutuyla ayri kosulur.
        add_executable(model_maker_occ_cut_probe tests/occ_cut_probe.cpp)
        target_link_libraries(model_maker_occ_cut_probe PRIVATE model_maker_occ)
        add_test(NAME model_maker_occ_cut_probe COMMAND model_maker_occ_cut_probe)
        if(UNIX)
            set_tests_properties(model_maker_occ_cut_probe PROPERTIES
                ENVIRONMENT "LD_LIBRARY_PATH=${OCC_ROOT}/lib:$ENV{LD_LIBRARY_PATH}")
        endif()

        # P1 KABUL SENARYOSU (Fitting + egimli duzlem + tasima->recompute):
        # HEB300 I-kesit -> egimli plane -> Fitting -> duzlem tasinir -> taraf
        # tersine doner. Gercek OCCT; Linux'ta OCC-disabled build'de kurulmaz.
        add_executable(model_maker_occ_fitting_acceptance tests/occ_fitting_acceptance.cpp)
        target_link_libraries(model_maker_occ_fitting_acceptance PRIVATE model_maker_occ)
        add_test(NAME model_maker_occ_fitting_acceptance COMMAND model_maker_occ_fitting_acceptance)
        if(UNIX)
            set_tests_properties(model_maker_occ_fitting_acceptance PROPERTIES
                ENVIRONMENT "LD_LIBRARY_PATH=${OCC_ROOT}/lib:$ENV{LD_LIBRARY_PATH}")
        endif()

        # GERCEK I-KESIT KONTURU kabulu: I-kesit tek KAPALI kontur yuzu olarak
        # uretilir (radius r + IPE/IPN %14 ic yuz egimi); kesit alani katalog
        # A degerine %3 icinde yaklasmali, bbox olculeri dogru olmali.
        add_executable(model_maker_occ_isection_acceptance tests/occ_isection_acceptance.cpp)
        target_link_libraries(model_maker_occ_isection_acceptance PRIVATE model_maker_occ)
        add_test(NAME model_maker_occ_isection_acceptance COMMAND model_maker_occ_isection_acceptance)
        if(UNIX)
            set_tests_properties(model_maker_occ_isection_acceptance PROPERTIES
                ENVIRONMENT "LD_LIBRARY_PATH=${OCC_ROOT}/lib:$ENV{LD_LIBRARY_PATH}")
        endif()

        # REAL OCCT B-REP CONTACT RESOLVER (Phase R2c): gercek TopoDS_Shape
        # yuz esleme + kesim dogrulama (flans/govde/delik/dondurme/paralel).
        add_executable(model_maker_occ_contact_resolver_test tests/occ_contact_resolver_test.cpp)
        target_link_libraries(model_maker_occ_contact_resolver_test PRIVATE model_maker_occ)
        add_test(NAME model_maker_occ_contact_resolver_test COMMAND model_maker_occ_contact_resolver_test)
        if(UNIX)
            set_tests_properties(model_maker_occ_contact_resolver_test PROPERTIES
                ENVIRONMENT "LD_LIBRARY_PATH=${OCC_ROOT}/lib:$ENV{LD_LIBRARY_PATH}")
        endif()

        # OCC MAGAZA + BELGE MMW8 round-trip regresyonu (P12): kayit/ac sonrasi
        # restoreBaseSolid ETKIN surumu ezmemeli (kesilmis kati, kesilmemis
        # tabanla degistirilmemeli). Gercek OCCT; Linux'ta OCC-disabled build'de
        # kurulmaz (basliklardaki g++ tek-satir komutuyla ayri kosulur).
        add_executable(model_maker_occ_store_roundtrip tests/occ_store_roundtrip_test.cpp)
        target_link_libraries(model_maker_occ_store_roundtrip PRIVATE model_maker_occ)
        add_test(NAME model_maker_occ_store_roundtrip COMMAND model_maker_occ_store_roundtrip)
        add_executable(model_maker_occ_document_roundtrip tests/occ_document_roundtrip_test.cpp)
        target_link_libraries(model_maker_occ_document_roundtrip PRIVATE model_maker_occ)
        add_test(NAME model_maker_occ_document_roundtrip COMMAND model_maker_occ_document_roundtrip)
        # BULON GRUBU MMW8 round-trip (Phase B1): kayit + delik (owner) + display
        # montaj B-Rep'i kayit/ac sonrasi duzenlenebilir kalir.
        add_executable(model_maker_occ_bolt_group_roundtrip tests/occ_bolt_group_roundtrip_test.cpp)
        target_link_libraries(model_maker_occ_bolt_group_roundtrip PRIVATE model_maker_occ)
        add_test(NAME model_maker_occ_bolt_group_roundtrip COMMAND model_maker_occ_bolt_group_roundtrip)
        # END PLATE MMW8 ROUND-TRIP + RECOMPUTE (Slice 2d-3, Slice D): kayit/ac
        # sonrasi baglanti kimlikleri + 2x4 M20 parametreleri + delikler korunur
        # ve kolon degisiminde levha/bulon/delik GERCEK OCCT ile yeniden hesaplanir.
        add_executable(model_maker_occ_end_plate_roundtrip tests/occ_end_plate_roundtrip_acceptance.cpp)
        target_link_libraries(model_maker_occ_end_plate_roundtrip PRIVATE model_maker_occ)
        add_test(NAME model_maker_occ_end_plate_roundtrip COMMAND model_maker_occ_end_plate_roundtrip)
        # END PLATE YERLESIMI SECTIONANCHOR KABULU: levha kirisin GERCEK kesit
        # MERKEZINE oturur (eksene DEGIL); Top/Center/Bottom + 30° kolon + kot
        # + bulonlar GERCEK OCCT B-Rep ile sayisal dogrulanir.
        add_executable(model_maker_occ_end_plate_placement tests/occ_end_plate_placement_acceptance.cpp)
        target_link_libraries(model_maker_occ_end_plate_placement PRIVATE model_maker_occ)
        add_test(NAME model_maker_occ_end_plate_placement COMMAND model_maker_occ_end_plate_placement)
        # TEKLA 144 M1 GEOMETRY: web tarafi temas (None rol) + negatif flans +
        # profile rotation levha cercevesi — GERCEK OCCT ile sayisal dogrulanir.
        add_executable(model_maker_occ_end_plate_144_geometry tests/occ_end_plate_144_geometry_test.cpp)
        target_link_libraries(model_maker_occ_end_plate_144_geometry PRIVATE model_maker_occ)
        add_test(NAME model_maker_occ_end_plate_144_geometry COMMAND model_maker_occ_end_plate_144_geometry)
        # GERCEK OCC KAYNAK KATISI (Phase W3): fillet weld katisinin hacmi/
        # BRepCheck + T-joint temas kenari tespiti + tek/cift tarafli + ayrik
        # fallback. GERCEK OCCT ile sayisal dogrulanir.
        add_executable(model_maker_occ_weld_geometry_test tests/occ_weld_geometry_test.cpp)
        target_link_libraries(model_maker_occ_weld_geometry_test PRIVATE model_maker_occ)
        add_test(NAME model_maker_occ_weld_geometry_test COMMAND model_maker_occ_weld_geometry_test)
        if(UNIX)
            set_tests_properties(model_maker_occ_weld_geometry_test PROPERTIES
                ENVIRONMENT "LD_LIBRARY_PATH=${OCC_ROOT}/lib:$ENV{LD_LIBRARY_PATH}")
        endif()
        # KAYNAK KATISI MMW8 ROUND-TRIP (Phase W3): weld + display model + OCC kati
        # kayit/ac sonrasi korunur (displayEntityId, boyut, kati hacmi, assembly).
        add_executable(model_maker_occ_weld_document_roundtrip tests/occ_weld_document_roundtrip_test.cpp)
        target_link_libraries(model_maker_occ_weld_document_roundtrip PRIVATE model_maker_occ)
        add_test(NAME model_maker_occ_weld_document_roundtrip COMMAND model_maker_occ_weld_document_roundtrip)
        if(UNIX)
            set_tests_properties(model_maker_occ_weld_document_roundtrip PROPERTIES
                ENVIRONMENT "LD_LIBRARY_PATH=${OCC_ROOT}/lib:$ENV{LD_LIBRARY_PATH}")
        endif()
        # GERCEK I-PROFIL T-BIRLESTIMI KAYNAK KATISI (W3-FIX regresyon): cornerFaceNormal
        # kaynak yonune paralel normali olan "uc" yuzunu secip cross(d,n) dejenere
        # oluyordu ("BRepCheck gecersiz"); HEB300 kolon + IPE200 kiris ile dogrulanir.
        add_executable(model_maker_occ_weld_profile_beam_test tests/occ_weld_profile_beam_test.cpp)
        target_link_libraries(model_maker_occ_weld_profile_beam_test PRIVATE model_maker_occ)
        add_test(NAME model_maker_occ_weld_profile_beam_test COMMAND model_maker_occ_weld_profile_beam_test)
        if(UNIX)
            set_tests_properties(model_maker_occ_weld_profile_beam_test PROPERTIES
                ENVIRONMENT "LD_LIBRARY_PATH=${OCC_ROOT}/lib:$ENV{LD_LIBRARY_PATH}")
        endif()
        # KOSE YERLESIMI (regresyon): levha kenari kolonun kosе cizgisine denk
        # gelince cornerFaceNormal sinirsiz DUZLEM mesafesiyle yanlis yuzu secip
        # fillet'i kolonun ICINE dusuruyordu. Kaynak merkezi iki parcanin da
        # DISINDA olmali (BRepClass3d_SolidClassifier ile dogrulanir).
        add_executable(model_maker_occ_weld_corner_placement_test tests/occ_weld_corner_placement_test.cpp)
        target_link_libraries(model_maker_occ_weld_corner_placement_test PRIVATE model_maker_occ)
        add_test(NAME model_maker_occ_weld_corner_placement_test COMMAND model_maker_occ_weld_corner_placement_test)
        if(UNIX)
            set_tests_properties(model_maker_occ_weld_corner_placement_test PROPERTIES
                ENVIRONMENT "LD_LIBRARY_PATH=${OCC_ROOT}/lib:$ENV{LD_LIBRARY_PATH}")
        endif()
        # BRepClass3d'SIZ NOKTA SINIFLANDIRMA (Windows GCC16 OCCT cokmesi):
        # pointSolidState, Linux'ta saglam BRepClass3d oracle'i ile izgarada
        # birebir ayni karari vermeli (kutu/silindir/delikli levha/L kesit).
        add_executable(model_maker_occ_point_solid_state_test tests/occ_point_solid_state_test.cpp)
        target_link_libraries(model_maker_occ_point_solid_state_test PRIVATE model_maker_occ)
        add_test(NAME model_maker_occ_point_solid_state_test COMMAND model_maker_occ_point_solid_state_test)
        if(UNIX)
            set_tests_properties(model_maker_occ_point_solid_state_test PROPERTIES
                ENVIRONMENT "LD_LIBRARY_PATH=${OCC_ROOT}/lib:$ENV{LD_LIBRARY_PATH}")
        endif()
        # src altinda BRepClass3d_SolidClassifier YASAK (Windows'ta SIGSEGV).
        add_test(NAME model_maker_no_brepclass3d_in_src
                 COMMAND ${CMAKE_COMMAND} -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/src
                         -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_no_brepclass3d.cmake)
        # UNIVERSAL COMPONENT MATERIALIZER (C2): ComponentBuilder -> ComponentPlan
        # -> gercek Document + OccSolidStore. roleKey->EntityId + ownership + delik
        # + Weld Unsupported + coklu bagimsiz component. GERCEK OCCT.
        add_executable(model_maker_component_materializer_test tests/component_materializer_test.cpp)
        target_link_libraries(model_maker_component_materializer_test PRIVATE model_maker_occ)
        add_test(NAME model_maker_component_materializer_test COMMAND model_maker_component_materializer_test)
        # LEVHA CERCEVESI EL YONU (CONTRACT.md kurali): sag + sol elli levha,
        # kalinlik +axis, kose numaralari materializer nokta sirasina gore.
        # ST-4 (GUI-3): modeldeki TUM Plate varliklari kPlateGreyRgb olmali
        # (Haunch 40 govde levhasi + paket levhasi dahil). Gercek OCCT yolu.
        add_executable(model_maker_component_plate_color_test tests/component_plate_color_test.cpp)
        target_link_libraries(model_maker_component_plate_color_test PRIVATE model_maker_occ)
        target_compile_definitions(model_maker_component_plate_color_test PRIVATE
            MM_PACKAGES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/libs/mm_contract/packages")
        add_test(NAME model_maker_component_plate_color_test COMMAND model_maker_component_plate_color_test)
        # KATALOG KOMUT YOLU (MM-REGRESYON): Document -> componentInputFromDocument
        # -> runRecipe(mm_end_plate_pkg) -> materializer -> instance. Uyumlu profil
        # instance olusturur; dar flans/egik kiris ACIK reddedilir (sessiz yok).
        add_executable(model_maker_component_command_path_test tests/component_command_path_test.cpp)
        target_link_libraries(model_maker_component_command_path_test PRIVATE model_maker_occ)
        target_compile_definitions(model_maker_component_command_path_test PRIVATE
            MM_PACKAGES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/libs/mm_contract/packages")
        add_test(NAME model_maker_component_command_path_test COMMAND model_maker_component_command_path_test)
        add_executable(model_maker_plate_frame_handedness_test tests/plate_frame_handedness_test.cpp)
        target_link_libraries(model_maker_plate_frame_handedness_test PRIVATE model_maker_occ)
        add_test(NAME model_maker_plate_frame_handedness_test COMMAND model_maker_plate_frame_handedness_test)
        # FIN PLATE UCTAN UCA (C5): registry (generateFinPlate) -> ComponentPlan
        # -> ComponentMaterializer -> Document/OCCT; recompute + undo/redo + MMW8
        # kayit/ac sonrasi roleKey->EntityId korunur. GERCEK OCCT.
        add_executable(model_maker_occ_fin_plate_e2e tests/occ_fin_plate_end_to_end_test.cpp)
        target_link_libraries(model_maker_occ_fin_plate_e2e PRIVATE model_maker_occ)
        add_test(NAME model_maker_occ_fin_plate_e2e COMMAND model_maker_occ_fin_plate_e2e)
        # TEKLA 146 BRIDGE OCC IMPORT (Windows GUI import altyapisi): fixture-146
        # shear-plate-v2 okunur -> plaka kati + 6 M20 delik + Document atomik
        # import + undo/redo + negatif (dis bulon delinmez) + rollback. GERCEK OCCT.
        # (Eskiden yalniz elle g++ ile derleniyordu; artik kalici ctest hedefi.)
        add_executable(model_maker_occ_bridge_146_import_test tests/occ_bridge_146_import_test.cpp)
        target_link_libraries(model_maker_occ_bridge_146_import_test PRIVATE model_maker_occ)
        target_compile_definitions(model_maker_occ_bridge_146_import_test PRIVATE
            MM_BRIDGE_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tools/model-maker-bridge/fixtures")
        add_test(NAME model_maker_occ_bridge_146_import_test
                 COMMAND model_maker_occ_bridge_146_import_test)
        # TEKLA BRIDGE V3 OCC IMPORT: cok parcali + mesh->solid + fitting/boolean
        # metadata + duplicate import + bos-objects rollback. GERCEK OCCT.
        add_executable(model_maker_occ_bridge_v3_import_test tests/occ_bridge_v3_import_test.cpp)
        target_link_libraries(model_maker_occ_bridge_v3_import_test PRIVATE model_maker_occ)
        target_compile_definitions(model_maker_occ_bridge_v3_import_test PRIVATE
            MM_BRIDGE_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tools/model-maker-bridge/fixtures")
        add_test(NAME model_maker_occ_bridge_v3_import_test
                 COMMAND model_maker_occ_bridge_v3_import_test)
        # TEKLA BRIDGE NATIVE DONUSUM: convertBridgeToNative (GUID baglama +
        # generator + gercek OCC solid + GUID iliskisi). GERCEK OCCT.
        add_executable(model_maker_occ_bridge_native_convert_test tests/occ_bridge_native_convert_test.cpp)
        target_link_libraries(model_maker_occ_bridge_native_convert_test PRIVATE model_maker_occ)
        add_test(NAME model_maker_occ_bridge_native_convert_test
                 COMMAND model_maker_occ_bridge_native_convert_test)
        # TEKLA 40 NATIVE HAUNCH: tapered web + inclined flange + end plate,
        # two 2x2 bolt groups, BRep materialization and undo/redo.
        add_executable(model_maker_occ_haunch40_native_e2e_test
            tests/occ_haunch40_native_e2e_test.cpp)
        target_link_libraries(model_maker_occ_haunch40_native_e2e_test PRIVATE model_maker_occ)
        add_test(NAME model_maker_occ_haunch40_native_e2e_test
            COMMAND model_maker_occ_haunch40_native_e2e_test)
        # MOCK <> DÜĞÜM TUTARLILIĞI (portal_1.mmw): alın levhası ve kiriş kesimi
        # planda (mock) ve OCC düğümünde AYNI ölçüde olmalı. GERCEK OCCT.
        add_executable(model_maker_occ_haunch_portal_node_test
            tests/occ_haunch_portal_node_test.cpp)
        target_link_libraries(model_maker_occ_haunch_portal_node_test PRIVATE model_maker_occ)
        add_test(NAME model_maker_occ_haunch_portal_node_test
            COMMAND model_maker_occ_haunch_portal_node_test)
        # TEKLA 146 UCTAN UCA (cok kolonlu bulon + hacim + recompute). GERCEK OCCT.
        # PART E: 2x3 6xM20 delikli levha hacmi + kalinlik 10->12 recompute.
        add_executable(model_maker_occ_shear_plate_146_end_to_end_test tests/occ_shear_plate_146_end_to_end_test.cpp)
        target_link_libraries(model_maker_occ_shear_plate_146_end_to_end_test PRIVATE model_maker_occ)
        add_test(NAME model_maker_occ_shear_plate_146_end_to_end_test
                 COMMAND model_maker_occ_shear_plate_146_end_to_end_test)
        # TEKLA 146 BOLTS sekmesi: yuvali/oversize/No hole/tolerance/stagger/uzunluk
        # artisi — analitik hacimle. GERCEK OCCT.
        add_executable(model_maker_occ_shear_plate_146_bolts_test tests/occ_shear_plate_146_bolts_test.cpp)
        target_link_libraries(model_maker_occ_shear_plate_146_bolts_test PRIVATE model_maker_occ)
        add_test(NAME model_maker_occ_shear_plate_146_bolts_test
                 COMMAND model_maker_occ_shear_plate_146_bolts_test)
        # TEKLA 146 STIFFENERS: berkitme hacmi/konumu/pah (analitik). GERCEK OCCT.
        add_executable(model_maker_occ_shear_plate_146_stiffener_test tests/occ_shear_plate_146_stiffener_test.cpp)
        target_link_libraries(model_maker_occ_shear_plate_146_stiffener_test PRIVATE model_maker_occ)
        add_test(NAME model_maker_occ_shear_plate_146_stiffener_test
                 COMMAND model_maker_occ_shear_plate_146_stiffener_test)
        # END PLATE REGRESYON (A3): mevcut production hatti vs yeni ortak runtime.
        # Ayni yerlesim iki yoldan beslenir; geometri/bulon/delik/ownership esit.
        add_executable(model_maker_occ_end_plate_regression_vs_runtime tests/occ_end_plate_regression_vs_runtime.cpp)
        target_link_libraries(model_maker_occ_end_plate_regression_vs_runtime PRIVATE model_maker_occ)
        add_test(NAME model_maker_occ_end_plate_regression_vs_runtime COMMAND model_maker_occ_end_plate_regression_vs_runtime)
        if(UNIX)
            if(DEFINED OCC_LIB_DIR AND NOT OCC_LIB_DIR STREQUAL "")
                set(_occ_ld "${OCC_LIB_DIR}")
            elseif(OCC_ROOT)
                set(_occ_ld "${OCC_ROOT}/lib")
            else()
                set(_occ_ld "")
            endif()
            set_tests_properties(model_maker_occ_store_roundtrip model_maker_occ_document_roundtrip model_maker_occ_bolt_group_roundtrip model_maker_occ_end_plate_roundtrip model_maker_occ_end_plate_placement model_maker_occ_end_plate_144_geometry model_maker_component_materializer_test model_maker_occ_fin_plate_e2e model_maker_occ_end_plate_regression_vs_runtime model_maker_occ_bridge_146_import_test model_maker_occ_bridge_v3_import_test model_maker_occ_bridge_native_convert_test model_maker_occ_shear_plate_146_end_to_end_test PROPERTIES
                ENVIRONMENT "LD_LIBRARY_PATH=${_occ_ld}:$ENV{LD_LIBRARY_PATH}")
        endif()
    endif()

    # OCC SAHIPLIK REGRESYON HARNESS'I (kaynak-govde cikarimi + GERCEK OCCT).
    # src/application.cpp'deki URETIM govdelerini (addCube / addCylinder /
    # addBoolean{Fuse,Common,Cut} / lastLiveSolids / clearOccSolidStore /
    # addStyledModel) CANLI KAYNAKTAN marker ile cikarir, GERCEK OCCT ile
    # derler ve EntityId <-> TopoDS_Shape yasam dongusunu calistirir.
    # Elle yazilmis bir Application stub'i kullanir → TAM GUI entegrasyon
    # testi DEGILDIR; hedefli kaynak-govde regresyon testidir.
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        if(MODELMAKER_OCC_READY AND OCC_INCLUDE_DIR)
            # Windows/MSYS2: build-occ yapilandirmasi zaten OCC_ROOT ile geldi.
            add_test(NAME model_maker_occ_solid_ids
                COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_occ_solid_ids.py
                    ${CMAKE_CURRENT_BINARY_DIR}
                    --cxx ${CMAKE_CXX_COMPILER}
                    --occt-include ${OCC_INCLUDE_DIR}
                    --occt-libdir ${OCC_ROOT}/lib)
        elseif(UNIX AND NOT CMAKE_CROSSCOMPILING)
            # Linux: sistem OCCT'si (find_package VTK yuzunden kirik olsa da
            # baslik/kutuphaneler dogrudan baglanabilir).
            find_path(MM_SYSTEM_OCC_INCLUDE NAMES BRepPrimAPI_MakeBox.hxx
                      PATH_SUFFIXES opencascade)
            if(MM_SYSTEM_OCC_INCLUDE)
                add_test(NAME model_maker_occ_solid_ids
                    COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_occ_solid_ids.py
                        ${CMAKE_CURRENT_BINARY_DIR}
                        --occt-include ${MM_SYSTEM_OCC_INCLUDE})
                # NATIVE BINARY B-Rep SERIALIZER round-trip (Phase 3.4A).
                add_test(NAME model_maker_occ_brep_serialize
                    COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_occ_brep_serialize.py
                        ${CMAKE_CURRENT_BINARY_DIR}
                        --occt-include ${MM_SYSTEM_OCC_INCLUDE})
            endif()
        endif()
    endif()

    add_executable(model_maker_render_prep_benchmark tests/benchmark_render_prep.cpp)
    target_link_libraries(model_maker_render_prep_benchmark PRIVATE model_maker_core)
    target_link_options(model_maker_render_prep_benchmark PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_render_prep_benchmark PRIVATE -O3 -Wall -Wextra -Wpedantic)

    add_executable(model_maker_spatial_query_benchmark tests/benchmark_spatial_query.cpp)
    target_link_libraries(model_maker_spatial_query_benchmark PRIVATE model_maker_core)
    target_link_options(model_maker_spatial_query_benchmark PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_spatial_query_benchmark PRIVATE -O3 -Wall -Wextra -Wpedantic)

    # 3Dconnexion Navlib kamera-matris koprusu: cameraToWorldMatrix4 ->
    # applyCameraToWorldMatrix4 round-trip (yaw/pitch/roll/center korunur).
    add_executable(model_maker_camera_navlib_tests tests/camera_navlib_roundtrip.cpp)
    target_link_libraries(model_maker_camera_navlib_tests PRIVATE model_maker_core)
    target_link_options(model_maker_camera_navlib_tests PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_camera_navlib_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_camera_navlib_tests COMMAND model_maker_camera_navlib_tests)

    # VARSAYILAN 3B GORUNUM: reset() ve Isometric ayni ust-sol-on kose
    # yonelimi vermeli (XY yere paralel, Z yukari) — kullanici "üst sola 45
    # dereceyle bakmali".
    add_executable(model_maker_camera_default_orientation_tests tests/camera_default_orientation_test.cpp)
    target_link_libraries(model_maker_camera_default_orientation_tests PRIVATE model_maker_core)
    target_link_options(model_maker_camera_default_orientation_tests PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_camera_default_orientation_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_camera_default_orientation_tests COMMAND model_maker_camera_default_orientation_tests)

    # PROJEKSIYON-VIEWPORT SOZLESMESI (BUG-UI-001): sahne merkezi HER viewport
    # boyutunda TAM ekran merkezine gider; viewport boyutu dogrudan haritalamayi
    # belirler (gomulu tuvalin GetClientRect'i mantiksal kalirsa cizim koseye
    # kayar — "yarim pencere"). OCC'siz, Linux'ta kosar.
    add_executable(model_maker_projection_viewport_contract_test tests/projection_viewport_contract_test.cpp)
    target_link_libraries(model_maker_projection_viewport_contract_test PRIVATE model_maker_core)
    target_link_options(model_maker_projection_viewport_contract_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_projection_viewport_contract_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_projection_viewport_contract_test COMMAND model_maker_projection_viewport_contract_test)

    # TEKLA-TARZI UCS + VIEW SISTEMI (P1): global XY/XZ/YZ calisma duzlemi,
    # 1/2/3 noktadan UCS, B-Rep yuzeyinden UCS, parca koordinat sisteminden UCS,
    # UCS uzerinde view, 2/3 noktadan view, yuzeye dik view, named views + list.
    # OCC'siz, Linux'ta kosar.
    add_executable(model_maker_ucs_test tests/ucs_test.cpp)
    target_link_libraries(model_maker_ucs_test PRIVATE model_maker_core)
    target_link_options(model_maker_ucs_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_ucs_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_ucs_test COMMAND model_maker_ucs_test)

    # KONSTRUKSIYON NESNELERI (P2): Construction Point/Line/Plane — ConstructionKind
    # etiketi + standart model yolu (snap/secim/gumball/EntityId/undo otomatik).
    # OCC'siz, Linux'ta kosar.
    add_executable(model_maker_construction_objects_test tests/construction_objects_test.cpp)
    target_link_libraries(model_maker_construction_objects_test PRIVATE model_maker_core)
    target_link_options(model_maker_construction_objects_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_construction_objects_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_construction_objects_test COMMAND model_maker_construction_objects_test)

    # KONSTRUKSIYON DUZLEM CERCEVESI (P0): constructionPlaneFrame() 4 koseden
    # origin/u/v/normal kurar (duzlem eksenleri + P1 kesme duzlemi kaynagi).
    # OCC'siz, Linux'ta kosar.
    add_executable(model_maker_construction_plane_frame_test tests/construction_plane_frame_test.cpp)
    target_link_libraries(model_maker_construction_plane_frame_test PRIVATE model_maker_core)
    target_link_options(model_maker_construction_plane_frame_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_construction_plane_frame_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_construction_plane_frame_test COMMAND model_maker_construction_plane_frame_test)

    # KESME NESNELERI (P3): CutModifier (PlaneCut/Fitting/PolygonCut/BooleanCut)
    # kalici modifier modeli + duzlem/cokgen/kesisim matematigi + Document
    # CRUD/undo. OCC'siz, Linux'ta kosar.
    add_executable(model_maker_cut_modifier_test tests/cut_modifier_test.cpp)
    target_link_libraries(model_maker_cut_modifier_test PRIVATE model_maker_core)
    target_link_options(model_maker_cut_modifier_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_cut_modifier_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_cut_modifier_test COMMAND model_maker_cut_modifier_test)

    # KESME NESNELERI SECIMI (P5): cutModifierPickGeometry + hitTestCutModifier2D/3D
    # (renderer drawCutSelectionHints ile AYNI geometri kaynagi). OCC'siz, Linux'ta kosar.
    add_executable(model_maker_cut_modifier_pick_test tests/cut_modifier_pick_test.cpp)
    target_link_libraries(model_maker_cut_modifier_pick_test PRIVATE model_maker_core)
    target_link_options(model_maker_cut_modifier_pick_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_cut_modifier_pick_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_cut_modifier_pick_test COMMAND model_maker_cut_modifier_pick_test)

    # KESME KAYNAK VARLIGI (P6): CutModifier.source referans cozumu + silme
    # kaskadi (kaynak tasininca/silinince kesme izler/kaldirilir). OCC'siz.
    add_executable(model_maker_cut_modifier_source_test tests/cut_modifier_source_test.cpp)
    target_link_libraries(model_maker_cut_modifier_source_test PRIVATE model_maker_core)
    target_link_options(model_maker_cut_modifier_source_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_cut_modifier_source_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_cut_modifier_source_test COMMAND model_maker_cut_modifier_source_test)

    # PARAMETRIK BULON (Phase B1): BoltGroup veri modeli + delik CutModifier
    # uretimi + MMW8 cap round-trip. OCC'siz, Linux'ta kosar.
    add_executable(model_maker_bolt_test tests/bolt_test.cpp)
    target_link_libraries(model_maker_bolt_test PRIVATE model_maker_core)
    target_link_options(model_maker_bolt_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_bolt_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_bolt_test COMMAND model_maker_bolt_test)

    # BULON GRUBU DOKUMAN (Phase B1, Slice 3a): BoltGroup belge kaydi + delik
    # (owner) + display + undo/redo + MMW8 round-trip + deleteModels kaskadi.
    add_executable(model_maker_bolt_group_document_test tests/bolt_group_document_test.cpp)
    target_link_libraries(model_maker_bolt_group_document_test PRIVATE model_maker_core)
    target_link_options(model_maker_bolt_group_document_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_bolt_group_document_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_bolt_group_document_test COMMAND model_maker_bolt_group_document_test)

    # ALIN LEVHASI BAGLANTISI (Phase C1): EndPlateConnection yerlesim + belge
    # kaydi + alt nesne sahipligi + undo/redo + MMW8 round-trip + kaskad. OCC'siz.
    add_executable(model_maker_end_plate_test tests/end_plate_test.cpp)
    target_link_libraries(model_maker_end_plate_test PRIVATE model_maker_core)
    target_link_options(model_maker_end_plate_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_end_plate_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_end_plate_test COMMAND model_maker_end_plate_test)

    # COMPONENT FOUNDATION (P4): Main/Secondary Part, parametrik Component
    # tanimi, instance + parametreler, bagimlilik + yeniden hesaplama. OCC'siz.
    add_executable(model_maker_component_test tests/component_test.cpp)
    target_link_libraries(model_maker_component_test PRIVATE model_maker_core)
    target_link_options(model_maker_component_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_component_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_component_test COMMAND model_maker_component_test)

    # COMPONENT ENGINE (Phase C2): tipli parametre semasi + registry/factory +
    # uretim plani; End Plate + Fin Plate ayni altyapiyi yeniden kullanir.
    # OCC'siz, Linux'ta kosar.
    add_executable(model_maker_component_engine_test tests/component_engine_test.cpp)
    target_link_libraries(model_maker_component_engine_test PRIVATE model_maker_core)
    target_link_options(model_maker_component_engine_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_component_engine_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_component_engine_test COMMAND model_maker_component_engine_test)

    # UNIVERSAL COMPONENT FRAMEWORK: ComponentBuilder + RelationshipGraph +
    # ComponentObjectSpec (roleKey, cycle/unresolved/conflict). OCC'siz.
    add_executable(model_maker_component_framework_test tests/component_framework_test.cpp)
    target_link_libraries(model_maker_component_framework_test PRIVATE model_maker_core)
    target_link_options(model_maker_component_framework_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_component_framework_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_component_framework_test COMMAND model_maker_component_framework_test)

    # COMPONENT SEMBOLU (GLYPH): collectComponentGlyphs TEK KAYNAK — End Plate +
    # universal parametrik component (Fin Plate) sembolleri, tekillesme, devre
    # disi/orphan/gecersiz atlama. OCC'siz, Linux'ta kosar.
    add_executable(model_maker_component_glyph_test tests/component_glyph_test.cpp)
    target_link_libraries(model_maker_component_glyph_test PRIVATE model_maker_core)
    target_link_options(model_maker_component_glyph_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_component_glyph_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_component_glyph_test COMMAND model_maker_component_glyph_test)

    # COMPONENT KASKAD SILME: ana/ikincil parca silinince tum component silinir.
    add_executable(model_maker_component_cascade_delete_test tests/component_cascade_delete_test.cpp)
    target_link_libraries(model_maker_component_cascade_delete_test PRIVATE model_maker_core)
    target_link_options(model_maker_component_cascade_delete_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_component_cascade_delete_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_component_cascade_delete_test COMMAND model_maker_component_cascade_delete_test)

    # UNIVERSAL COMPONENT DIALOG + SCHEMATIC (Tekla-tarzi dialog): sematik cizim
    # + hotspot <-> parametre baglantisi + parametre gruplari + EndPlateParams
    # adaptoru + dialog registry. OCC'siz, Linux'ta kosar.
    add_executable(model_maker_component_dialog_test tests/component_dialog_test.cpp)
    target_link_libraries(model_maker_component_dialog_test PRIVATE model_maker_core)
    target_link_options(model_maker_component_dialog_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_component_dialog_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_component_dialog_test COMMAND model_maker_component_dialog_test)

    # TEKLA 146 (Shear plate simple) pilotu: tip + generator + dialog tanimi.
    # OCC'siz, Linux'ta kosar; geometri fin plate cekirdegine delege eder.
    add_executable(model_maker_shear_plate_146_test tests/shear_plate_146_test.cpp)
    target_link_libraries(model_maker_shear_plate_146_test PRIVATE model_maker_core)
    target_link_options(model_maker_shear_plate_146_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_shear_plate_146_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_shear_plate_146_test COMMAND model_maker_shear_plate_146_test)

    # TEKLA 146 BOLTS sekmesi: her alanin gercek etkisi (delik tipi/yuva, tolerans,
    # cut length, yatay konum, staggering...). OCC'siz, Linux'ta kosar.
    add_executable(model_maker_shear_plate_146_bolts_test tests/shear_plate_146_bolts_test.cpp)
    target_link_libraries(model_maker_shear_plate_146_bolts_test PRIVATE model_maker_core)
    target_link_options(model_maker_shear_plate_146_bolts_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_shear_plate_146_bolts_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_shear_plate_146_bolts_test COMMAND model_maker_shear_plate_146_bolts_test)

    # TEKLA 146 STIFFENERS sekmesi: karsi-govde berkitmesi uretim plani. OCC'siz.
    add_executable(model_maker_shear_plate_146_stiffener_test tests/shear_plate_146_stiffener_test.cpp)
    target_link_libraries(model_maker_shear_plate_146_stiffener_test PRIVATE model_maker_core)
    target_link_options(model_maker_shear_plate_146_stiffener_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_shear_plate_146_stiffener_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_shear_plate_146_stiffener_test COMMAND model_maker_shear_plate_146_stiffener_test)

    # GENERIC COMPONENT CATALOG (C7) testi: registry+dialog kayitlarindan katalog
    # derlemesi, metadata, generic uretim, dialog varligi. OCC'siz, Linux'ta kosar.
    add_executable(model_maker_component_catalog_test tests/component_catalog_test.cpp)
    target_link_libraries(model_maker_component_catalog_test PRIVATE model_maker_core)
    target_link_options(model_maker_component_catalog_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_component_catalog_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_component_catalog_test COMMAND model_maker_component_catalog_test)

    # MM-8b/3: OTOMATIK SEMATIK (ortografik izdusum, gercek mm) — Qt/OCCT'siz.
    # Bas genisligi / levha kalinligi orani + determinizm dogrulanir.
    add_executable(model_maker_component_schematic_test tests/component_schematic_test.cpp)
    target_link_libraries(model_maker_component_schematic_test PRIVATE model_maker_core)
    target_link_options(model_maker_component_schematic_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_component_schematic_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_component_schematic_test COMMAND model_maker_component_schematic_test)

    # UNIVERSAL COMPONENT DIALOG Qt OFFScreen testi: widget'in gercek davranisi
    # (round-trip, sentinel koruma, bilinmeyen parametre) bassiz dogrulanir.
    # Qt widget regressions run headless on Linux AND Windows (Qt6 available).
    # Real pointer picking and model-overlay edits must be exercised on Windows too.
    if(Qt6_FOUND AND (UNIX OR WIN32) AND NOT CMAKE_CROSSCOMPILING)
        add_executable(model_maker_component_dialog_widget_test
            tests/component_dialog_widget_test.cpp
            src/component_dialog_widget.cpp
            src/component_live_preview_widget.cpp
            include/model_maker/component_live_preview_widget.hpp
            src/component_option_icons.cpp
            include/model_maker/component_dialog_widget.hpp
            resources/tekla146.qrc)
        target_include_directories(model_maker_component_dialog_widget_test PRIVATE include)
        # mm_component_contract: MM-6b autoPreviewDefinition (duyarlilik analizi)
        # widget'ta dogrudan cagriliyor; test kutuphanesi de linklemeli.
        target_link_libraries(model_maker_component_dialog_widget_test PRIVATE
            model_maker_core mm_component_contract Qt6::Widgets)
        target_compile_definitions(model_maker_component_dialog_widget_test PRIVATE
            MM_PACKAGES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/libs/mm_contract/packages")
        add_test(NAME model_maker_component_dialog_widget_test
                 COMMAND model_maker_component_dialog_widget_test)

        # DRAWING GUI (P1): sayfa cizimi (QPainter), PDF, onizleme diyalogu. Offscreen render dogrulamasi.
        add_executable(model_maker_drawing_gui_test
            tests/drawing_gui_test.cpp
            src/drawing_sheet_widget.cpp
            include/model_maker/drawing_sheet_widget.hpp)
        target_include_directories(model_maker_drawing_gui_test PRIVATE include)
        target_link_libraries(model_maker_drawing_gui_test PRIVATE model_maker_core Qt6::Widgets)
        add_test(NAME model_maker_drawing_gui_test COMMAND model_maker_drawing_gui_test)

        # DRAWING GUI (P2A): ayni nihai plan onizleme+PDF, Qt metin genisligi <= yerlesim tahmini, vektor PDF, ekran goruntusu.
        add_executable(model_maker_drawing_fab_gui_test
            tests/drawing_fab_gui_test.cpp
            src/drawing_sheet_widget.cpp
            include/model_maker/drawing_sheet_widget.hpp)
        target_include_directories(model_maker_drawing_fab_gui_test PRIVATE include tests)
        target_link_libraries(model_maker_drawing_fab_gui_test PRIVATE model_maker_core Qt6::Widgets)
        add_test(NAME model_maker_drawing_fab_gui_test COMMAND model_maker_drawing_fab_gui_test)

        # DRAWING GUI (P2C): coklu pafta seti cok-sayfali vektor PDF + pafta gezinme + ekran/PDF paritesi.
        add_executable(model_maker_drawing_set_gui_test
            tests/drawing_set_gui_test.cpp
            src/drawing_sheet_widget.cpp
            include/model_maker/drawing_sheet_widget.hpp)
        target_include_directories(model_maker_drawing_set_gui_test PRIVATE include tests)
        target_link_libraries(model_maker_drawing_set_gui_test PRIVATE model_maker_core Qt6::Widgets)
        add_test(NAME model_maker_drawing_set_gui_test COMMAND model_maker_drawing_set_gui_test)

        # DRAWING GUI (P2B): paftada fare ile sec/tasi, 2B cizgi/daire ekleme, geri al, PDF paritesi.
        add_executable(model_maker_paper_editing_gui_test
            tests/paper_editing_gui_test.cpp
            src/drawing_sheet_widget.cpp
            include/model_maker/drawing_sheet_widget.hpp)
        target_include_directories(model_maker_paper_editing_gui_test PRIVATE include tests)
        target_link_libraries(model_maker_paper_editing_gui_test PRIVATE model_maker_core Qt6::Widgets)
        add_test(NAME model_maker_paper_editing_gui_test COMMAND model_maker_paper_editing_gui_test)

        # DRAWING GUI (P2D): kagit secenegi degisince yeniden uretim + pafta duzenlemelerinin korunmasi,
        # yikici degisiklikte onay, durum satiri kagit/olcek/sebep, PDF/JSON determinizmi.
        add_executable(model_maker_drawing_p2d_gui_test
            tests/drawing_p2d_gui_test.cpp
            src/drawing_sheet_widget.cpp
            include/model_maker/drawing_sheet_widget.hpp)
        target_include_directories(model_maker_drawing_p2d_gui_test PRIVATE include tests)
        target_link_libraries(model_maker_drawing_p2d_gui_test PRIVATE model_maker_core Qt6::Widgets)
        add_test(NAME model_maker_drawing_p2d_gui_test COMMAND model_maker_drawing_p2d_gui_test)

        # DRAWING GUI (P2): pencere-kesisme secimi, snap'li iki-tik tasima, tik-tik cizim, Esc iptali.
        add_executable(model_maker_drawing_p2d_select_gui_test
            tests/drawing_p2d_select_move_gui_test.cpp
            src/drawing_sheet_widget.cpp
            include/model_maker/drawing_sheet_widget.hpp)
        target_include_directories(model_maker_drawing_p2d_select_gui_test PRIVATE include tests)
        target_link_libraries(model_maker_drawing_p2d_select_gui_test PRIVATE model_maker_core Qt6::Widgets)
        add_test(NAME model_maker_drawing_p2d_select_gui_test COMMAND model_maker_drawing_p2d_select_gui_test)

        # ILERLEME PENCERESI (Qt): ekranin ortasinda panel, belirli/belirsiz cubuk, Esc ile kapanmama.
        # SERIT (UI-2): platformdan bagimsiz Qt widget'i — offscreen.
        # Her komut bir kez, tikla->dogru komut, arama, DPI etiketleri, PNG.
        add_executable(model_maker_ribbon_widget_test
            tests/ribbon_widget_test.cpp
            src/ribbon_widget.cpp
            include/model_maker/ribbon_widget.hpp)
        target_include_directories(model_maker_ribbon_widget_test PRIVATE include)
        target_link_libraries(model_maker_ribbon_widget_test PRIVATE
            model_maker_core Qt6::Widgets)
        add_test(NAME model_maker_ribbon_widget_test COMMAND model_maker_ribbon_widget_test)

        # OZELLIKLER PANELI (UI-3a): platformdan bagimsiz Qt widget'i — offscreen.
        # Baslik/bolumler, katalog-bir-kez sozlesmesi, kirli bayraklari,
        # validasyon, UYGULA kurali.
        add_executable(model_maker_properties_panel_test
            tests/properties_panel_test.cpp
            src/properties_panel.cpp
            include/model_maker/properties_panel.hpp)
        target_include_directories(model_maker_properties_panel_test PRIVATE include)
        target_link_libraries(model_maker_properties_panel_test PRIVATE Qt6::Widgets)
        add_test(NAME model_maker_properties_panel_test COMMAND model_maker_properties_panel_test)

        # PROFIL/MALZEME SECIM PENCERESI (UI-4): aile agaci + filtre + senkron
        # + ozellik tablosu + kesit resmi (sematik motor) + Tamam/Uygula/Iptal.
        add_executable(model_maker_profile_picker_dialog_test
            tests/profile_picker_dialog_test.cpp
            src/profile_picker_dialog.cpp
            src/profile_database.cpp
            include/model_maker/profile_picker_dialog.hpp
            include/model_maker/profile_database.hpp)
        target_include_directories(model_maker_profile_picker_dialog_test PRIVATE include)
        target_link_libraries(model_maker_profile_picker_dialog_test PRIVATE Qt6::Widgets)
        add_test(NAME model_maker_profile_picker_dialog_test COMMAND model_maker_profile_picker_dialog_test)

        add_executable(model_maker_progress_overlay_test tests/progress_overlay_test.cpp)
        target_include_directories(model_maker_progress_overlay_test PRIVATE include)
        target_link_libraries(model_maker_progress_overlay_test PRIVATE model_maker_core Qt6::Widgets)
        add_test(NAME model_maker_progress_overlay_test COMMAND model_maker_progress_overlay_test)

        # Offscreen platform: her iki cizim GUI testi icin ENVIRONMENT dogru testlere uygulanir
        # (onceden yanlis hedefe yaziliydi; testler qputenv ile de korunuyor).
        set_tests_properties(model_maker_drawing_p2d_gui_test model_maker_drawing_p2d_select_gui_test
            model_maker_progress_overlay_test
            model_maker_ribbon_widget_test
            model_maker_properties_panel_test
            model_maker_profile_picker_dialog_test
            model_maker_component_dialog_widget_test PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()

    # COMPONENT SERIALIZE (Phase C2b): ComponentInstanceRecord'un MMW8 'I' blogu
    # + Document save/load entegrasyonu; orphan tespiti + param migration +
    # undo/redo. OCC'siz, Linux'ta kosar.
    add_executable(model_maker_component_serialize_test tests/component_serialize_test.cpp)
    target_link_libraries(model_maker_component_serialize_test PRIVATE model_maker_core)
    target_link_options(model_maker_component_serialize_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_component_serialize_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_component_serialize_test COMMAND model_maker_component_serialize_test)

    # END PLATE TEMAS YUZEYI (Phase R1): resolveBoundaryPlaneFacing +
    # computeEndPlatePlacementOnFace (gap). Levha kolon yuzeyine oturur.
    # OCC'siz, Linux'ta kosar.
    add_executable(model_maker_end_plate_contact_test tests/end_plate_contact_test.cpp)
    target_link_libraries(model_maker_end_plate_contact_test PRIVATE model_maker_core)
    target_link_options(model_maker_end_plate_contact_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_end_plate_contact_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_end_plate_contact_test COMMAND model_maker_end_plate_contact_test)

    # CONSTRAINT SYSTEM (Phase R2a): rank-tabanli duzlem position solver.
    # DistanceConstraint + ReferenceDistance + deterministik cozum. OCC'siz.
    add_executable(model_maker_constraint_solver_test tests/constraint_solver_test.cpp)
    target_link_libraries(model_maker_constraint_solver_test PRIVATE model_maker_core)
    target_link_options(model_maker_constraint_solver_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_constraint_solver_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_constraint_solver_test COMMAND model_maker_constraint_solver_test)

    # CONTACT RESOLVER (Phase R2b): MemberLocalFrame + SemanticFaceResolver +
    # ContactPointResolver. Dondurulmus kolon, temas kotu, sonlu yuz. OCC'siz.
    add_executable(model_maker_contact_resolver_test tests/contact_resolver_test.cpp)
    target_link_libraries(model_maker_contact_resolver_test PRIVATE model_maker_core)
    target_link_options(model_maker_contact_resolver_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_contact_resolver_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_contact_resolver_test COMMAND model_maker_contact_resolver_test)

    # GEOMETRY REFERENCE RESOLVER + MAGNETIC FRAME (Phase R2d-1): portable
    # referans cozumleyici arayuzu (MemberFrame/Start/End/SemanticFace/duzlem) +
    # rijit magnetic cerceve (alt nesne dunya koordinati turetimi). OCC'siz.
    add_executable(model_maker_geometry_reference_resolver_test tests/geometry_reference_resolver_test.cpp)
    target_link_libraries(model_maker_geometry_reference_resolver_test PRIVATE model_maker_core)
    target_link_options(model_maker_geometry_reference_resolver_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_geometry_reference_resolver_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_geometry_reference_resolver_test COMMAND model_maker_geometry_reference_resolver_test)

    # COMPONENT DEPENDENCY + RECOMPUTE (Phase R2d-2): parca -> component ters
    # indeks + deterministik recompute planlayici (atomik, mutasyon yok). OCC'siz.
    add_executable(model_maker_component_recompute_test tests/component_recompute_test.cpp)
    target_link_libraries(model_maker_component_recompute_test PRIVATE model_maker_core)
    target_link_options(model_maker_component_recompute_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_component_recompute_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_component_recompute_test COMMAND model_maker_component_recompute_test)

    # SLICE 2d-2c: atomik recompute (prepare-then-commit) + generic adapter.
    add_executable(model_maker_component_recompute_atomic_test tests/component_recompute_atomic_test.cpp)
    target_link_libraries(model_maker_component_recompute_atomic_test PRIVATE model_maker_core)
    target_link_options(model_maker_component_recompute_atomic_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_component_recompute_atomic_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_component_recompute_atomic_test COMMAND model_maker_component_recompute_atomic_test)

    # SLICE 2d-2c: gercek transaction rollback (Document::rollbackOpenRecord).
    add_executable(model_maker_document_transaction_test tests/document_transaction_test.cpp)
    target_link_libraries(model_maker_document_transaction_test PRIVATE model_maker_core)
    target_link_options(model_maker_document_transaction_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_document_transaction_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_document_transaction_test COMMAND model_maker_document_transaction_test)
    add_executable(model_maker_document_property_edit_test tests/document_property_edit_test.cpp)
    target_link_libraries(model_maker_document_property_edit_test PRIVATE model_maker_core)
    target_link_options(model_maker_document_property_edit_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_document_property_edit_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_document_property_edit_test COMMAND model_maker_document_property_edit_test)

    # SLICE 2d-2c: SectionAnchor ofsetleri (Top/Center/Bottom, default Top).
    add_executable(model_maker_section_anchor_test tests/section_anchor_test.cpp)
    target_link_libraries(model_maker_section_anchor_test PRIVATE model_maker_core)
    target_link_options(model_maker_section_anchor_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_section_anchor_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_section_anchor_test COMMAND model_maker_section_anchor_test)

    # ASSEMBLY & PAPER SPACE (P5): Part/Assembly iliskileri, orto projeksiyon
    # (Camera standardViewRotation ile tek kaynak), Paper Space gorunus listesi.
    add_executable(model_maker_assembly_test tests/assembly_test.cpp)
    target_link_libraries(model_maker_assembly_test PRIVATE model_maker_core)
    target_link_options(model_maker_assembly_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_assembly_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_assembly_test COMMAND model_maker_assembly_test)

    # Weld + imalat Assembly (Phase W1): kaynak/assembly merge/split/undo/MMW8.
    add_executable(model_maker_weld_assembly_test tests/weld_assembly_test.cpp)
    target_link_libraries(model_maker_weld_assembly_test PRIVATE model_maker_core)
    target_link_options(model_maker_weld_assembly_test PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_weld_assembly_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_weld_assembly_test COMMAND model_maker_weld_assembly_test)
endif()

if(TARGET model-maker)
    # Gömülü Avrupa profil kataloğu exe'nin yanına kopyalanır
    add_custom_command(TARGET model-maker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/profiles
            $<TARGET_FILE_DIR:model-maker>/profiles
        COMMENT "Kopyalanıyor: profiles/ -> exe dizini")
endif()
