/// @file safetyhook/vmt_hook.hpp
/// @brief VMT hooking classes

#pragma once

#ifndef SAFETYHOOK_USE_CXXMODULES
#include <cstdint>
#include <expected>
#include <unordered_map>
#else
import std.compat;
#endif

#include "./allocator.hpp"
#include "./common.hpp"
#include "./utility.hpp"

namespace safetyhook {

/// @brief A hook class that allows for hooking a single method in a VMT.
class SAFETYHOOK_API VmHook final {
public:
    VmHook() = default;
    VmHook(const VmHook&) = delete;
    VmHook(VmHook&& other) noexcept;
    auto operator=(const VmHook&) -> VmHook& = delete;
    auto operator=(VmHook&& other) noexcept -> VmHook&;
    ~VmHook();

    /// @brief Removes the hook.
    void reset();

    /// @brief Gets the original method pointer.
    template <typename T>
    [[nodiscard]] auto original() const -> T { return reinterpret_cast<T>(m_original_vm); }

    /// @brief Calls the original method.
    /// @tparam RetT The return type of the method.
    /// @tparam Args The argument types of the method.
    /// @param args The arguments to pass to the method.
    /// @return The return value of the method.
    /// @note This will call the original method with the default calling convention.
    template <typename RetT = void, typename... Args>
    auto call(Args... args) -> RetT {
        return original<RetT (*)(Args...)>()(args...);
    }

    /// @brief Calls the original method with the __cdecl calling convention.
    /// @tparam RetT The return type of the method.
    /// @tparam Args The argument types of the method.
    /// @param args The arguments to pass to the method.
    /// @return The return value of the method.
    template <typename RetT = void, typename... Args>
    auto ccall(Args... args) -> RetT {
        return original<RetT(SAFETYHOOK_CCALL*)(Args...)>()(args...);
    }

    /// @brief Calls the original method with the __thiscall calling convention.
    /// @tparam RetT The return type of the method.
    /// @tparam Args The argument types of the method.
    /// @param args The arguments to pass to the method.
    /// @return The return value of the method.
    template <typename RetT = void, typename... Args>
    auto thiscall(Args... args) -> RetT {
        return original<RetT(SAFETYHOOK_THISCALL*)(Args...)>()(args...);
    }

    /// @brief Calls the original method with the __stdcall calling convention.
    /// @tparam RetT The return type of the method.
    /// @tparam Args The argument types of the method.
    /// @param args The arguments to pass to the method.
    /// @return The return value of the method.
    template <typename RetT = void, typename... Args>
    auto stdcall(Args... args) -> RetT {
        return original<RetT(SAFETYHOOK_STDCALL*)(Args...)>()(args...);
    }

    /// @brief Calls the original method with the __fastcall calling convention.
    /// @tparam RetT The return type of the method.
    /// @tparam Args The argument types of the method.
    /// @param args The arguments to pass to the method.
    /// @return The return value of the method.
    template <typename RetT = void, typename... Args>
    auto fastcall(Args... args) -> RetT {
        return original<RetT(SAFETYHOOK_FASTCALL*)(Args...)>()(args...);
    }

private:
    friend class VmtHook;

    uint8_t* m_original_vm{};
    uint8_t* m_new_vm{};
    uint8_t** m_vmt_entry{};

    // This keeps the allocation alive until the hook is destroyed.
    std::shared_ptr<Allocation> m_new_vmt_allocation{};

    void destroy();
};

/// @brief A hook class that copies an entire VMT for a given object and replaces it.
class SAFETYHOOK_API VmtHook final {
public:
    /// @brief Error type for VmtHook.
    struct Error {
        /// @brief The type of error.
        enum : uint8_t {
            BAD_ALLOCATION, ///< An error occurred while allocating memory.
        } type;

        /// @brief Extra error information.
        union {
            Allocator::Error allocator_error; ///< Allocator error information.
        };

        /// @brief Create a BAD_ALLOCATION error.
        /// @param err The Allocator::Error that failed.
        /// @return The new BAD_ALLOCATION error.
        [[nodiscard]] static auto bad_allocation(Allocator::Error err) -> Error {
            return Error{
                .type = BAD_ALLOCATION,
                .allocator_error = err,
            };
        }
    };

    /// @brief Creates a new VmtHook object. Will clone the VMT of the given object and replace it.
    /// @param object The object to hook.
    /// @return The VmtHook object or a VmtHook::Error if an error occurred.
    [[nodiscard]] static auto create(void* object) -> std::expected<VmtHook, Error>;

    VmtHook() = default;
    VmtHook(const VmtHook&) = delete;
    VmtHook(VmtHook&& other) noexcept;
    auto operator=(const VmtHook&) -> VmtHook& = delete;
    auto operator=(VmtHook&& other) noexcept -> VmtHook&;
    ~VmtHook();

    /// @brief Applies the hook.
    /// @param object The object to apply the hook to.
    /// @note This will replace the VMT of the object with the new VMT. You can apply the hook to multiple objects.
    void apply(void* object);

    /// @brief Removes the hook.
    /// @param object The object to remove the hook from.
    void remove(void* object);

    /// @brief Removes the hook from all objects.
    void reset();

    /// @brief Hooks a method in the VMT.
    /// @param index The index of the method to hook.
    /// @param new_function The new function to use.
    template <typename T>
    [[nodiscard]] auto hook_method(size_t index, T new_function) -> std::expected<VmHook, Error> {
        VmHook hook{};

        ++index; // Skip RTTI pointer.
        hook.m_original_vm = m_new_vmt[index];
        store(reinterpret_cast<uint8_t*>(&hook.m_new_vm), new_function);
        hook.m_vmt_entry = &m_new_vmt[index];
        hook.m_new_vmt_allocation = m_new_vmt_allocation;
        m_new_vmt[index] = hook.m_new_vm;

        return hook;
    }

private:
    // Map of object instance to their original VMT.
    std::unordered_map<void*, uint8_t**> m_objects{};

    // The allocation is a shared_ptr, so it can be shared with VmHooks to ensure the memory is kept alive.
    std::shared_ptr<Allocation> m_new_vmt_allocation{};
    uint8_t** m_new_vmt{};

    void destroy();
};

} // namespace safetyhook
