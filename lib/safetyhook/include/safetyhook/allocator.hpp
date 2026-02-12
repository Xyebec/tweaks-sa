/// @file safetyhook/allocator.hpp
/// @brief Allocator for allocating memory near target addresses.

#pragma once

#ifndef SAFETYHOOK_USE_CXXMODULES
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <vector>
#else
import std.compat;
#endif

#include "./common.hpp"

namespace safetyhook {

class Allocator;

/// @brief A memory allocation.
class SAFETYHOOK_API Allocation final {
public:
    Allocation() = default;
    Allocation(const Allocation&) = delete;
    Allocation(Allocation&& other) noexcept;
    auto operator=(const Allocation&) -> Allocation& = delete;
    auto operator=(Allocation&& other) noexcept -> Allocation&;
    ~Allocation();

    /// @brief Frees the allocation.
    /// @note This is called automatically when the Allocation object is destroyed.
    void free();

    /// @brief Returns a pointer to the data of the allocation.
    /// @return Pointer to the data of the allocation.
    [[nodiscard]] auto data() const noexcept -> uint8_t* { return m_address; }

    /// @brief Returns the address of the allocation.
    /// @return The address of the allocation.
    [[nodiscard]] auto address() const noexcept -> uintptr_t { return reinterpret_cast<uintptr_t>(m_address); }

    /// @brief Returns the size of the allocation.
    /// @return The size of the allocation.
    [[nodiscard]] auto size() const noexcept -> size_t { return m_size; }

    /// @brief Tests if the allocation is valid.
    /// @return True if the allocation is valid, false otherwise.
    explicit operator bool() const noexcept { return m_address != nullptr && m_size != 0; }

protected:
    friend Allocator;

    Allocation(std::shared_ptr<Allocator> allocator, uint8_t* address, size_t size) noexcept;

private:
    std::shared_ptr<Allocator> m_allocator{};
    uint8_t* m_address{};
    size_t m_size{};
};

/// @brief Allocates memory near target addresses.
class SAFETYHOOK_API Allocator final : public std::enable_shared_from_this<Allocator> {
public:
    /// @brief Returns the global Allocator.
    /// @return The global Allocator.
    [[nodiscard]] static auto global() -> std::shared_ptr<Allocator>;

    /// @brief Creates a new Allocator.
    /// @return The new Allocator.
    [[nodiscard]] static auto create() -> std::shared_ptr<Allocator>;

    Allocator(const Allocator&) = delete;
    Allocator(Allocator&&) noexcept = delete;
    auto operator=(const Allocator&) -> Allocator& = delete;
    auto operator=(Allocator&&) noexcept -> Allocator& = delete;
    ~Allocator() = default;

    /// @brief The error type returned by the allocate functions.
    enum class Error : uint8_t {
        BAD_VIRTUAL_ALLOC,  ///< VirtualAlloc failed.
        NO_MEMORY_IN_RANGE, ///< No memory in range.
    };

    /// @brief Allocates memory.
    /// @param size The size of the allocation.
    /// @return The Allocation or an Allocator::Error if the allocation failed.
    [[nodiscard]] auto allocate(size_t size) -> std::expected<Allocation, Error>;

    /// @brief Allocates memory near a target address.
    /// @param desired_addresses The target address.
    /// @param size The size of the allocation.
    /// @param max_distance The maximum distance from the target address.
    /// @return The Allocation or an Allocator::Error if the allocation failed.
    [[nodiscard]] auto allocate_near(const std::vector<uint8_t*>& desired_addresses,
        size_t size, size_t max_distance = 0x7FFF'FFFF) -> std::expected<Allocation, Error>;

protected:
    friend Allocation;

    void free(uint8_t* address, size_t size);

private:
    struct FreeNode {
        std::unique_ptr<FreeNode> next{};
        uint8_t* start{};
        uint8_t* end{};
    };

    struct Memory {
        uint8_t* address{};
        size_t size{};
        std::unique_ptr<FreeNode> freelist{};

        ~Memory();
    };

    std::vector<std::unique_ptr<Memory>> m_memory{};
    std::mutex m_mutex{};

    Allocator() = default;

    [[nodiscard]] auto internal_allocate_near(const std::vector<uint8_t*>& desired_addresses,
        size_t size, size_t max_distance = 0x7FFF'FFFF) -> std::expected<Allocation, Error>;
    void internal_free(uint8_t* address, size_t size);

    static void combine_adjacent_freenodes(Memory& memory);
    [[nodiscard]] static auto allocate_nearby_memory(const std::vector<uint8_t*>& desired_addresses,
        size_t size, size_t max_distance) -> std::expected<uint8_t*, Error>;
    [[nodiscard]] static auto in_range(
        const uint8_t* address, const std::vector<uint8_t*>& desired_addresses, size_t max_distance) -> bool;
};

} // namespace safetyhook
