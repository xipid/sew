cmake_minimum_required(VERSION 3.14)

# Force Clang if not specified
if(NOT CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER "clang")
endif()
if(NOT CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER "clang++")
endif()

project(Sew VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable CCache
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "ccache enabled")
endif()

# Enable Mold Linker
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=mold")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=mold")
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -fuse-ld=mold")
message(STATUS "mold linker enabled")

# Include the local 'xic' library from the sibling directory
get_filename_component(XIC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../xic" ABSOLUTE)
if(NOT EXISTS "${XIC_DIR}/CMakeLists.txt")
    message(STATUS "xic not found at ${XIC_DIR}, downloading automatically...")
    execute_process(
        COMMAND git clone https://github.com/xipid/xic.git "${XIC_DIR}"
        RESULT_VARIABLE CLONE_RESULT
    )
    if(NOT CLONE_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to automatically download xic from https://github.com/xipid/xic.git")
    endif()
endif()

if(EXISTS "${XIC_DIR}/CMakeLists.txt")
    message(STATUS "Found xic at ${XIC_DIR}")
    add_subdirectory(${XIC_DIR} ${CMAKE_CURRENT_BINARY_DIR}/xic)
else()
    message(FATAL_ERROR "xic not found at ${XIC_DIR}")
endif()

# --- Automatic Source Discovery ---
file(GLOB_RECURSE SEW_SOURCES 
    "src/Sew/*.cpp"
    "src/Languages/*.cpp"
)
list(APPEND SEW_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Parser.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Generator.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Reflection/Reflection.cpp"
)

# --- QuickJS via FetchContent ---
include(FetchContent)
set(QUICKJS_LOCAL_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/_deps/quickjs_src-src")
if(EXISTS "${QUICKJS_LOCAL_SOURCE_DIR}/quickjs.c")
    message(STATUS "Using local QuickJS source at ${QUICKJS_LOCAL_SOURCE_DIR}")
    FetchContent_Declare(
        quickjs_src
        SOURCE_DIR "${QUICKJS_LOCAL_SOURCE_DIR}"
    )
else()
    FetchContent_Declare(
        quickjs_src
        GIT_REPOSITORY https://github.com/bellard/quickjs.git
        GIT_TAG master
    )
endif()
FetchContent_GetProperties(quickjs_src)
if(NOT quickjs_src_POPULATED)
    FetchContent_Populate(quickjs_src)
endif()

add_library(quickjs STATIC
    "${quickjs_src_SOURCE_DIR}/quickjs.c"
    "${quickjs_src_SOURCE_DIR}/libregexp.c"
    "${quickjs_src_SOURCE_DIR}/libunicode.c"
    "${quickjs_src_SOURCE_DIR}/cutils.c"
    "${quickjs_src_SOURCE_DIR}/quickjs-libc.c"
    "${quickjs_src_SOURCE_DIR}/dtoa.c"
)
target_compile_definitions(quickjs PRIVATE
    _GNU_SOURCE
    CONFIG_VERSION="2026-06-04"
)
target_include_directories(quickjs PUBLIC
    "${quickjs_src_SOURCE_DIR}"
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(quickjs PRIVATE
        -Wno-sign-compare
        -Wno-missing-field-initializers
        -Wno-unused-parameter
        -Wno-char-subscripts
    )
endif()

# --- Main Target Definition ---
add_library(SewLib STATIC ${SEW_SOURCES})

# Basic Include Directories
target_include_directories(SewLib PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    $<BUILD_INTERFACE:${quickjs_src_SOURCE_DIR}>
)

# Link against Xi and quickjs
target_link_libraries(SewLib PUBLIC Xi::Xi quickjs)


# --- Test Discovery ---
file(GLOB TEST_SOURCES "tests/*.cpp")
foreach(TEST_SRC ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)
    add_executable(${TEST_NAME} ${TEST_SRC})
    target_link_libraries(${TEST_NAME} PRIVATE SewLib)
    set_target_properties(${TEST_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests")
endforeach()

# --- Hot-reloading test (separate: lives in tests/hotreloading/, needs dl) ---
add_executable(test_hotreloading tests/hotreloading/main.cpp)
target_link_libraries(test_hotreloading PRIVATE SewLib ${CMAKE_DL_LIBS})
# -export-dynamic: symbols from this binary are visible to dlopen()'d .so files,
# so the modules can resolve SewLib functions without being linked against SewLib.
target_link_options(test_hotreloading PRIVATE -Wl,-export-dynamic)
set_target_properties(test_hotreloading PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests")

# --- CLI Tool ---
add_executable(sew src/main.cpp)
target_link_libraries(sew PRIVATE -Wl,--whole-archive SewLib -Wl,--no-whole-archive)
set_target_properties(sew PROPERTIES LINK_FLAGS "-Wl,-export-dynamic")
set_target_properties(sew PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

if(EXISTS "/home/xi/.gemini/antigravity/brain/8f46d962-8582-4335-9d80-910ea9fb462b/scratch/debug_parser.cpp")
    add_executable(debug_parser /home/xi/.gemini/antigravity/brain/8f46d962-8582-4335-9d80-910ea9fb462b/scratch/debug_parser.cpp)
    target_link_libraries(debug_parser PRIVATE SewLib)
    set_target_properties(debug_parser PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endif()
