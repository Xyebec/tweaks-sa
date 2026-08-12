#pragma once

#include "resettable.hpp"
#include <d3d9.h>
#include <cstring>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace dx::detail {
    struct ReleaseDeleter {
        void operator()(IUnknown* ptr) const noexcept {
            if (ptr != nullptr) { ptr->Release(); }
        }
    };

    template <typename T>
    using Handle = std::unique_ptr<T, ReleaseDeleter>;

    constexpr auto get_decltype_size(D3DDECLTYPE decl) -> BYTE {
        switch (decl) {
            case D3DDECLTYPE_FLOAT1:    return 4;  // f32
            case D3DDECLTYPE_FLOAT2:    return 8;  // f32[2]
            case D3DDECLTYPE_FLOAT3:    return 12; // f32[3]
            case D3DDECLTYPE_FLOAT4:    return 16; // f32[4]
            case D3DDECLTYPE_D3DCOLOR:             //  u8[4] normalized
            case D3DDECLTYPE_UBYTE4:               //  u8[4]
            case D3DDECLTYPE_SHORT2:    return 4;  // i16[2]
            case D3DDECLTYPE_SHORT4:    return 8;  // i16[4]
            case D3DDECLTYPE_UBYTE4N:              //  u8[4] normalized
            case D3DDECLTYPE_SHORT2N:   return 4;  // i16[2] normalized
            case D3DDECLTYPE_SHORT4N:   return 8;  // i16[4] normalized
            case D3DDECLTYPE_USHORT2N:  return 4;  // u16[2] normalized
            case D3DDECLTYPE_USHORT4N:  return 8;  // u16[4] normalized
            case D3DDECLTYPE_UDEC3:                // u10[3] (2 unused bits)
            case D3DDECLTYPE_DEC3N:                // i10[3] normalized (2 unused bits)
            case D3DDECLTYPE_FLOAT16_2: return 4;  // f16[2]
            case D3DDECLTYPE_FLOAT16_4: return 8;  // f16[4]
            case D3DDECLTYPE_UNUSED:
            default:                    return 0;
        }
    }
}

namespace dx {
    enum class BufferType  { Vertex, Index, Instance };
    enum class BufferUsage { Static, Dynamic };

    template <typename T, BufferType BufType>
    class Buffer final : public Resettable {
    public:
        using InterfaceType = std::conditional_t<BufType == BufferType::Index, IDirect3DIndexBuffer9, IDirect3DVertexBuffer9>;

        constexpr Buffer(BufferUsage usage) noexcept
            : m_usage{usage} {}

        Buffer(IDirect3DDevice9* device, BufferUsage usage, size_t initial_capacity)
            : Buffer{usage}
        {
            reserve(device, initial_capacity);
        }

        Buffer(IDirect3DDevice9* device, BufferUsage usage, std::span<const T> data)
            : Buffer{usage}
        {
            assign(device, data);
        }

        void on_device_lost() override {
            if (m_usage == BufferUsage::Dynamic) {
                m_gpu.reset();
                m_size = 0;
            }
        }

        void on_device_reset(IDirect3DDevice9* device) override {
            if (m_usage == BufferUsage::Dynamic) {
                if (m_capacity > 0) {
                    m_gpu = allocate_gpu_buffer(device, m_capacity, m_usage);
                }
            }
        }

        static constexpr auto stride() noexcept -> size_t { return sizeof(T); }

        auto get() const noexcept -> InterfaceType* { return m_gpu.get(); }
        auto size() const noexcept -> size_t { return m_size; }
        auto empty() const noexcept -> bool { return m_size == 0; }
        auto capacity() const noexcept -> size_t { return m_capacity; }
        auto size_bytes() const noexcept -> size_t { return m_size * sizeof(T); }

