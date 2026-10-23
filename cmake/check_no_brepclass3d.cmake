# Uygulama kaynaginda BRepClass3d_SolidClassifier KULLANILMAMALI.
#
# MSYS2 opencascade 7.9.3-3 (GCC 16 -O3) BRepClass3d_SClassifier'i yanlis
# derliyor (OCCT #1637, GCC PR 127917): Windows'ta Extrema_ExtCC::Perform SIGSEGV.
# Linux OCCT saglam oldugu icin bu cokme Linux testlerinde HIC gorunmez; bu
# kontrol regresyonu Linux'ta yakalar. Yerine pointSolidState / boundaryFacesOf
# (occ_geometry.hpp) kullanin. Testlerde oracle olarak kullanmak serbesttir.
#
# Kullanim: cmake -DSRC_DIR=<repo>/src -P check_no_brepclass3d.cmake
file(GLOB_RECURSE _files "${SRC_DIR}/*.cpp" "${SRC_DIR}/*.hpp" "${SRC_DIR}/*.h")
set(_bad "")
foreach(_f IN LISTS _files)
    file(STRINGS "${_f}" _hits REGEX "^[^/]*BRepClass3d_SolidClassifier")
    if(_hits)
        list(APPEND _bad "${_f}")
    endif()
endforeach()
if(_bad)
    message(FATAL_ERROR "BRepClass3d_SolidClassifier yasak (Windows GCC16 OCCT cokmesi): ${_bad}")
endif()
# Kaynak dikis katilari Fuse ile birlestirilmez (Fuse da ayni siniflandiriciya
# girer: BOPAlgo_BuilderSolid -> ComputeState); compoundOfShapes kullanilir.
foreach(_f IN LISTS _files)
    file(STRINGS "${_f}" _hits REGEX "booleanFuseShape\\(fused, *solids")
    if(_hits)
        message(FATAL_ERROR "Kaynak katilari Fuse ile birlestiriliyor (compoundOfShapes kullan): ${_f}")
    endif()
endforeach()
message(STATUS "OK: src altinda BRepClass3d_SolidClassifier yok")
