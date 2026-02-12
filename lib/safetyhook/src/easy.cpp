#include "safetyhook/easy.hpp"

namespace safetyhook {

auto create_inline(void* target, void* destination, InlineHook::Flags flags) -> InlineHook {
    if (auto hook = InlineHook::create(target, destination, flags)) {
        return std::move(*hook);
    } else {
        return {};
    }
}

auto create_mid(void* target, MidHookFn destination, MidHook::Flags flags) -> MidHook {
    if (auto hook = MidHook::create(target, destination, flags)) {
        return std::move(*hook);
    } else {
        return {};
    }
}

auto create_vmt(void* object) -> VmtHook {
    if (auto hook = VmtHook::create(object)) {
        return std::move(*hook);
    } else {
        return {};
    }
}

} // namespace safetyhook