        void reserve(IDirect3DDevice9* device, size_t new_capacity) {
            if (m_capacity >= new_capacity) {
                return;
            }

            auto new_buffer = allocate_gpu_buffer(device, new_capacity, m_usage);

            if (m_size > 0 && m_gpu) {
                if (m_usage == BufferUsage::Static) {
                    const auto size_bytes = m_size * sizeof(T);
                    void* src = nullptr;
                    if (FAILED(m_gpu->Lock(0, size_bytes, &src, D3DLOCK_READONLY))) {
                        throw std::runtime_error{"Failed to lock src GPU buffer"};
                    }
                    void* dst = nullptr;
                    if (FAILED(new_buffer->Lock(0, size_bytes, &dst, D3DLOCK_DISCARD))) {
                        throw std::runtime_error{"Failed to lock dst GPU buffer"};
                    }
                    std::memcpy(dst, src, size_bytes);
                    m_gpu->Unlock();
                    new_buffer->Unlock();
                } else {
                    m_size = 0;
                }
            }

            m_gpu = std::move(new_buffer);
            m_capacity = new_capacity;
        }

        void append(IDirect3DDevice9* device, std::span<const T> data) {
            if (data.empty()) {
                return;
            }

            if (m_size + data.size() > m_capacity) {
                const auto recommended_capacity = m_capacity + m_capacity / 2;
                const auto new_capacity = std::max(m_size + data.size(), recommended_capacity);
                reserve(device, new_capacity);
            }

            const auto offset = m_size * sizeof(T);
            const auto size_to_lock = data.size_bytes();
            
            DWORD lock_flags = 0;
            if (m_usage == BufferUsage::Dynamic) {
                lock_flags = m_size == 0 ? D3DLOCK_DISCARD : D3DLOCK_NOOVERWRITE;
            }

            void* dst = nullptr;
            if (FAILED(m_gpu->Lock(offset, size_to_lock, &dst, lock_flags))) {
                throw std::runtime_error{"Failed to lock GPU buffer"};
            }

            std::memcpy(dst, data.data(), size_to_lock);
            m_gpu->Unlock();
            m_size += data.size();
        }

        void append(IDirect3DDevice9* device, std::initializer_list<T> data) {
            append(device, std::span{data.begin(), data.end()});
        }

        void assign(IDirect3DDevice9* device, std::span<const T> data) {
            clear();
            append(device, data);
        }

        void assign(IDirect3DDevice9* device, std::initializer_list<T> data) {
            assign(device, std::span{data.begin(), data.end()});
        }

        void clear() {
            m_size = 0;
        }

        void bind(IDirect3DDevice9* device) const requires (BufType == BufferType::Index) {
            device->SetIndices(m_gpu.get());
        }

        void bind(IDirect3DDevice9* device, WORD stream) const requires (BufType == BufferType::Vertex) {
            device->SetStreamSource(stream, m_gpu.get(), 0, stride());
        }

        void bind_instanced(IDirect3DDevice9* device, WORD stream, UINT num_instances) const requires (BufType == BufferType::Vertex) {
            device->SetStreamSourceFreq(stream, (D3DSTREAMSOURCE_INDEXEDDATA | num_instances));
            device->SetStreamSource(stream, m_gpu.get(), 0, stride());
        }

        void bind(IDirect3DDevice9* device, WORD stream) const requires (BufType == BufferType::Instance) {
            device->SetStreamSourceFreq(stream, (D3DSTREAMSOURCE_INSTANCEDATA | 1));
            device->SetStreamSource(stream, m_gpu.get(), 0, stride());
        }

    private:
        static auto allocate_gpu_buffer(IDirect3DDevice9* device, size_t capacity, BufferUsage usage) -> detail::Handle<InterfaceType> {
            const auto size_bytes = capacity * sizeof(T);
            InterfaceType* gpu_buffer = nullptr;
            auto hr = HRESULT{};

            const auto d3d_usage = usage == BufferUsage::Dynamic
                ? (D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC)
                : (D3DUSAGE_WRITEONLY);
            
            const auto pool = usage == BufferUsage::Dynamic
                ? D3DPOOL_DEFAULT
                : D3DPOOL_MANAGED;

            if constexpr (BufType == BufferType::Index) {
                constexpr auto INDEX_FORMAT = []() {
                    if      constexpr (sizeof(T) == 4) { return D3DFMT_INDEX32; }
                    else if constexpr (sizeof(T) == 2) { return D3DFMT_INDEX16; }
                    else { static_assert(false, "D3D9 only supports 16-bit and 32-bit indices"); }
                }();
                hr = device->CreateIndexBuffer(size_bytes, d3d_usage, INDEX_FORMAT, pool, &gpu_buffer, nullptr);
            } else {
                hr = device->CreateVertexBuffer(size_bytes, d3d_usage, 0, pool, &gpu_buffer, nullptr);
            }

            if (FAILED(hr)) {
                throw std::runtime_error{"Failed to create GPU buffer"};
            }

            return detail::Handle<InterfaceType>{gpu_buffer};
        }

