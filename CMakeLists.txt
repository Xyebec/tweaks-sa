cmake_minimum_required(VERSION 4.1)

project(TweaksSA
  LANGUAGES C CXX
)

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported)

if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
else()
    message(STATUS "LTO is not supported")
endif()

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
file(GLOB_RECURSE SOURCES ${SRC_DIR}/*.cpp)
file(GLOB_RECURSE HEADERS ${SRC_DIR}/*.h)

add_library(tweaks_sa SHARED ${SOURCES} ${HEADERS})

target_compile_features(tweaks_sa PRIVATE cxx_std_23)

set_target_properties(tweaks_sa PROPERTIES
  PREFIX ""
  SUFFIX ".asi"
  OUTPUT_NAME ${PROJECT_NAME}
)

if(NOT MSVC)
    target_link_options(tweaks_sa PRIVATE "LINKER:--exclude-all-symbols")
endif()

target_include_directories(tweaks_sa
  PRIVATE
    ${SRC_DIR}
    lib/
)

configure_file(assets/config.toml ${PROJECT_NAME}.toml COPYONLY)

if(NOT MSVC AND CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_command(
        TARGET tweaks_sa POST_BUILD
        COMMAND ${CMAKE_STRIP} $<TARGET_FILE:tweaks_sa>
        COMMENT "Stripping release binary"
    )
endif()

add_subdirectory(lib/safetyhook)
target_link_libraries(tweaks_sa PRIVATE safetyhook)

include(FetchContent)

FetchContent_Declare(
  pluginsdk
  GIT_REPOSITORY https://github.com/Xyebec/plugin-sdk.git
  GIT_TAG        8449ce821f4de5159aa02ae0a2eda9bebb1aac13
)
FetchContent_MakeAvailable(pluginsdk)
target_link_libraries(tweaks_sa PRIVATE pluginsdk::gtasa)
