#include "resettable.hpp"
#include <d3d9.h>
#include <mutex>
#include <vector>

using Reset = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
static Reset Orig_IDirect3DDevice9__Reset;

namespace dx {
    class ResetManager final {
    public:
        static auto get() -> ResetManager&;

        ResetManager(const ResetManager&) = delete;
        ResetManager& operator=(const ResetManager&) = delete;

        void register_resource(Resettable* resource);
        void unregister_resource(Resettable* resource);
        static auto WINAPI on_device_reset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params) -> HRESULT;

    private:
        ResetManager() = default;
        ~ResetManager() = default;

    private:
        std::vector<Resettable*> m_resources{};
        std::mutex m_mutex{};
    };
}

dx::Resettable::Resettable() {
    ResetManager::get().register_resource(this);
}

dx::Resettable::~Resettable() {
    ResetManager::get().unregister_resource(this);
}

dx::Resettable::Resettable(Resettable&& other) noexcept {
    ResetManager::get().unregister_resource(&other);
    ResetManager::get().register_resource(this);
}

auto dx::Resettable::operator=(Resettable&& other) noexcept -> Resettable& {
    if (this != &other) {
        ResetManager::get().unregister_resource(this);

        ResetManager::get().unregister_resource(&other);
        ResetManager::get().register_resource(this);
    }
    return *this;
}

auto dx::ResetManager::get() -> ResetManager& {
    static ResetManager instance;
    return instance;
}

void dx::ResetManager::register_resource(Resettable* resource) {
    if (resource == nullptr) {
        return;
    }

    const auto _ = std::scoped_lock{m_mutex};
    m_resources.push_back(resource);
}

void dx::ResetManager::unregister_resource(Resettable* resource) {
    const auto _ = std::scoped_lock{m_mutex};
    if (m_resources.empty()) {
        return;
    }

    auto it = std::ranges::find(m_resources, resource);
    if (it != m_resources.end()) {
        std::iter_swap(it, m_resources.end() - 1);
        m_resources.pop_back();
    }
}

auto dx::ResetManager::on_device_reset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params) -> HRESULT {
    auto& instance = get();
    
    const auto _ = std::scoped_lock{instance.m_mutex};

    for (auto* resource : instance.m_resources) {
        resource->on_device_lost();
    }

    const auto hr = Orig_IDirect3DDevice9__Reset(device, params);

    if (SUCCEEDED(hr)) {
        for (auto* resource : instance.m_resources) {
            resource->on_device_reset(device);
        }
    }

    return hr;
}

void dx::hook_device_reset(IDirect3DDevice9* device) {
    if (Orig_IDirect3DDevice9__Reset != nullptr) {
        return;
    }

    void** vtable = *reinterpret_cast<void***>(device);
    auto* method = reinterpret_cast<Reset*>(&vtable[16]);

    DWORD old_protect = {};
    VirtualProtect(static_cast<void*>(method), sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &old_protect);

    Orig_IDirect3DDevice9__Reset = *method;
    *method = ResetManager::on_device_reset;

    DWORD dummy = {};
    VirtualProtect(static_cast<void*>(method), sizeof(uintptr_t), old_protect, &dummy);
}
