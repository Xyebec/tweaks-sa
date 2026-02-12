#include <algorithm>
#include <array>

#include "safetyhook/allocator.hpp"
#include "safetyhook/inline_hook.hpp"
#include "safetyhook/utility.hpp"

#include "safetyhook/mid_hook.hpp"

namespace safetyhook {

#if SAFETYHOOK_DISABLE_X86_SSE
static constexpr std::array<uint8_t, 54> asm_data = {
    // Save context
    0x68, 0x00, 0x00, 0x00, 0x00, // push trampoline address
    0x54, // push esp // push trampoline esp
    0x54, // push esp // push original esp (this gets fixed later)
    0x55, // push ebp
    0x50, // push eax
    0x53, // push ebx
    0x51, // push ecx
    0x52, // push edx
    0x56, // push esi
    0x57, // push edi
    0x9C, // pushfd

    // Fix stored esp
    0x8B, 0x4C, 0x24, 0x20, // mov ecx, dword ptr [esp + 32]
    0x83, 0xC1, 0x08, // add ecx, 8
    0x89, 0x4C, 0x24, 0x20, // mov dword ptr [esp + 32], ecx

    // Call destination
    0x54, // push esp
    0xFF, 0x15, 0x32, 0x00, 0x00, 0x00, // call dword ptr [0x32] // [destination]
    0x83, 0xC4, 0x04, // add esp, 4

    // Restore context
    0x9D, // popfd
    0x5F, // pop edi
    0x5E, // pop esi
    0x5A, // pop edx
    0x59, // pop ecx
    0x5B, // pop ebx
    0x58, // pop eax
    0x5D, // pop ebp
    0x8D, 0x64, 0x24, 0x04, // lea esp, [esp + 4] // Skip original esp
    0x5C, // pop esp
    0xC3, // ret

    // destination:
    0x00, 0x00, 0x00, 0x00,
};

static void populate_stub_addresses(uint8_t* stub, MidHookFn destination, const uint8_t* trampoline) {
    store(stub + sizeof(asm_data) - 4, destination);

    // Fix absolute address for the `call dword ptr [destination]`
    store(stub + 29, stub + sizeof(asm_data) - 4);
    
    store(stub + 1, trampoline);
}
#else
static constexpr std::array<uint8_t, 166> asm_data = {
    // Save context
    0x68, 0x00, 0x00, 0x00, 0x00, // push trampoline address
    0x54, // push esp // push trampoline esp
    0x54, // push esp // push original esp (this gets fixed later)
    0x55, // push ebp
    0x50, // push eax
    0x53, // push ebx
    0x51, // push ecx
    0x52, // push edx
    0x56, // push esi
    0x57, // push edi
    0x9C, // pushfd
    0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, // sub esp, 128
    0xF3, 0x0F, 0x7F, 0x7C, 0x24, 0x70, // movdqu xmmword ptr [esp + 112], xmm7
    0xF3, 0x0F, 0x7F, 0x74, 0x24, 0x60, // movdqu xmmword ptr [esp + 96], xmm6
    0xF3, 0x0F, 0x7F, 0x6C, 0x24, 0x50, // movdqu xmmword ptr [esp + 80], xmm5
    0xF3, 0x0F, 0x7F, 0x64, 0x24, 0x40, // movdqu xmmword ptr [esp + 64], xmm4
    0xF3, 0x0F, 0x7F, 0x5C, 0x24, 0x30, // movdqu xmmword ptr [esp + 48], xmm3
    0xF3, 0x0F, 0x7F, 0x54, 0x24, 0x20, // movdqu xmmword ptr [esp + 32], xmm2
    0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x10, // movdqu xmmword ptr [esp + 16], xmm1
    0xF3, 0x0F, 0x7F, 0x04, 0x24, // movdqu xmmword ptr [esp], xmm0
    
    // Fix stored esp
    0x8B, 0x8C, 0x24, 0xA0, 0x00, 0x00, 0x00, // mov ecx, dword ptr [esp + 160]
    0x83, 0xC1, 0x08, // add ecx, 8
    0x89, 0x8C, 0x24, 0xA0, 0x00, 0x00, 0x00, // mov dword ptr [esp + 160], ecx
    
    // Call destination
    0x54, // push esp
    0xFF, 0x15, 0xA3, 0x00, 0x00, 0x00, // call dword ptr [0xa3] // [destination]
    0x83, 0xC4, 0x04, // add esp, 4
    
    // Restore context
    0xF3, 0x0F, 0x6F, 0x04, 0x24, // movdqu xmm0, xmmword ptr [esp]
    0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x10, // movdqu xmm1, xmmword ptr [esp + 16]
    0xF3, 0x0F, 0x6F, 0x54, 0x24, 0x20, // movdqu xmm2, xmmword ptr [esp + 32]
    0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x30, // movdqu xmm3, xmmword ptr [esp + 48]
    0xF3, 0x0F, 0x6F, 0x64, 0x24, 0x40, // movdqu xmm4, xmmword ptr [esp + 64]
    0xF3, 0x0F, 0x6F, 0x6C, 0x24, 0x50, // movdqu xmm5, xmmword ptr [esp + 80]
    0xF3, 0x0F, 0x6F, 0x74, 0x24, 0x60, // movdqu xmm6, xmmword ptr [esp + 96]
    0xF3, 0x0F, 0x6F, 0x7C, 0x24, 0x70, // movdqu xmm7, xmmword ptr [esp + 112]
    0x81, 0xC4, 0x80, 0x00, 0x00, 0x00, // add esp, 128
    0x9D, // popfd
    0x5F, // pop edi
    0x5E, // pop esi
    0x5A, // pop edx
    0x59, // pop ecx
    0x5B, // pop ebx
    0x58, // pop eax
    0x5D, // pop ebp
    0x8D, 0x64, 0x24, 0x04, // lea esp, [esp + 4] // Skip original esp
    0x5C, // pop esp
    0xC3, // ret
    
    // destination:
    0x00, 0x00, 0x00, 0x00,
};

static void populate_stub_addresses(uint8_t* stub, MidHookFn destination, const uint8_t* trampoline) {
    store(stub + sizeof(asm_data) - 4, destination);

    // Fix absolute address for the `call dword ptr [destination]`
    store(stub + 88, stub + sizeof(asm_data) - 4);

    store(stub + 1, trampoline);
}
#endif // SAFETYHOOK_DISABLE_X86_SSE

auto MidHook::create(void* target, MidHookFn destination, Flags flags) -> std::expected<MidHook, Error> {
    return create(Allocator::global(), target, destination, flags);
}

auto MidHook::create(const std::shared_ptr<Allocator>& allocator, void* target, MidHookFn destination, Flags flags) -> std::expected<MidHook, Error> {
    MidHook hook{};

    if (const auto setup_result = hook.setup(allocator, reinterpret_cast<uint8_t*>(target), destination);
        !setup_result) {
        return std::unexpected{setup_result.error()};
    }

    if ((flags & StartDisabled) == 0) {
        if (auto enable_result = hook.enable(); !enable_result) {
            return std::unexpected{enable_result.error()};
        }
    }

    return hook;
}

MidHook::MidHook(MidHook&& other) noexcept {
    *this = std::move(other);
}

auto MidHook::operator=(MidHook&& other) noexcept -> MidHook& {
    if (this != &other) {
        m_hook = std::move(other.m_hook);
        m_target = other.m_target;
        m_stub = std::move(other.m_stub);
        m_destination = other.m_destination;

        other.m_target = 0;
        other.m_destination = nullptr;
    }

    return *this;
}

void MidHook::reset() {
    *this = {};
}

auto MidHook::setup(const std::shared_ptr<Allocator>& allocator, uint8_t* target, MidHookFn destination_fn) -> std::expected<void, Error> {
    m_target = target;
    m_destination = destination_fn;

    auto stub_allocation = allocator->allocate(asm_data.size());

    if (!stub_allocation) {
        return std::unexpected{Error::bad_allocation(stub_allocation.error())};
    }

    m_stub = std::move(*stub_allocation);

    std::ranges::copy(asm_data, m_stub.data());

    auto hook_result = InlineHook::create(allocator, m_target, m_stub.data(), InlineHook::StartDisabled);

    if (!hook_result) {
        m_stub.free();
        return std::unexpected{Error::bad_inline_hook(hook_result.error())};
    }

    m_hook = std::move(*hook_result);

    populate_stub_addresses(m_stub.data(), m_destination, m_hook.trampoline().data());

    return {};
}

auto MidHook::enable() -> std::expected<void, Error> {
    if (auto enable_result = m_hook.enable(); !enable_result) {
        return std::unexpected{Error::bad_inline_hook(enable_result.error())};
    }

    return {};
}

auto MidHook::disable() -> std::expected<void, Error> {
    if (auto disable_result = m_hook.disable(); !disable_result) {
        return std::unexpected{Error::bad_inline_hook(disable_result.error())};
    }

    return {};
}

} // namespace safetyhook