    private:
        detail::Handle<InterfaceType> m_gpu{};
        size_t m_capacity{};
        size_t m_size{};
        BufferUsage m_usage;
    };

    template <typename T> using VertexBuffer   = Buffer<T, BufferType::Vertex>;
    template <typename T> using IndexBuffer    = Buffer<T, BufferType::Index>;
    template <typename T> using InstanceBuffer = Buffer<T, BufferType::Instance>;

    template <typename T, BufferType BufType>
    class DynBuffer final {
    public:
        constexpr DynBuffer() noexcept
            : m_gpu{BufferUsage::Dynamic} {}

        explicit DynBuffer(IDirect3DDevice9* device, size_t initial_capacity)
            : m_gpu{device, BufferUsage::Dynamic, initial_capacity}
        {
            m_cpu.reserve(initial_capacity);
        }

        static constexpr auto stride() noexcept -> size_t { return sizeof(T); }

        auto get_gpu() -> Buffer<T, BufType>& { return m_gpu; }
        auto get_raw() const -> Buffer<T, BufType>::InterfaceType* { return m_gpu.get(); }

        auto data() noexcept -> T* { return m_cpu.data(); }
        auto data() const noexcept -> const T* { return m_cpu.data(); }
        auto size() const noexcept -> size_t { return m_cpu.size(); }
        auto empty() const noexcept -> bool { return m_cpu.empty(); }
        auto capacity() const noexcept -> size_t { return m_cpu.capacity(); }
        auto size_bytes() const noexcept -> size_t { return m_cpu.size() * sizeof(T); }
        auto span() const noexcept -> std::span<const T> { return m_cpu; }

        void push_back(const T& item) {
            m_cpu.push_back(item);
        }
        
        void push_back(T&& item) {
            m_cpu.emplace_back(std::move(item));
        }

        template <typename... Args>
        auto emplace_back(Args&&... args) -> T& {
            return m_cpu.emplace_back(std::forward<Args>(args)...);
        }

        auto insert(std::vector<T>::const_iterator pos, std::initializer_list<T> list) {
            return m_cpu.insert(pos, list);
        }

        auto insert_back(std::initializer_list<T> list) {
            return insert(m_cpu.end(), list);
        }

        auto insert(std::vector<T>::const_iterator pos, std::span<const T> list) {
            return m_cpu.insert_range(pos, list);
        }

        auto insert_back(std::span<const T> list) {
            return insert(m_cpu.end(), list);
        }

        void reserve(size_t new_capacity) {
            m_cpu.reserve(new_capacity);
        }

        void clear() noexcept {
            m_cpu.clear();
        }

        void shrink_to_fit() {
            m_cpu.shrink_to_fit();
        }

        void sync(IDirect3DDevice9* device) {
            m_gpu.assign(device, m_cpu);
        }

    private:
        std::vector<T> m_cpu{};
        Buffer<T, BufType> m_gpu{};
    };

    template <typename T> using DynVertexBuffer   = DynBuffer<T, BufferType::Vertex>;
    template <typename T> using DynIndexBuffer    = DynBuffer<T, BufferType::Index>;
    template <typename T> using DynInstanceBuffer = DynBuffer<T, BufferType::Instance>;

    template <size_t N = 0, size_t NumStreams = 0>
    struct LayoutBuilder final {
        std::array<D3DVERTEXELEMENT9, N> m_elements{};
        std::array<WORD, NumStreams> m_offsets{};

        template <WORD StreamId>
        consteval auto add(D3DDECLTYPE type, BYTE usage, BYTE usage_index) const {
            constexpr size_t next_stream_count = (StreamId >= NumStreams) ? (StreamId + 1) : NumStreams;

            auto next = LayoutBuilder<N + 1, next_stream_count>{};
            std::ranges::copy(m_elements, next.m_elements.begin());

            if constexpr (next_stream_count > NumStreams) {
                std::ranges::copy(m_offsets, next.m_offsets.begin());
            } else {
                next.m_offsets = m_offsets;
            }

            const auto decltype_size = detail::get_decltype_size(type);
            const auto current_offset = next.m_offsets[StreamId];
            next.m_elements[N] = { StreamId, current_offset, static_cast<BYTE>(type), D3DDECLMETHOD_DEFAULT, usage, usage_index };
            next.m_offsets[StreamId] = current_offset + decltype_size;

            return next;
        }

