#if __has_include("Zydis/Zydis.h")
#include "Zydis/Zydis.h"
#elif __has_include("Zydis.h")
#include "Zydis.h"
#else
#error "Zydis not found"
#endif

#include "safetyhook/allocator.hpp"
#include "safetyhook/os.hpp"
#include "safetyhook/utility.hpp"

#include "safetyhook/inline_hook.hpp"

namespace safetyhook {

#pragma pack(push, 1)
struct JmpE9 {
    uint8_t opcode{0xE9};
    uint32_t offset{0};
};

struct TrampolineEpilogueE9 {
    JmpE9 jmp_to_original{};
    JmpE9 jmp_to_destination{};
};
#pragma pack(pop)

static constexpr auto make_jmp_e9(const uint8_t* src, const uint8_t* dst) {
    JmpE9 jmp{};

    jmp.offset = static_cast<uint32_t>(dst - src - sizeof(jmp));

    return jmp;
}

[[nodiscard]] static auto emit_jmp_e9(uint8_t* src, uint8_t* dst, size_t size = sizeof(JmpE9)) -> std::expected<void, InlineHook::Error> {
    if (size < sizeof(JmpE9)) {
        return std::unexpected{InlineHook::Error::not_enough_space(dst)};
    }

    if (size > sizeof(JmpE9)) {
        std::fill_n(src, size, static_cast<uint8_t>(0x90));
    }

    store(src, make_jmp_e9(src, dst));

    return {};
}

static auto decode(ZydisDecodedInstruction* ix, uint8_t* ip) -> bool {
    ZydisDecoder decoder{};
    
    const auto status = ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LEGACY_32, ZYDIS_STACK_WIDTH_32);

    if (!ZYAN_SUCCESS(status)) {
        return false;
    }

    return ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&decoder, nullptr, ip, 15, ix));
}

auto InlineHook::create(void* target, void* destination, Flags flags) -> std::expected<InlineHook, Error> {
    return create(Allocator::global(), target, destination, flags);
}

