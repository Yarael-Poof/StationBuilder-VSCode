# Libraries that are built optimised even in Debug builds.
#
# The simulation's matrix solver (nodalSolver.cpp) is all Eigen templates, which run 15-40x slower
# unoptimised: rebuilding a 40x40 all-cable network took ~200 ms instead of ~17 ms, and each tick ~7 ms
# instead of ~0.2 ms. It holds nothing but the matrix maths, so the rest of the game stays a normal,
# fully debuggable Debug build.
#
# CMake applies CMAKE_CXX_FLAGS_<CONFIG> per directory, which is why this lives in a folder of its own.
# /RTC1 has to go as well: MSVC can't combine runtime checks with /O2.

if(MSVC)
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/Ob0" "/Ob2" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/Od" "/O2" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

add_library(nodalSolver STATIC ${SRC_DIR}/nodalSolver.cpp)
target_include_directories(nodalSolver PUBLIC ${SRC_DIR})
target_link_libraries(nodalSolver PRIVATE eigen_headers)
if(MSVC)
    target_compile_options(nodalSolver PRIVATE /W3 /utf-8)
endif()
