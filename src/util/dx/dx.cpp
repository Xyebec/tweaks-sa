#include "dx.hpp"
#include <d3dx9.h>
#include <expected>

static auto compile_shader(std::string_view source, const char* entry_point, const char* profile) -> std::expected<dx::detail::Handle<ID3DXBuffer>, std::string> {
    ID3DXBuffer* shader_code = nullptr;
    ID3DXBuffer* error_buffer = nullptr;
    ID3DXConstantTable* constant_table = nullptr;

    const auto hr = D3DXCompileShader(
        source.data(),
        source.size(),
        nullptr,
        nullptr,
        entry_point,
        profile,
        0,
        &shader_code,
        &error_buffer,
        &constant_table
    );

    if (FAILED(hr)) {
        auto message = std::string{static_cast<const char*>(error_buffer->GetBufferPointer()), error_buffer->GetBufferSize()};
        error_buffer->Release();
        return std::unexpected{std::move(message)};
    }

    constant_table->Release();
    return dx::detail::Handle<ID3DXBuffer>{shader_code};
}

auto dx::Shaders::create(IDirect3DDevice9* device, Desc desc) -> std::expected<Shaders, std::string> {
    auto result_vs = compile_shader(desc.vs.source, desc.vs.entry_point, "vs_3_0");
    if (!result_vs.has_value()) {
        return std::unexpected{std::move(result_vs.error())};
    }

    IDirect3DVertexShader9* shader_vs = nullptr;
    device->CreateVertexShader(static_cast<DWORD*>((**result_vs).GetBufferPointer()), &shader_vs);

    auto result_ps = compile_shader(desc.ps.source, desc.ps.entry_point, "ps_3_0");
    if (!result_ps.has_value()) {
        return std::unexpected{std::move(result_ps.error())};
    }

    IDirect3DPixelShader9* shader_ps = nullptr;
    device->CreatePixelShader(static_cast<DWORD*>((**result_ps).GetBufferPointer()), &shader_ps);

    IDirect3DVertexDeclaration9* layout = nullptr;
    if (FAILED(device->CreateVertexDeclaration(desc.layout.data(), &layout))) {
        return std::unexpected{"Failed to create vertex declaration"};
    }

    return Shaders{
        detail::Handle<IDirect3DVertexDeclaration9>{layout},
        detail::Handle<IDirect3DVertexShader9>{shader_vs},
        detail::Handle<IDirect3DPixelShader9>{shader_ps},
    };
}

void dx::Shaders::bind(IDirect3DDevice9* device) const {
    device->SetVertexDeclaration(m_layout.get());
    device->SetVertexShader(m_vs.get());
    device->SetPixelShader(m_ps.get());
}
