/// @file safetyhook/easy.hpp
/// @brief Easy to use API for creating hooks.

#pragma once

#include "./common.hpp"
#include "./inline_hook.hpp"
#include "./mid_hook.hpp"
#include "./vmt_hook.hpp"

namespace safetyhook {

/// @brief Easy to use API for creating an InlineHook.
/// @param target The address of the function to hook.
/// @param destination The address of the destination function.
/// @param flags The flags to use.
/// @return The InlineHook object.
[[nodiscard]] auto SAFETYHOOK_API create_inline(
    void* target, void* destination, InlineHook::Flags flags = InlineHook::Default) -> InlineHook;

/// @brief Easy to use API for creating an InlineHook.
/// @param target The address of the function to hook.
/// @param destination The address of the destination function.
/// @param flags The flags to use.
/// @return The InlineHook object.
template <typename T, typename U>
[[nodiscard]] auto create_inline(T target, U destination, InlineHook::Flags flags = InlineHook::Default) -> InlineHook {
    return create_inline(reinterpret_cast<void*>(target), reinterpret_cast<void*>(destination), flags);
}

/// @brief Easy to use API for creating a MidHook.
/// @param target the address of the function to hook.
/// @param destination The destination function.
/// @param flags The flags to use.
/// @return The MidHook object.
[[nodiscard]] auto SAFETYHOOK_API create_mid(
    void* target, MidHookFn destination, MidHook::Flags flags = MidHook::Default) -> MidHook;

/// @brief Easy to use API for creating a MidHook.
/// @param target the address of the function to hook.
/// @param destination The destination function.
/// @param flags The flags to use.
/// @return The MidHook object.
template <typename T>
[[nodiscard]] auto create_mid(T target, MidHookFn destination, MidHook::Flags flags = MidHook::Default) -> MidHook {
    return create_mid(reinterpret_cast<void*>(target), destination, flags);
}

/// @brief Easy to use API for creating a VmtHook.
/// @param object The object to hook.
/// @return The VmtHook object.
[[nodiscard]] auto SAFETYHOOK_API create_vmt(void* object) -> VmtHook;

/// @brief Easy to use API for creating a VmHook.
/// @param vmt The VmtHook to use to create the VmHook.
/// @param index The index of the method to hook.
/// @param destination The destination function.
/// @return The VmHook object.
template <typename T>
[[nodiscard]] auto create_vm(VmtHook& vmt, size_t index, T destination) -> VmHook {
    if (auto hook = vmt.hook_method(index, destination)) {
        return std::move(*hook);
    } else {
        return {};
    }
}

} // namespace safetyhook
