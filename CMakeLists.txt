cmake_minimum_required(VERSION 3.22)
project(lodgen LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build assimp as a static library, disable unnecessary features
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)

# Build meshoptimizer as a static library, disable extras
set(MESHOPT_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_GLTFPACK OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(MESHOPT_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(deps/assimp-6.0.4)
add_subdirectory(deps/meshoptimizer-1.0)

file(GLOB_RECURSE SRCS lodgen/*.cpp)
file(GLOB_RECURSE HDRS lodgen/*.hpp)
add_library(lodgen STATIC ${SRCS} ${HDRS})
target_include_directories(lodgen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(lodgen PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/deps/stb)
target_link_libraries(lodgen PUBLIC assimp meshoptimizer)

install(TARGETS lodgen
    ARCHIVE DESTINATION lib)
install(DIRECTORY lodgen/
    DESTINATION include/lodgen
    FILES_MATCHING PATTERN "*.hpp"
    PATTERN "stb_impl.cpp" EXCLUDE)

option(LODGENCLI "Build lodgen cli" ON)
if (LODGENCLI)
    add_executable(lodgencli lodgencli/main.cpp)
    target_include_directories(lodgencli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/deps)
    target_link_libraries(lodgencli PRIVATE lodgen)
    install(TARGETS lodgencli
        RUNTIME DESTINATION bin)
endif()