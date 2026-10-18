#include "besktop/anim3d/renderer.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <array>
#include <stdexcept>
#include <cstring>
#include <chrono>

namespace besktop::anim3d
{
using Microsoft::WRL::ComPtr;
using namespace DirectX;
namespace
{
void Check(HRESULT hr, const char *what)
{
    if (FAILED(hr))
        throw std::runtime_error(std::string(what) + " HRESULT=" + std::to_string(static_cast<unsigned>(hr)));
}
struct Vertex
{
    XMFLOAT3 position, normal;
    XMFLOAT2 uv;
};
struct Instance
{
    XMFLOAT4X4 world;
    XMFLOAT4 color;
};
struct Constants
{
    XMFLOAT4X4 viewProjection;
    XMFLOAT4 settings;
};
struct Mesh
{
    ComPtr<ID3D11Buffer> vertices;
    UINT count = 0;
};
const char *Shader = R"(
cbuffer Scene : register(b0) { row_major float4x4 vp; float4 settings; }
struct VI { float3 p:POSITION; float3 n:NORMAL; float2 uv:TEXCOORD0; float4 w0:IWORLD0; float4 w1:IWORLD1; float4 w2:IWORLD2; float4 w3:IWORLD3; float4 c:ICOLOR; };
struct VO { float4 p:SV_POSITION; float3 n:NORMAL; float2 uv:TEXCOORD0; float4 c:COLOR; };
VO vs(VI i) {
  VO o; row_major float4x4 world=float4x4(i.w0,i.w1,i.w2,i.w3);
  o.p=mul(mul(float4(i.p,1),world),vp);
  float3x3 rotation=float3x3(normalize(i.w0.xyz),normalize(i.w1.xyz),normalize(i.w2.xyz));
  o.n=normalize(mul(i.n,rotation)); o.uv=i.uv; o.c=i.c; return o;
}
Texture2D image:register(t0); SamplerState linearSampler:register(s0);
float4 ps(VO i):SV_TARGET {
  float4 c=i.c;
  if(settings.x>0.5) { c*=image.Sample(linearSampler,i.uv); clip(c.a-0.08); return c; }
  float diffuse=saturate(dot(normalize(i.n),normalize(float3(-.5,.9,.6))));
  return float4(c.rgb*(.42+.58*diffuse),c.a);
}
)";
XMMATRIX World(Vec3 p, Quat q, Vec3 scale)
{
    return XMMatrixScaling(scale.x, scale.y, scale.z) * XMMatrixRotationQuaternion(XMVectorSet(q.x, q.y, q.z, q.w)) *
           XMMatrixTranslation(p.x, p.y, p.z);
}
Instance MakeInstance(Vec3 p, Quat q, Vec3 scale, Vec3 color)
{
    Instance result;
    XMStoreFloat4x4(&result.world, World(p, q, scale));
    result.color = {color.x, color.y, color.z, 1};
    return result;
}
std::vector<Vertex> Sphere()
{
    std::vector<Vertex> result;
    auto point = [](int lat, int lon) {
        const float a = Pi * lat / 8, b = 2 * Pi * lon / 12;
        return XMFLOAT3{std::sin(a) * std::cos(b), std::cos(a), std::sin(a) * std::sin(b)};
    };
    for (int lat = 0; lat < 8; ++lat)
        for (int lon = 0; lon < 12; ++lon)
        {
            const XMFLOAT3 p[] = {point(lat, lon), point(lat + 1, lon), point(lat + 1, lon + 1), point(lat, lon + 1)};
            for (int i : {0, 1, 2, 0, 2, 3})
                result.push_back({p[i], p[i], {0, 0}});
        }
    return result;
}
std::vector<Vertex> Cylinder()
{
    std::vector<Vertex> result;
    for (int i = 0; i < 12; ++i)
    {
        const float a = 2 * Pi * i / 12, b = 2 * Pi * (i + 1) / 12;
        const XMFLOAT3 p[] = {{std::cos(a), -.5f, std::sin(a)},
                              {std::cos(a), .5f, std::sin(a)},
                              {std::cos(b), .5f, std::sin(b)},
                              {std::cos(b), -.5f, std::sin(b)}};
        for (int j : {0, 1, 2, 0, 2, 3})
            result.push_back({p[j], {p[j].x, 0, p[j].z}, {0, 0}});
    }
    return result;
}
std::vector<Vertex> Quad()
{
    return {{{-.5f, .5f, 0}, {0, 0, 1}, {0, 0}}, {{.5f, .5f, 0}, {0, 0, 1}, {1, 0}},
            {{.5f, -.5f, 0}, {0, 0, 1}, {1, 1}}, {{-.5f, .5f, 0}, {0, 0, 1}, {0, 0}},
            {{.5f, -.5f, 0}, {0, 0, 1}, {1, 1}}, {{-.5f, -.5f, 0}, {0, 0, 1}, {0, 1}}};
}
} // namespace
struct Renderer::Impl
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swap;
    ComPtr<ID3D11Texture2D> target, depth;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> constants, instances, singleInstance;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> depthOn, depthOff;
    std::vector<ComPtr<ID3D11ShaderResourceView>> textures;
    Mesh sphere, cylinder, quad;
    int width = 0, height = 0;
    std::string adapter;
    static constexpr size_t MaxInstances = 32768;
    Mesh Upload(const std::vector<Vertex> &data)
    {
        Mesh mesh;
        mesh.count = static_cast<UINT>(data.size());
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(sizeof(Vertex) * data.size());
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{data.data(), 0, 0};
        Check(device->CreateBuffer(&desc, &initial, &mesh.vertices), "Create geometry");
        return mesh;
    }
    void Batch(const Mesh &mesh, const std::vector<Instance> &data, int texture = -1)
    {
        if (data.empty())
            return;
        ID3D11ShaderResourceView *srv = texture >= 0 ? textures.at(texture).Get() : nullptr;
        context->PSSetShaderResources(0, 1, &srv);
        UINT strides[] = {sizeof(Vertex), sizeof(Instance)}, offsets[] = {0, 0};
        ID3D11Buffer *buffers[] = {mesh.vertices.Get(), data.size() == 1 ? singleInstance.Get() : instances.Get()};
        context->IASetVertexBuffers(0, 2, buffers, strides, offsets);
        if (data.size() == 1)
        {
            // A per-icon WRITE_DISCARD of the crowd buffer made the driver rename megabytes per quad.
            context->UpdateSubresource(singleInstance.Get(), 0, nullptr, data.data(), 0, 0);
            context->DrawInstanced(mesh.count, 1, 0, 0);
            return;
        }
        for (size_t start = 0; start < data.size(); start += MaxInstances)
        {
            const size_t count = std::min(MaxInstances, data.size() - start);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            Check(context->Map(instances.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map instances");
            std::memcpy(mapped.pData, data.data() + start, count * sizeof(Instance));
            context->Unmap(instances.Get(), 0);
            context->DrawInstanced(mesh.count, static_cast<UINT>(count), 0, 0);
        }
    }
    void SetConstants(const XMMATRIX &vp, bool textured)
    {
        Constants values;
        XMStoreFloat4x4(&values.viewProjection, vp);
        values.settings = {textured ? 1.f : 0.f, 0, 0, 0};
        context->UpdateSubresource(constants.Get(), 0, nullptr, &values, 0, 0);
    }
};
Renderer::Renderer() : impl_(std::make_unique<Impl>())
{
}
Renderer::~Renderer() = default;
void Renderer::Initialize(HWND window, int width, int height, bool warp)
{
    auto &s = *impl_;
    DXGI_SWAP_CHAIN_DESC swap{};
    swap.BufferCount = 2;
    swap.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap.OutputWindow = window;
    swap.SampleDesc.Count = 1;
    swap.Windowed = TRUE;
    swap.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL level;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                               D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 1, D3D11_SDK_VERSION, &swap,
                                               &s.swap, &s.device, &level, &s.context);
    if (FAILED(hr) && !warp)
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                           levels, 1, D3D11_SDK_VERSION, &swap, &s.swap, &s.device, &level, &s.context);
    Check(hr, "Create D3D11 device");
    ComPtr<IDXGIDevice> dxgi;
    Check(s.device.As(&dxgi), "Query DXGI");
    ComPtr<IDXGIAdapter> adapter;
    Check(dxgi->GetAdapter(&adapter), "Query adapter");
    DXGI_ADAPTER_DESC description{};
    adapter->GetDesc(&description);
    const int nameLength = WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, nullptr, 0, nullptr, nullptr);
    std::vector<char> adapterName(nameLength);
    WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, adapterName.data(), nameLength, nullptr, nullptr);
    s.adapter = adapterName.data();
    ComPtr<ID3DBlob> vertex, pixel, error;
    auto compile = [&](const char *entry, const char *profile, ComPtr<ID3DBlob> &result) {
        HRESULT status = D3DCompile(Shader, std::strlen(Shader), "Besktop3D", nullptr, nullptr, entry, profile,
                                    D3DCOMPILE_ENABLE_STRICTNESS, 0, &result, &error);
        if (FAILED(status))
            throw std::runtime_error(error ? static_cast<const char *>(error->GetBufferPointer())
                                           : "Shader compilation failed");
    };
    compile("vs", "vs_4_0", vertex);
    compile("ps", "ps_4_0", pixel);
    Check(s.device->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(), nullptr, &s.vs),
          "Vertex shader");
    Check(s.device->CreatePixelShader(pixel->GetBufferPointer(), pixel->GetBufferSize(), nullptr, &s.ps),
          "Pixel shader");
    const D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"IWORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"IWORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"IWORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"IWORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"ICOLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64, D3D11_INPUT_PER_INSTANCE_DATA, 1}};
    Check(s.device->CreateInputLayout(elements, 8, vertex->GetBufferPointer(), vertex->GetBufferSize(), &s.layout),
          "Input layout");
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(Constants);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    Check(s.device->CreateBuffer(&desc, nullptr, &s.constants), "Constants");
    desc.ByteWidth = static_cast<UINT>(Impl::MaxInstances * sizeof(Instance));
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    Check(s.device->CreateBuffer(&desc, nullptr, &s.instances), "Instances");
    desc.ByteWidth = sizeof(Instance);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.CPUAccessFlags = 0;
    Check(s.device->CreateBuffer(&desc, nullptr, &s.singleInstance), "Single icon instance");
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    Check(s.device->CreateSamplerState(&sampler, &s.sampler), "Sampler");
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    Check(s.device->CreateRasterizerState(&raster, &s.raster), "Rasterizer");
    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    Check(s.device->CreateDepthStencilState(&depth, &s.depthOn), "Depth state");
    depth.DepthEnable = FALSE;
    Check(s.device->CreateDepthStencilState(&depth, &s.depthOff), "Background state");
    s.sphere = s.Upload(Sphere());
    s.cylinder = s.Upload(Cylinder());
    s.quad = s.Upload(Quad());
    Resize(width, height);
}
void Renderer::Resize(int width, int height)
{
    if (width < 1 || height < 1)
        return;
    auto &s = *impl_;
    if (!s.swap)
        return;
    s.context->OMSetRenderTargets(0, nullptr, nullptr);
    s.rtv.Reset();
    s.target.Reset();
    s.dsv.Reset();
    s.depth.Reset();
    Check(s.swap->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0), "Resize buffers");
    s.width = width;
    s.height = height;
    Check(s.swap->GetBuffer(0, IID_PPV_ARGS(&s.target)), "Back buffer");
    Check(s.device->CreateRenderTargetView(s.target.Get(), nullptr, &s.rtv), "Render target");
    D3D11_TEXTURE2D_DESC depth{};
    depth.Width = width;
    depth.Height = height;
    depth.MipLevels = depth.ArraySize = 1;
    depth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depth.SampleDesc.Count = 1;
    depth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    Check(s.device->CreateTexture2D(&depth, nullptr, &s.depth), "Depth texture");
    Check(s.device->CreateDepthStencilView(s.depth.Get(), nullptr, &s.dsv), "Depth view");
}
int Renderer::AddTexture(int width, int height, const unsigned char *bgra)
{
    if (width < 1 || height < 1 || width > 8192 || height > 8192 || !bgra)
        throw std::runtime_error("Invalid icon texture");
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{bgra, static_cast<UINT>(width * 4), 0};
    ComPtr<ID3D11Texture2D> texture;
    Check(impl_->device->CreateTexture2D(&desc, &data, &texture), "Icon texture");
    ComPtr<ID3D11ShaderResourceView> view;
    Check(impl_->device->CreateShaderResourceView(texture.Get(), nullptr, &view), "Icon view");
    impl_->textures.push_back(view);
    return static_cast<int>(impl_->textures.size() - 1);
}
void Renderer::SetOverlay(int texture)
{
    if (overlay_ >= 0 && overlay_ != texture)
        impl_->textures.at(overlay_).Reset();
    overlay_ = texture;
}
void Renderer::Draw(const Library &library, const std::vector<RenderActor> &actors, const RenderOptions &options)
{
    auto &s = *impl_;
    const float clear[] = {.025f, .043f, .069f, 1};
    s.context->ClearRenderTargetView(s.rtv.Get(), clear);
    s.context->ClearDepthStencilView(s.dsv.Get(), D3D11_CLEAR_DEPTH, 1, 0);
    ID3D11RenderTargetView *rtv = s.rtv.Get();
    s.context->OMSetRenderTargets(1, &rtv, s.dsv.Get());
    D3D11_VIEWPORT viewport{0, 0, static_cast<float>(s.width), static_cast<float>(s.height), 0, 1};
    s.context->RSSetViewports(1, &viewport);
    s.context->RSSetState(s.raster.Get());
    s.context->IASetInputLayout(s.layout.Get());
    s.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    s.context->VSSetShader(s.vs.Get(), nullptr, 0);
    s.context->PSSetShader(s.ps.Get(), nullptr, 0);
    ID3D11Buffer *cb = s.constants.Get();
    s.context->VSSetConstantBuffers(0, 1, &cb);
    s.context->PSSetConstantBuffers(0, 1, &cb);
    ID3D11SamplerState *sampler = s.sampler.Get();
    s.context->PSSetSamplers(0, 1, &sampler);
    if (background_ >= 0)
    {
        s.context->OMSetDepthStencilState(s.depthOff.Get(), 0);
        s.SetConstants(XMMatrixIdentity(), true);
        s.Batch(s.quad, {MakeInstance({0, 0, 0}, {}, {2, 2, 1}, {1, 1, 1})}, background_);
    }
    s.context->OMSetDepthStencilState(s.depthOn.Get(), 0);
    const auto &c = options.camera;
    const Vec3 eye =
        c.target +
        Vec3{std::sin(c.yaw) * std::cos(c.pitch), std::sin(c.pitch), std::cos(c.yaw) * std::cos(c.pitch)} * c.distance;
    const XMMATRIX view = XMMatrixLookAtRH(XMVectorSet(eye.x, eye.y, eye.z, 1),
                                           XMVectorSet(c.target.x, c.target.y, c.target.z, 1), XMVectorSet(0, 1, 0, 0));
    const float aspect = static_cast<float>(s.width) / s.height;
    const XMMATRIX projection = c.orthographic
                                    ? XMMatrixOrthographicRH(c.orthoHeight * aspect, c.orthoHeight, .01f, 300.f)
                                    : XMMatrixPerspectiveFovRH(Pi / 4, aspect, .025f, 300.f);
    const XMMATRIX vp = view * projection;
    s.SetConstants(vp, false);
    std::vector<Instance> spheres, cylinders;
    auto sphere = [&](Vec3 p, float radius, Vec3 color) {
        spheres.push_back(MakeInstance(p, {}, {radius, radius, radius}, color));
    };
    auto line = [&](Vec3 a, Vec3 b, float radius, Vec3 color) {
        const float length = Length(b - a);
        if (length < .0001f)
            return;
        cylinders.push_back(MakeInstance((a + b) * .5f, FromTo({0, 1, 0}, b - a), {radius, length, radius}, color));
    };
    if (options.grid)
        for (int i = -24; i <= 24; ++i)
        {
            const float gridX = std::floor(c.target.x * 4) * .25f, gridZ = std::floor(c.target.z * 4) * .25f;
            const Vec3 col = i == 0 ? Vec3{.17f, .34f, .45f} : Vec3{.07f, .13f, .18f};
            line({gridX + i * .25f, 0, gridZ - 6}, {gridX + i * .25f, 0, gridZ + 6}, .0018f, col);
            line({gridX - 6, 0, gridZ + i * .25f}, {gridX + 6, 0, gridZ + i * .25f}, .0018f, col);
        }
    for (const auto &actor : actors)
    {
        if (!actor.pose)
            continue;
        const auto &p = *actor.pose;
        const float scale = actor.bodyScale;
        const auto &body = library.Presentation();
        auto ellipsoid = [&](const BodyEllipsoid &shape, Vec3 color) {
            spheres.push_back(MakeInstance(shape.transform.position, shape.transform.rotation, shape.radii, color));
        };
        if (!options.skeleton)
        {
            for (const auto &shape : TorsoShapes(library, p, scale))
                ellipsoid(shape, actor.color);
            for (const char *side : {"_l", "_r"})
            {
                ellipsoid(FootShape(library, p, library.Joint(std::string("foot") + side), scale), actor.color);
                ellipsoid(PalmShape(library, p, library.Joint(std::string("hand") + side), scale), {.55f, .79f, .9f});
            }
        }
        for (size_t j = 1; j < p.world.size(); ++j)
        {
            const int parent = library.Parents()[j];
            const auto &name = library.Names()[j];
            const bool head = name == "head";
            const bool torso = name == "spine" || name == "chest";
            const bool helper =
                name == "pelvis" || name == "spine_low" || name == "neck_base" || name.starts_with("clavicle");
            const bool toe = name.starts_with("toe"), hip = name.starts_with("thigh");
            const bool smallJoint = name.starts_with("hand") || name.starts_with("foot");
            const float radius =
                options.skeleton ? .018f : (head ? body.headRadius : body.jointRadius * (smallJoint ? .7f : 1.f));
            // Icon is an appearance attached to the head; it never alters the underlying skeleton.
            if (!(head && actor.icon >= 0) && (options.skeleton || (!helper && !torso && !toe)))
                sphere(p.world[j].position, radius * scale, head ? actor.color : Vec3{.75f, .88f, .94f});
            if (parent > 0 && (options.skeleton || (!torso && !toe && !hip)))
                line(p.world[parent].position, p.world[j].position,
                     (options.skeleton ? .007f
                                       : (name == "spine" || name == "chest" ? body.torsoRadius : body.limbRadius)) *
                         scale,
                     actor.color);
        }
        sphere(p.world[0].position, .025f * scale, {1, .6f, .16f});
    }
    if (options.contacts)
        for (Vec3 point : options.contactPoints)
            sphere(point, .022f, {.3f, 1, .55f});
    for (size_t i = 1; i < options.rootTrail.size(); ++i)
        line(options.rootTrail[i - 1], options.rootTrail[i], .006f, {1, .58f, .16f});
    s.Batch(s.cylinder, cylinders);
    s.Batch(s.sphere, spheres);
    s.SetConstants(vp, true);
    for (const auto &actor : actors)
        if (actor.pose && actor.icon >= 0)
        {
            const Vec3 head = actor.pose->world[library.Joint("head")].position;
            // Camera-facing texture preserves legibility; shoulder/hip motion remains fully three-dimensional.
            const Quat facing = AxisAngle({0, 1, 0}, c.yaw) * AxisAngle({1, 0, 0}, -c.pitch);
            const float size = actor.iconWidth * actor.bodyScale;
            s.Batch(s.quad, {MakeInstance(head, facing, {size, size, 1}, {1, 1, 1})}, actor.icon);
        }
    if (overlay_ >= 0)
    {
        s.context->OMSetDepthStencilState(s.depthOff.Get(), 0);
        s.SetConstants(XMMatrixIdentity(), true);
        s.Batch(s.quad, {MakeInstance({0, 0, 0}, {}, {2, 2, 1}, {1, 1, 1})}, overlay_);
    }
}
void Renderer::Present(bool vsync)
{
    Check(impl_->swap->Present(vsync ? 1 : 0, 0), "Present");
}
void Renderer::WaitForGpu()
{
    D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT, 0};
    ComPtr<ID3D11Query> query;
    Check(impl_->device->CreateQuery(&desc, &query), "GPU fence query");
    impl_->context->End(query.Get());
    impl_->context->Flush();
    const auto started = std::chrono::steady_clock::now();
    BOOL completed = FALSE;
    HRESULT result;
    while ((result = impl_->context->GetData(query.Get(), &completed, sizeof(completed), 0)) == S_FALSE)
    {
        if (std::chrono::steady_clock::now() - started > std::chrono::seconds(5))
            throw std::runtime_error("GPU completion timeout");
        Sleep(0);
    }
    Check(result, "GPU completion");
}
const std::string &Renderer::Adapter() const
{
    return impl_->adapter;
}
void Renderer::SavePng(const std::filesystem::path &path)
{
    auto &s = *impl_;
    D3D11_TEXTURE2D_DESC desc{};
    s.target->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    Check(s.device->CreateTexture2D(&desc, nullptr, &staging), "Readback texture");
    s.context->CopyResource(staging.Get(), s.target.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Check(s.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Readback map");
    std::vector<unsigned char> pixels(static_cast<size_t>(s.width) * s.height * 4);
    for (int y = 0; y < s.height; ++y)
    {
        const auto *row = static_cast<const unsigned char *>(mapped.pData) + y * mapped.RowPitch;
        auto *out = pixels.data() + static_cast<size_t>(y) * s.width * 4;
        for (int x = 0; x < s.width; ++x)
        {
            out[x * 4] = row[x * 4 + 2];
            out[x * 4 + 1] = row[x * 4 + 1];
            out[x * 4 + 2] = row[x * 4];
            out[x * 4 + 3] = 255;
        }
    }
    s.context->Unmap(staging.Get(), 0);
    ComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)),
          "WIC factory");
    ComPtr<IWICStream> stream;
    Check(factory->CreateStream(&stream), "WIC stream");
    Check(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE), "PNG output");
    ComPtr<IWICBitmapEncoder> encoder;
    Check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "PNG encoder");
    Check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "PNG initialize");
    ComPtr<IWICBitmapFrameEncode> frame;
    Check(encoder->CreateNewFrame(&frame, nullptr), "PNG frame");
    Check(frame->Initialize(nullptr), "PNG frame init");
    Check(frame->SetSize(s.width, s.height), "PNG size");
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    Check(frame->SetPixelFormat(&format), "PNG pixel format");
    Check(frame->WritePixels(s.height, s.width * 4, static_cast<UINT>(pixels.size()), pixels.data()), "PNG pixels");
    Check(frame->Commit(), "PNG frame commit");
    Check(encoder->Commit(), "PNG commit");
}
} // namespace besktop::anim3d
