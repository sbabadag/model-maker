cmake_minimum_required(VERSION 3.20)

# ============================================================================
# mm_component_contract — BAGIMSIZ derleme (Studio sparse-checkout icin).
#
# Bu CMakeLists.txt libs/mm_contract/ altindadir; KAYNAKLAR repo kokunde
# src/ ve include/model_maker/ icinde kalir (sifir kaynak degisikligi).
# Qt ve OCCT YOKTUR; yalniz C++20 + std gerekir.
#
# Studio bunu iki zincirde derler:
#   Linux GCC  : cmake -S libs/mm_contract -B build-contract && cmake --build build-contract && ctest --test-dir build-contract
#   Win UCRT64 : cmake -S libs/mm_contract -B build-contract -G Ninja && cmake --build build-contract && ctest --test-dir build-contract
# ============================================================================
project(mm_component_contract VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Repo koku (bu dosyanin iki ustu: libs/mm_contract -> libs -> kok).
get_filename_component(MM_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# --- mm_foundation: yaprak saf tipler ---
add_library(mm_foundation
    ${MM_ROOT}/src/geometry.cpp
    ${MM_ROOT}/src/reference_geometry.cpp
    ${MM_ROOT}/src/contact_resolver.cpp
    ${MM_ROOT}/src/sha256.cpp
)
target_include_directories(mm_foundation PUBLIC ${MM_ROOT}/include ${MM_ROOT}/third_party)

# --- mm_component_contract: component veri modeli + generator'lar ---
add_library(mm_component_contract
    ${MM_ROOT}/src/cut_modifier.cpp
    ${MM_ROOT}/src/bolt.cpp
    ${MM_ROOT}/src/constraint_system.cpp
    ${MM_ROOT}/src/magnetic_frame.cpp
    ${MM_ROOT}/src/end_plate.cpp
    ${MM_ROOT}/src/component_engine.cpp
    ${MM_ROOT}/src/component_dialog.cpp
    ${MM_ROOT}/src/component_dialog_146_canvas.cpp
    ${MM_ROOT}/src/component_framework.cpp
    ${MM_ROOT}/src/component_catalog.cpp
    ${MM_ROOT}/src/component_codec.cpp
    ${MM_ROOT}/src/component_package.cpp
    ${MM_ROOT}/src/component_preview.cpp
    ${MM_ROOT}/src/component_checks.cpp
    ${MM_ROOT}/src/component_preview_auto.cpp
    ${MM_ROOT}/src/recipe.cpp
)
target_include_directories(mm_component_contract PUBLIC ${MM_ROOT}/include ${MM_ROOT}/third_party ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(mm_component_contract PUBLIC mm_foundation)

# --- contractVersion basligi (bu alt klasorde, self-contained) ---
add_library(mm_contract_version INTERFACE)
target_include_directories(mm_contract_version INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(mm_contract_version INTERFACE mm_component_contract)

# --- Bagimsiz testler ---
option(MM_CONTRACT_BUILD_TESTS "Build contract standalone tests" ON)
if(MM_CONTRACT_BUILD_TESTS)
    enable_testing()
    add_executable(mm_contract_smoke_test ${CMAKE_CURRENT_LIST_DIR}/tests/contract_smoke_test.cpp)
    target_link_libraries(mm_contract_smoke_test PRIVATE mm_component_contract mm_contract_version)
    add_test(NAME mm_contract_smoke_test COMMAND mm_contract_smoke_test)

    add_executable(mm_contract_codec_test ${CMAKE_CURRENT_LIST_DIR}/tests/component_codec_test.cpp)
    target_link_libraries(mm_contract_codec_test PRIVATE mm_component_contract mm_contract_version)
    target_compile_definitions(mm_contract_codec_test PRIVATE
        MM_CONTRACT_GOLDEN_DIR="${MM_ROOT}/tests/golden")
    add_test(NAME mm_contract_codec_test COMMAND mm_contract_codec_test)

    add_executable(mm_contract_recipe_test ${CMAKE_CURRENT_LIST_DIR}/tests/recipe_test.cpp)
    target_link_libraries(mm_contract_recipe_test PRIVATE mm_component_contract mm_contract_version)
    add_test(NAME mm_contract_recipe_test COMMAND mm_contract_recipe_test)

    add_executable(mm_contract_end_plate_equiv_test ${CMAKE_CURRENT_LIST_DIR}/tests/end_plate_equivalence_test.cpp)
    target_link_libraries(mm_contract_end_plate_equiv_test PRIVATE mm_component_contract mm_contract_version)
    target_compile_definitions(mm_contract_end_plate_equiv_test PRIVATE
        MM_CONTRACT_GOLDEN_DIR="${MM_ROOT}/tests/golden")
    add_test(NAME mm_contract_end_plate_equiv_test COMMAND mm_contract_end_plate_equiv_test)

    add_executable(mm_contract_haunch40_equiv_test ${CMAKE_CURRENT_LIST_DIR}/tests/haunch40_equivalence_test.cpp)
    target_link_libraries(mm_contract_haunch40_equiv_test PRIVATE mm_component_contract mm_contract_version)
    target_compile_definitions(mm_contract_haunch40_equiv_test PRIVATE
        MM_CONTRACT_GOLDEN_DIR="${MM_ROOT}/tests/golden")
    add_test(NAME mm_contract_haunch40_equiv_test COMMAND mm_contract_haunch40_equiv_test)

    # MM-5 fix (item 1): PAKET recipe.json'undan parametre akisi. Golden digest'ler
    # DEGISMEDEN, >=5 parametre degistirildiginde recipe == generateHaunch40.
    add_executable(mm_contract_haunch40_param_flow_test
        ${CMAKE_CURRENT_LIST_DIR}/tests/haunch40_param_flow_test.cpp)
    target_link_libraries(mm_contract_haunch40_param_flow_test PRIVATE mm_component_contract mm_contract_version)
    target_compile_definitions(mm_contract_haunch40_param_flow_test PRIVATE
        MM_CONTRACT_GOLDEN_DIR="${MM_ROOT}/tests/golden"
        MM_PACKAGES_DIR="${CMAKE_CURRENT_LIST_DIR}/packages")
    add_test(NAME mm_contract_haunch40_param_flow_test COMMAND mm_contract_haunch40_param_flow_test)

    # MM-5 — paket yukleyici testi: yerlesik typeId cakismasi, mm_end_plate_pkg
    # olarak yukleme, bozuk paket reddi, proje klasoru golgelemesi.
    add_executable(mm_contract_package_test
        ${CMAKE_CURRENT_LIST_DIR}/tests/component_package_test.cpp)
    target_link_libraries(mm_contract_package_test PRIVATE mm_component_contract mm_contract_version)
    target_compile_definitions(mm_contract_package_test PRIVATE
        MM_PACKAGES_DIR="${CMAKE_CURRENT_LIST_DIR}/packages")
    add_test(NAME mm_contract_package_test COMMAND mm_contract_package_test)

    # MM-6 — veriyle tanimlanan onizleme: anchor cozucu + olcu hesabi.
    add_executable(mm_contract_component_preview_test
        ${CMAKE_CURRENT_LIST_DIR}/tests/component_preview_test.cpp)
    target_link_libraries(mm_contract_component_preview_test PRIVATE mm_component_contract mm_contract_version)
    add_test(NAME mm_contract_component_preview_test COMMAND mm_contract_component_preview_test)

    # MM-7 — bulon/uye cakisma + EC3 min kenar/aralik kontrolu.
    add_executable(mm_contract_component_checks_test
        ${CMAKE_CURRENT_LIST_DIR}/tests/component_checks_test.cpp)
    target_link_libraries(mm_contract_component_checks_test PRIVATE mm_component_contract mm_contract_version)
    add_test(NAME mm_contract_component_checks_test COMMAND mm_contract_component_checks_test)

    # MM-6b — otomatik onizleme tanimi (duyarlilik analizi).
    add_executable(mm_contract_component_preview_auto_test
        ${CMAKE_CURRENT_LIST_DIR}/tests/component_preview_auto_test.cpp)
    target_link_libraries(mm_contract_component_preview_auto_test PRIVATE mm_component_contract mm_contract_version)
    add_test(NAME mm_contract_component_preview_auto_test COMMAND mm_contract_component_preview_auto_test)

    # Levha gorunum kurali (gri sabit, tek kaynak) — MM-7/ST-3 notu.
    add_executable(mm_contract_component_visual_test
        ${CMAKE_CURRENT_LIST_DIR}/tests/component_visual_test.cpp)
    target_link_libraries(mm_contract_component_visual_test PRIVATE mm_component_contract mm_contract_version)
    add_test(NAME mm_contract_component_visual_test COMMAND mm_contract_component_visual_test)

    # Paket uretici (parameters/dialog/manifest) — test degil, arac.
    add_executable(mm_gen_end_plate_package ${CMAKE_CURRENT_LIST_DIR}/tools/gen_end_plate_package.cpp)
    target_link_libraries(mm_gen_end_plate_package PRIVATE mm_component_contract mm_contract_version)

    # Uretim hedefi: parameters/dialog/manifest'i mevcut tanimlardan yeniden uretir.
    add_custom_target(mm_generate_end_plate_package_pkg
        COMMAND mm_gen_end_plate_package ${CMAKE_CURRENT_LIST_DIR}/packages/end_plate
        DEPENDS mm_gen_end_plate_package
        COMMENT "end plate paketi (parameters/dialog/manifest) uretiliyor"
        VERBATIM)

    # Haunch 40 paket uretici (MM-3c 1-C) — ayni desen.
    add_executable(mm_gen_haunch40_package ${CMAKE_CURRENT_LIST_DIR}/tools/gen_haunch40_package.cpp)
    target_link_libraries(mm_gen_haunch40_package PRIVATE mm_component_contract mm_contract_version)

    add_custom_target(mm_generate_haunch40_package_pkg
        COMMAND mm_gen_haunch40_package ${CMAKE_CURRENT_LIST_DIR}/packages/haunch40
        DEPENDS mm_gen_haunch40_package
        COMMENT "haunch40 paketi (parameters/dialog/manifest) uretiliyor"
        VERBATIM)
endif()
