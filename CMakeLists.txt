cmake_minimum_required(VERSION 4.1)

project(TweaksSA
  LANGUAGES C CXX
)

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# include(CheckIPOSupported)
# check_ipo_supported(RESULT ipo_supported)
#
# if(ipo_supported)
#     set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
# else()
#     message(STATUS "LTO is not supported")
# endif()

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
file(GLOB_RECURSE SOURCES ${SRC_DIR}/*.cpp)
file(GLOB_RECURSE HEADERS ${SRC_DIR}/*.h)

add_library(tweaks_sa SHARED ${SOURCES} ${HEADERS})

target_compile_features(tweaks_sa PRIVATE cxx_std_26)

set_target_properties(tweaks_sa PROPERTIES
  PREFIX ""
  SUFFIX ".asi"
  OUTPUT_NAME ${PROJECT_NAME}
)

if(NOT MSVC)
    target_link_options(tweaks_sa
      PRIVATE
        "LINKER:--exclude-all-symbols"
        "-static"
        $<$<CONFIG:Release>:-s>
    )
endif()

target_include_directories(tweaks_sa
  PRIVATE
    ${SRC_DIR}
    lib/
)

configure_file(assets/config.toml ${PROJECT_NAME}.toml COPYONLY)

add_subdirectory(lib/safetyhook)
target_link_libraries(tweaks_sa PRIVATE safetyhook)

include(FetchContent)

FetchContent_Declare(
  pluginsdk
  GIT_REPOSITORY https://github.com/Xyebec/plugin-sdk.git
  GIT_TAG        473cde9a17146f988528b7cb20588ea4a500e976
)
FetchContent_MakeAvailable(pluginsdk)
target_link_libraries(tweaks_sa PRIVATE pluginsdk::gtasa)

target_link_libraries(tweaks_sa PRIVATE d3dx9)
