#pragma once

#if defined(_MSC_VER)
#define SAFETYHOOK_COMPILER_MSVC 1
#define SAFETYHOOK_COMPILER_GCC 0
#define SAFETYHOOK_COMPILER_CLANG 0
#elif defined(__GNUC__)
#define SAFETYHOOK_COMPILER_MSVC 0
#define SAFETYHOOK_COMPILER_GCC 1
#define SAFETYHOOK_COMPILER_CLANG 0
#elif defined(__clang__)
#define SAFETYHOOK_COMPILER_MSVC 0
#define SAFETYHOOK_COMPILER_GCC 0
#define SAFETYHOOK_COMPILER_CLANG 1
#else
#error "Unsupported compiler"
#endif

#if !defined(_M_IX86) && !defined(__i386__)
#error "Unsupported architecture"
#endif

#if !defined(_WIN32)
#error "Unsupported OS"
#endif

#if SAFETYHOOK_COMPILER_MSVC
#define SAFETYHOOK_CCALL __cdecl
#define SAFETYHOOK_STDCALL __stdcall
#define SAFETYHOOK_FASTCALL __fastcall
#define SAFETYHOOK_THISCALL __thiscall
#elif SAFETYHOOK_COMPILER_GCC || SAFETYHOOK_COMPILER_CLANG
#define SAFETYHOOK_CCALL __attribute__((cdecl))
#define SAFETYHOOK_STDCALL __attribute__((stdcall))
#define SAFETYHOOK_FASTCALL __attribute__((fastcall))
#define SAFETYHOOK_THISCALL __attribute__((thiscall))
#endif

#if SAFETYHOOK_COMPILER_MSVC
#define SAFETYHOOK_NOINLINE __declspec(noinline)
#elif SAFETYHOOK_COMPILER_GCC || SAFETYHOOK_COMPILER_CLANG
#define SAFETYHOOK_NOINLINE __attribute__((noinline))
#endif

#if SAFETYHOOK_COMPILER_MSVC
#define SAFETYHOOK_DLLEXPORT __declspec(dllexport)
#define SAFETYHOOK_DLLIMPORT __declspec(dllimport)
#elif SAFETYHOOK_COMPILER_GCC || SAFETYHOOK_COMPILER_CLANG
#define SAFETYHOOK_DLLEXPORT __attribute__((visibility("default")))
#define SAFETYHOOK_DLLIMPORT
#endif

#if SAFETYHOOK_SHARED_LIB && SAFETYHOOK_BUILDING
#define SAFETYHOOK_API SAFETYHOOK_DLLEXPORT
#elif SAFETYHOOK_SHARED_LIB
#define SAFETYHOOK_API SAFETYHOOK_DLLIMPORT
#else
#define SAFETYHOOK_API
#endif
