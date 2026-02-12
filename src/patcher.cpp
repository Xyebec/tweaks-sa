#include "patcher.h"

#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
  #define NOMINMAX
#endif

#include <windows.h>
#include <cassert>

template <typename... Args>
static constexpr void write_sequence(patch::Address dst, const Args&... args, bool unprotect = true) {
    constexpr size_t LENGTH = (sizeof(Args) + ... + 0);
    const auto up = patch::ScopedUnprotect{dst, LENGTH, unprotect};

    ([&] {
        *dst.as_ptr<Args>() = args;
        dst += sizeof(args);
    }(), ...);
}

template <typename T>
static constexpr auto calc_offset(patch::Address from, patch::Address to, size_t instruction_len) -> T {
    const auto offset = (to - from - instruction_len).as<ptrdiff_t>();
    using Limits = std::numeric_limits<T>;
    assert(offset >= Limits::min() && offset <= Limits::max());
    return static_cast<T>(offset);
}

namespace patch {
    static_assert(sizeof(MemoryProtection) == sizeof(DWORD));

    auto unprotect(Address dst, size_t size) -> MemoryProtection {
        DWORD old_protect{};
        VirtualProtect(dst.as_ptr(), size, PAGE_EXECUTE_READWRITE, &old_protect);
        return old_protect;
    }

    ScopedUnprotect::ScopedUnprotect(Address address, size_t size, bool unprotect) noexcept
        : address{address}
        , size{size}
        , old_protect{}
        , unprotect{unprotect}
    {
        if (unprotect) {
            DWORD old_protect{};
            VirtualProtect(address.as_ptr(), size, PAGE_EXECUTE_READWRITE, &old_protect);
            this->old_protect = old_protect;
        }
    }

    ScopedUnprotect::~ScopedUnprotect() {
        if (unprotect) {
            DWORD unused_old_protect{};
            VirtualProtect(address.as_ptr(), size, old_protect, &unused_old_protect);
        }
    }

    auto get_vmt(const void* self) -> void** {
        return *reinterpret_cast<void* *const *>(self);
    }

    auto get_virtual_method(const void* self, size_t index) -> void* {
        return get_vmt(self)[index];
    }
    
    void set_virtual_method(const void* self, void* func, size_t index) {
        auto** vmt = get_vmt(self);
        const auto* method = vmt[index];
        const auto up = ScopedUnprotect{method, sizeof(method)};
        vmt[index] = func;
    }

    auto find_function(const char* module_name, const char* function_name) -> void* {
        auto *const module = GetModuleHandleA(module_name);
        return module != nullptr ? reinterpret_cast<void*>(GetProcAddress(module, function_name)) : nullptr;
    }

    void copy(Address dst, Address src, size_t size, bool unprotect) {
        const auto up = ScopedUnprotect{dst, size, unprotect};
        std::memcpy(dst.as_ptr(), src.as_ptr(), size);
    }
    
    void copy_slice(Address dst, std::span<const uint8_t> src, bool unprotect) {
        copy(dst, src.data(), src.size(), unprotect);
    }

    void copy_slice(Address dst, std::initializer_list<uint8_t> src, bool unprotect) {
        copy(dst, src.begin(), src.size(), unprotect);
    }

    void fill(Address dst, uint8_t value, size_t size, bool unprotect) {
        const auto up = ScopedUnprotect{dst, size, unprotect};
        std::memset(dst.as_ptr(), value, size);
    }

    void nop(Address at, size_t size, bool unprotect) {
        fill(at, 0x90, size, unprotect);
    }
    
    void call(Address from, Address to, bool unprotect) {
        const auto offset = calc_offset<int32_t>(from, to, 5);
        write_sequence<uint8_t, int32_t>(from, 0xE8, offset, unprotect);
    }
    
    void jmp(Address from, Address to, bool unprotect) {
        const auto offset = calc_offset<int32_t>(from, to, 5);
        write_sequence<uint8_t, int32_t>(from, 0xE9, offset, unprotect);
    }
    
    void jmp_short(Address from, Address to, bool unprotect) {
        const auto offset = calc_offset<int8_t>(from, to, 2);
        write_sequence<uint8_t, int8_t>(from, 0xEB, offset, unprotect);
    }

    void ret(Address at, uint16_t bytes_to_pop, bool unprotect) {
        if (bytes_to_pop != 0) {
            write_sequence<uint8_t, uint16_t>(at, 0xC2, bytes_to_pop, unprotect);
        } else {
            set<uint8_t>(at, 0xC3, unprotect);
        }
    }
}