        consteval auto build() const {
            auto elements = std::array<D3DVERTEXELEMENT9, N + 1>{};
            std::ranges::copy(m_elements, elements.begin());
            elements[N] = D3DDECL_END();
            return elements;
        }
    };

    class Shaders final {
    public:
        struct Shader {
            std::string_view source;
            const char* entry_point;
        };

        struct Desc {
            std::span<const D3DVERTEXELEMENT9> layout;
            Shader vs;
            Shader ps;
        };

    public:
        static auto create(IDirect3DDevice9* device, Desc desc) -> std::expected<Shaders, std::string>;
        void bind(IDirect3DDevice9* device) const;

    private:
        Shaders(
            detail::Handle<IDirect3DVertexDeclaration9> layout,
            detail::Handle<IDirect3DVertexShader9> vs,
            detail::Handle<IDirect3DPixelShader9> ps
        ) noexcept
            : m_layout{std::move(layout)}
            , m_vs{std::move(vs)}
            , m_ps{std::move(ps)}
        {}

        detail::Handle<IDirect3DVertexDeclaration9> m_layout;
        detail::Handle<IDirect3DVertexShader9> m_vs;
        detail::Handle<IDirect3DPixelShader9> m_ps;
    };

    struct StateArg {
        D3DRENDERSTATETYPE type{};
        DWORD value{};
        bool modify{};

        constexpr StateArg() noexcept = default;

        template <typename T>
        requires (sizeof(T) <= sizeof(DWORD) && std::is_trivially_copyable_v<T>)
        constexpr StateArg(D3DRENDERSTATETYPE type, T val, bool modify = true) noexcept
            : type{type}
            , modify{modify}
        {
            if constexpr (sizeof(T) == sizeof(DWORD)) {
                value = std::bit_cast<DWORD>(val);
            } else {
                if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
                    value = static_cast<DWORD>(val);
                } else {
                    value = 0;
                    std::memcpy(&value, &val, sizeof(T));
                }
            }
        }
    };

    template <size_t N>
    class [[nodiscard]] ScopedState final {
    public:
        ScopedState(IDirect3DDevice9* device, const StateArg (&states)[N]) noexcept
            : m_device{device}
        {
            for (size_t i = 0; i < N; i++) {
                if (!states[i].modify) {
                    continue;
                }

                auto current_value = DWORD{};
                m_device->GetRenderState(states[i].type, &current_value);

                if (current_value == states[i].value) {
                    continue;
                }

                m_saved[i] = {
                    .type = states[i].type,
                    .value = current_value,
                };
                m_device->SetRenderState(states[i].type, states[i].value);
            }
        }

        ScopedState(IDirect3DDevice9* device, const StateArg& state) noexcept requires (N == 1)
            : ScopedState{device, reinterpret_cast<const StateArg(&)[1]>(state)} {}

        ~ScopedState() noexcept {
            for (size_t i = N; i > 0; i--) {
                const auto& saved = m_saved[i - 1];

                if (saved.has_value()) {
                    m_device->SetRenderState(saved->type, saved->value);
                }
            }
        }

        ScopedState(const ScopedState&) = delete;
        ScopedState& operator=(const ScopedState&) = delete;
        ScopedState(ScopedState&&) = delete;
        ScopedState& operator=(ScopedState&&) = delete;

    public:
        struct SavedValue {
            D3DRENDERSTATETYPE type{};
            DWORD value{};
        };

        IDirect3DDevice9* m_device{};
        std::array<std::optional<SavedValue>, N> m_saved{};
    };

    ScopedState(IDirect3DDevice9*, const StateArg&) -> ScopedState<1>;

    template <typename V, typename I>
    struct Mesh {
        VertexBuffer<V> vertices;
        IndexBuffer<I>  indices;
    };
}
