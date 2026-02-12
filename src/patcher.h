#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

#if defined(_MSC_VER)
  #define CALLCONV_CCALL __cdecl
  #define CALLCONV_STDCALL __stdcall
  #define CALLCONV_FASTCALL __fastcall
  #define CALLCONV_THISCALL __thiscall
#elif defined(__GNUC__) || defined(__clang__)
  #define CALLCONV_CCALL __attribute__((cdecl))
  #define CALLCONV_STDCALL __attribute__((stdcall))
  #define CALLCONV_FASTCALL __attribute__((fastcall))
  #define CALLCONV_THISCALL __attribute__((thiscall))
#else
  #error "Unsupported compiler"
#endif

namespace patch {
    class Address final {
    public:
        template <typename Fn>
        requires std::is_invocable_v<Fn> && std::is_class_v<Fn>
        Address(Fn& lambda) noexcept
            : value{reinterpret_cast<uintptr_t>(+lambda)} {}

        template <typename T>
        Address(T* ptr) noexcept
            : value{reinterpret_cast<uintptr_t>(ptr)} {}

        constexpr Address(uintptr_t address) noexcept
            : value{address} {}

        constexpr Address(std::nullptr_t) noexcept
            : value{} {}
        
        constexpr Address() noexcept
            : value{} {}

        template <typename T = uint8_t>
        auto as_ptr() const noexcept -> T* {
            return reinterpret_cast<T*>(value);
        }

        constexpr auto as_int() const noexcept -> uintptr_t {
            return value;
        }

        template <typename T>
        requires std::is_integral_v<T>
        constexpr auto as() const noexcept -> T {
            return static_cast<T>(value);
        }

        constexpr explicit operator bool() const noexcept { 
            return value != 0;
        }

        constexpr auto operator+(const Address other) const noexcept -> Address {
            return value + other.value;
        }

        constexpr auto operator+(const uintptr_t other) const noexcept -> Address {
            return value + other;
        }

        constexpr auto operator-(const Address other) const noexcept -> Address {
            return value - other.value;
        }
        
        constexpr auto operator-(const uintptr_t other) const noexcept -> Address {
            return value - other;
        }

        constexpr auto operator+=(const Address other) noexcept -> Address& {
            value += other.value;
            return *this;
        }

        constexpr auto operator-=(const Address other) noexcept -> Address& {
            value -= other.value;
            return *this;
        }

    private:
        uintptr_t value;
    };

    using MemoryProtection = uint32_t;
    
    auto unprotect(Address dst, size_t size) -> MemoryProtection;

    class ScopedUnprotect final {
    public:
        ScopedUnprotect(Address address, size_t size, bool unprotect = true) noexcept;
        ~ScopedUnprotect();

        ScopedUnprotect(const ScopedUnprotect& other) = delete;
        auto operator=(const ScopedUnprotect& other) -> ScopedUnprotect& = delete;

        ScopedUnprotect(ScopedUnprotect&& other) noexcept = delete;
        auto operator=(ScopedUnprotect&& other) noexcept -> ScopedUnprotect& = delete;

    private:
        Address address;
        size_t size;
        MemoryProtection old_protect;
        bool unprotect;
    };
    
    /// Gets the virtual method table (array of function pointers) from the object @self
    auto get_vmt(const void* self) -> void**;

    /// Gets a virtual method from the table @self at the index @index
    auto get_virtual_method(const void* self, size_t index) -> void*;

    void set_virtual_method(const void* self, void* func, size_t index);
    
    template <uintptr_t address, typename Ret, typename... Args>
    inline auto call_static(Args... args) -> Ret {
        return reinterpret_cast<Ret(CALLCONV_CCALL*)(Args...)>(address)(args...);
    }

    template <uintptr_t address, typename Ret, typename... Args>
    inline auto call_std(Args... args) -> Ret {
        return reinterpret_cast<Ret(CALLCONV_STDCALL*)(Args...)>(address)(args...);
    }

    template <uintptr_t address, typename Ret, class Class, typename... Args>
    inline auto call_method(Class* _this, Args... args) -> Ret {
        return reinterpret_cast<Ret(CALLCONV_THISCALL*)(Class*, Args...)>(address)(_this, args...);
    }

    template <uintptr_t address, size_t table_index, typename Ret, class Class, typename... Args>
    inline auto call_virtual_method(Class* _this, Args... args) -> Ret {
        return reinterpret_cast<Ret(CALLCONV_THISCALL*)(Class*, Args...)>(get_virtual_method(_this, table_index))(_this, args...);
    }
    
    auto find_function(const char* module_name, const char* function_name) -> void*;

    void copy(Address dst, Address src, size_t size, bool unprotect = true);

    void copy_slice(Address dst, std::span<const uint8_t> src, bool unprotect = true);

    void copy_slice(Address dst, std::initializer_list<uint8_t> src, bool unprotect = true);
    
    template <typename T>
    inline void set(Address dst, const T& value, bool unprotect = true) {
        const auto up = ScopedUnprotect{dst, sizeof(T), unprotect};
        *dst.as_ptr<T>() = value;
    }

    void fill(Address dst, uint8_t value, size_t size, bool unprotect = true);

    void nop(Address at, size_t size, bool unprotect = true);
    
    void call(Address from, Address to, bool unprotect = true);
    
    void jmp(Address from, Address to, bool unprotect = true);
    
    void jmp_short(Address from, Address to, bool unprotect = true);
    
    void ret(Address at, uint16_t bytes_to_pop = 0, bool unprotect = true);
}
