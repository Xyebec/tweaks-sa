cmake_minimum_required(VERSION 3.15)

option(SAFETYHOOK_USE_CXXMODULES "" OFF)
option(SAFETYHOOK_DISABLE_X86_SSE "" ON)

project(safetyhook)

set(safetyhook_SOURCES
	"src/allocator.cpp"
	"src/easy.cpp"
	"src/inline_hook.cpp"
	"src/mid_hook.cpp"
	"src/os.windows.cpp"
	"src/utility.cpp"
	"src/vmt_hook.cpp"

    "src/Zydis.c"
)

add_library(safetyhook)

target_sources(safetyhook PRIVATE ${safetyhook_SOURCES})

if(BUILD_SHARED_LIBS) # shared
	# target_compile_definitions(safetyhook PUBLIC
	# 	SAFETYHOOK_SHARED_LIB
	# )

	# target_compile_definitions(safetyhook PRIVATE
	# 	SAFETYHOOK_BUILDING
	# )

    target_compile_definitions(safetyhook
      PUBLIC
        SAFETYHOOK_SHARED_LIB
      PRIVATE
        SAFETYHOOK_BUILDING
    )
endif()

target_compile_features(safetyhook PUBLIC
	cxx_std_23
)

if(MSVC) # msvc
	target_compile_options(safetyhook PRIVATE
		"/permissive-"
		"/W4"
		"/w14640"
		"/wd4251"
	)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_C_COMPILER_ID STREQUAL "Clang") # gcc/clang
	target_compile_options(safetyhook PRIVATE
		-Wall
		-Wextra
		-Wshadow
		# -Wnon-virtual-dtor
		-pedantic
	)
endif()


target_include_directories(safetyhook PUBLIC
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
	# "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

if(SAFETYHOOK_USE_CXXMODULES)
    target_compile_definitions(safetyhook INTERFACE SAFETYHOOK_USE_CXXMODULES)
endif()

if(SAFETYHOOK_DISABLE_X86_SSE)
    target_compile_definitions(safetyhook INTERFACE SAFETYHOOK_DISABLE_X86_SSE)
endif()

# include(GNUInstallDirs)

# install(
#     TARGETS safetyhook
#     EXPORT safetyhook-targets
#     LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
#     ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
#     RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
#     INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
# )

# install(
#     DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
#     DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
# )

# install(
#     EXPORT safetyhook-targets
#     FILE safetyhook-targets.cmake
#     NAMESPACE safetyhook::
#     DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/safetyhook
# )

# install(
#     FILES ${CMAKE_CURRENT_BINARY_DIR}/safetyhook-config.cmake
#     DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/safetyhook
# )
