cmake_minimum_required(VERSION 4.1)
project(TweaksSA LANGUAGES CXX)

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# include(CheckIPOSupported)
# check_ipo_supported(RESULT ipo_supported)
#
# if(ipo_supported)
#     set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
# else()
#     message(STATUS "LTO is not supported")
# endif()

# Dependencies

add_library(deps::boost::pfr INTERFACE IMPORTED)
target_include_directories(deps::boost::pfr INTERFACE "lib")

add_library(deps::toml INTERFACE IMPORTED)
target_include_directories(deps::toml INTERFACE "lib")
target_compile_definitions(deps::toml INTERFACE TOML_EXCEPTIONS=0 TOML_HEADER_ONLY=0)

add_subdirectory("lib/safetyhook")

include(FetchContent)
FetchContent_Declare(
  pluginsdk
  GIT_REPOSITORY "https://github.com/Xyebec/plugin-sdk.git"
  GIT_TAG        473cde9a17146f988528b7cb20588ea4a500e976
)
FetchContent_MakeAvailable(pluginsdk)

# Project

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/*.hpp")
add_library(TweaksSA SHARED ${SOURCES})
target_compile_features(TweaksSA PRIVATE cxx_std_26)

target_include_directories(TweaksSA PRIVATE "src")

set_target_properties(TweaksSA PROPERTIES
  PREFIX ""
  SUFFIX ".asi"
  OUTPUT_NAME ${PROJECT_NAME}
)

if(MSVC)
    set_target_properties(TweaksSA PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
else()
    target_link_options(TweaksSA PRIVATE
        "LINKER:--exclude-all-symbols"
        "LINKER:-static"
        $<$<CONFIG:Release>:LINKER:-s>
    )
endif()

configure_file(assets/config.toml ${PROJECT_NAME}.toml COPYONLY)

target_link_libraries(TweaksSA PRIVATE
    deps::boost::pfr
    deps::toml
    safetyhook 
    pluginsdk::gtasa
    d3dx9
)