auto InlineHook::create(const std::shared_ptr<Allocator>& allocator, void* target, void* destination, Flags flags) -> std::expected<InlineHook, Error> {
    InlineHook hook{};

    if (const auto setup_result =
            hook.setup(allocator, reinterpret_cast<uint8_t*>(target), reinterpret_cast<uint8_t*>(destination));
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

InlineHook::InlineHook(InlineHook&& other) noexcept {
    *this = std::move(other);
}

auto InlineHook::operator=(InlineHook&& other) noexcept -> InlineHook& {
    if (this != &other) {
        destroy();

        std::scoped_lock lock{m_mutex, other.m_mutex};

        m_target = other.m_target;
        m_destination = other.m_destination;
        m_trampoline = std::move(other.m_trampoline);
        m_trampoline_size = other.m_trampoline_size;
        m_original_bytes = std::move(other.m_original_bytes);
        m_enabled = other.m_enabled;
        m_type = other.m_type;

        other.m_target = nullptr;
        other.m_destination = nullptr;
        other.m_trampoline_size = 0;
        other.m_enabled = false;
        other.m_type = Type::Unset;
    }

    return *this;
}

InlineHook::~InlineHook() {
    destroy();
}

void InlineHook::reset() {
    *this = {};
}

auto InlineHook::setup(const std::shared_ptr<Allocator>& allocator, uint8_t* target, uint8_t* destination) -> std::expected<void, Error> {
    m_target = target;
    m_destination = destination;

    return e9_hook(allocator);
}

auto InlineHook::e9_hook(const std::shared_ptr<Allocator>& allocator) -> std::expected<void, Error> {
    m_original_bytes.clear();
    m_trampoline_size = sizeof(TrampolineEpilogueE9);

    std::vector<uint8_t*> desired_addresses{m_target};
    ZydisDecodedInstruction ix{};

    for (auto* ip = m_target; ip < m_target + sizeof(JmpE9); ip += ix.length) {
        if (!decode(&ix, ip)) {
            return std::unexpected{Error::failed_to_decode_instruction(ip)};
        }

        m_trampoline_size += ix.length;
        m_original_bytes.insert(m_original_bytes.end(), ip, ip + ix.length);

        const auto is_relative = (ix.attributes & ZYDIS_ATTRIB_IS_RELATIVE) != 0;

        if (is_relative) {
            if (ix.raw.disp.size == 32) {
                auto *const target_address = ip + ix.length + static_cast<int32_t>(ix.raw.disp.value);
                desired_addresses.emplace_back(target_address);
            } else if (ix.raw.imm[0].size == 32) {
                auto *const target_address = ip + ix.length + static_cast<int32_t>(ix.raw.imm[0].value.s);
                desired_addresses.emplace_back(target_address);
            } else if (ix.meta.category == ZYDIS_CATEGORY_COND_BR && ix.meta.branch_type == ZYDIS_BRANCH_TYPE_SHORT) {
                auto *const target_address = ip + ix.length + static_cast<int32_t>(ix.raw.imm[0].value.s);
                desired_addresses.emplace_back(target_address);
                m_trampoline_size += 4; // near conditional branches are 4 bytes larger.
            } else if (ix.meta.category == ZYDIS_CATEGORY_UNCOND_BR && ix.meta.branch_type == ZYDIS_BRANCH_TYPE_SHORT) {
                auto *const target_address = ip + ix.length + static_cast<int32_t>(ix.raw.imm[0].value.s);
                desired_addresses.emplace_back(target_address);
                m_trampoline_size += 3; // near unconditional branches are 3 bytes larger.
            } else {
                return std::unexpected{Error::unsupported_instruction_in_trampoline(ip)};
            }
        }
    }

    auto trampoline_allocation = allocator->allocate_near(desired_addresses, m_trampoline_size);

    if (!trampoline_allocation) {
        return std::unexpected{Error::bad_allocation(trampoline_allocation.error())};
    }

    m_trampoline = std::move(*trampoline_allocation);

    for (auto ip = m_target, tramp_ip = m_trampoline.data(); ip < m_target + m_original_bytes.size(); ip += ix.length) {
        if (!decode(&ix, ip)) {
            m_trampoline.free();
            return std::unexpected{Error::failed_to_decode_instruction(ip)};
        }

        const auto is_relative = (ix.attributes & ZYDIS_ATTRIB_IS_RELATIVE) != 0;

        if (is_relative && ix.raw.disp.size == 32) {
            std::copy_n(ip, ix.length, tramp_ip);
            auto *const target_address = ip + ix.length + ix.raw.disp.value;
            const auto new_disp = target_address - (tramp_ip + ix.length);
            store(tramp_ip + ix.raw.disp.offset, static_cast<int32_t>(new_disp));
            tramp_ip += ix.length;
        } else if (is_relative && ix.raw.imm[0].size == 32) {
            std::copy_n(ip, ix.length, tramp_ip);
            auto *const target_address = ip + ix.length + ix.raw.imm[0].value.s;
            const auto new_disp = target_address - (tramp_ip + ix.length);
            store(tramp_ip + ix.raw.imm[0].offset, static_cast<int32_t>(new_disp));
            tramp_ip += ix.length;
        } else if (ix.meta.category == ZYDIS_CATEGORY_COND_BR && ix.meta.branch_type == ZYDIS_BRANCH_TYPE_SHORT) {
            auto *const target_address = ip + ix.length + ix.raw.imm[0].value.s;
            auto new_disp = target_address - (tramp_ip + 6);

            // Handle the case where the target is now in the trampoline.
            if (target_address >= m_target && target_address < m_target + m_original_bytes.size()) {
                new_disp = static_cast<ptrdiff_t>(ix.raw.imm[0].value.s);
            }

            *tramp_ip = 0x0F;
            *(tramp_ip + 1) = 0x10 + ix.opcode;
            store(tramp_ip + 2, static_cast<int32_t>(new_disp));
            tramp_ip += 6;
        } else if (ix.meta.category == ZYDIS_CATEGORY_UNCOND_BR && ix.meta.branch_type == ZYDIS_BRANCH_TYPE_SHORT) {
            auto *const target_address = ip + ix.length + ix.raw.imm[0].value.s;
            auto new_disp = target_address - (tramp_ip + 5);

            // Handle the case where the target is now in the trampoline.
            if (target_address >= m_target && target_address < m_target + m_original_bytes.size()) {
                new_disp = static_cast<ptrdiff_t>(ix.raw.imm[0].value.s);
            }

            *tramp_ip = 0xE9;
            store(tramp_ip + 1, static_cast<int32_t>(new_disp));
            tramp_ip += 5;
        } else {
            std::copy_n(ip, ix.length, tramp_ip);
            tramp_ip += ix.length;
        }
    }

    auto* trampoline_epilogue = reinterpret_cast<TrampolineEpilogueE9*>(
        m_trampoline.address() + m_trampoline_size - sizeof(TrampolineEpilogueE9));

    // jmp from trampoline to original.
    auto* src = reinterpret_cast<uint8_t*>(&trampoline_epilogue->jmp_to_original);
    auto* dst = m_target + m_original_bytes.size();

    if (auto result = emit_jmp_e9(src, dst); !result) {
        return std::unexpected{result.error()};
    }

    // jmp from trampoline to destination.
    src = reinterpret_cast<uint8_t*>(&trampoline_epilogue->jmp_to_destination);
    dst = m_destination;

    if (auto result = emit_jmp_e9(src, dst); !result) {
        return std::unexpected{result.error()};
    }

    m_type = Type::E9;

    return {};
}

auto InlineHook::enable() -> std::expected<void, Error> {
    std::scoped_lock lock{m_mutex};

    if (m_enabled) {
        return {};
    }

    std::optional<Error> error;

    // jmp from original to trampoline.
    trap_threads(m_target, m_trampoline.data(), m_original_bytes.size(), [this, &error] {
        if (m_type == Type::E9) {
            auto* trampoline_epilogue = reinterpret_cast<TrampolineEpilogueE9*>(
                m_trampoline.address() + m_trampoline_size - sizeof(TrampolineEpilogueE9));

            if (auto result = emit_jmp_e9(m_target,
                    reinterpret_cast<uint8_t*>(&trampoline_epilogue->jmp_to_destination), m_original_bytes.size());
                !result) {
                error = result.error();
            }
        }
    });

    if (error) {
        return std::unexpected{*error};
    }

    m_enabled = true;

    return {};
}

auto InlineHook::disable() -> std::expected<void, Error> {
    std::scoped_lock lock{m_mutex};

    if (!m_enabled) {
        return {};
    }

    trap_threads(m_trampoline.data(), m_target, m_original_bytes.size(),
        [this] { std::copy(m_original_bytes.begin(), m_original_bytes.end(), m_target); });

    m_enabled = false;

    return {};
}

void InlineHook::destroy() {
    [[maybe_unused]] auto disable_result = disable();

    std::scoped_lock lock{m_mutex};

    if (!m_trampoline) {
        return;
    }

    m_trampoline.free();
}

} // namespace safetyhook
