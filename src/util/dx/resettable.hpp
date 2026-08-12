#pragma once

class IDirect3DDevice9;

namespace dx {
    class Resettable {
    public:
        Resettable();
        virtual ~Resettable();

        Resettable(const Resettable&) = delete;
        Resettable& operator=(const Resettable&) = delete;

        Resettable(Resettable&& other) noexcept;
        Resettable& operator=(Resettable&& other) noexcept;

        virtual void on_device_lost() = 0;
        virtual void on_device_reset(IDirect3DDevice9* device) = 0;
    };

    void hook_device_reset(IDirect3DDevice9* device);
}
