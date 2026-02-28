cmake_minimum_required(VERSION 3.22)
project(lodgen LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── third-party deps ──────────────────────────────────────────────────────────

set(BUILD_SHARED_LIBS          OFF CACHE BOOL "" FORCE)

set(ASSIMP_BUILD_TESTS         OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS  OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL             OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT           OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB          ON  CACHE BOOL "" FORCE)

set(MESHOPT_BUILD_DEMO         OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_GLTFPACK     OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_SHARED_LIBS  OFF CACHE BOOL "" FORCE)
set(MESHOPT_INSTALL            OFF CACHE BOOL "" FORCE)

add_subdirectory(deps/assimp-6.0.4)
add_subdirectory(deps/meshoptimizer-1.0)
add_subdirectory(deps/stb)        # target: stb       (static, owns stb_impl.c)
add_subdirectory(deps/argparser)  # target: cxxopts   (interface, header-only)

# ── lodgen library ────────────────────────────────────────────────────────────

file(GLOB_RECURSE LODGEN_SRCS lodgen/*.cpp)
file(GLOB_RECURSE LODGEN_HDRS lodgen/*.hpp)

add_library(lodgen STATIC ${LODGEN_SRCS} ${LODGEN_HDRS})
target_include_directories(lodgen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lodgen
    PUBLIC  assimp meshoptimizer
    PRIVATE stb)

install(TARGETS lodgen ARCHIVE DESTINATION lib)
install(DIRECTORY lodgen/
    DESTINATION include/lodgen
    FILES_MATCHING PATTERN "*.hpp")

# ── lodgencli executable ──────────────────────────────────────────────────────

option(LODGENCLI "Build lodgen CLI" ON)
if(LODGENCLI)
    add_executable(lodgencli lodgencli/main.cpp)
    target_link_libraries(lodgencli PRIVATE lodgen cxxopts)
    install(TARGETS lodgencli RUNTIME DESTINATION bin)
endif()
