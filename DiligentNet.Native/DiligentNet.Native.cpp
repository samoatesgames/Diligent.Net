
#include "pch.h"
#include "DiligentNet.Native.h"
#include "framework.h"
#include <memory>
#include <iomanip>
#include <iostream>
#include <vector>
#include <array>

#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>
#include <crtdbg.h>

#ifndef PLATFORM_WIN32
#    define PLATFORM_WIN32 1
#endif

#ifndef ENGINE_DLL
#    define ENGINE_DLL 1
#endif

#ifndef D3D12_SUPPORTED
#    define D3D12_SUPPORTED 1
#endif

#include "RenderStateNotation/interface/RenderStateNotationLoader.h"
#include "AssetLoader/interface/GLTFLoader.hpp"
#include "PBR/interface/GLTF_PBR_Renderer.hpp"
#include "Common/interface/BasicMath.hpp"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "Graphics/GraphicsTools/interface/GraphicsUtilities.h"
#include "TextureLoader/interface/TextureUtilities.h"
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
#include "Graphics/GraphicsTools/interface/ShaderSourceFactoryUtils.hpp"

#include <Common/interface/RefCntAutoPtr.hpp>
#include "Graphics/GraphicsEngineD3D12/interface/EngineFactoryD3D12.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/SwapChain.h"

namespace Diligent
{
    namespace HLSL
    {
        #include "Shaders/Common/public/BasicStructures.fxh"
        #include "Shaders/PBR/public/PBR_Structures.fxh"
        #include "Shaders/PBR/private/RenderPBR_Structures.fxh"
    }
}

using namespace Diligent;

class DiligentNetNativeImpl
{
private:
    SwapChainDesc SCDesc;
    EngineD3D12CreateInfo EngineCI;
    IRenderDevice* m_pDevice;
    IDeviceContext* m_pImmediateContext;
    ISwapChain* m_pSwapChain;

    Uint32 m_CurrentFrameNumber = 0;

    // PBR Renderer
    GLTF_PBR_Renderer::RenderInfo m_RenderParams;

    float3 m_LightDirection = float3(0.5f, 0.6f, -0.2f);
    float4 m_LightColor = float4(1, 1, 1, 1);

    std::unique_ptr<GLTF_PBR_Renderer>    m_GLTFRenderer;
    std::unique_ptr<GLTF::Model>          m_Model;
    std::array<GLTF::ModelTransforms, 2>  m_Transforms; // [0] - current frame, [1] - previous frame
    float4x4                              m_ModelTransform;
    RefCntAutoPtr<IBuffer>                m_FrameAttribsCB;

    ITextureView* m_pCurrentEnvMapSRV = nullptr;

    GLTF_PBR_Renderer::ModelResourceBindings m_ModelResourceBindings;

    std::unique_ptr<HLSL::CameraAttribs[]> m_CameraAttribs; // [0] - current frame, [1] - previous frame

    void LoadModel(const char* Path);
    void CreateGLTFRenderer();

public:
    DiligentNetNativeImpl();

    void Initialize(void* hWnd);
    void Render();
    void Resize(unsigned int width, unsigned int  height);
};

DiligentNetNativeImpl::DiligentNetNativeImpl() :
    m_CameraAttribs{ std::make_unique<HLSL::CameraAttribs[]>(2) }
{
}

void DiligentNetNativeImpl::LoadModel(const char* Path)
{
    GLTF::ModelCreateInfo ModelCI;
    ModelCI.FileName = Path;

    m_Model = std::make_unique<GLTF::Model>(m_pDevice, m_pImmediateContext, ModelCI);
    m_ModelResourceBindings = m_GLTFRenderer->CreateResourceBindings(*m_Model, m_FrameAttribsCB);
    m_RenderParams.SceneIndex = m_Model->DefaultSceneId;

    m_Model->ComputeTransforms(m_RenderParams.SceneIndex, m_Transforms[0]);
    auto ModelAABB = m_Model->ComputeBoundingBox(m_RenderParams.SceneIndex, m_Transforms[0]);

    // Center and scale model
    float  MaxDim = 0;
    float3 ModelDim{ ModelAABB.Max - ModelAABB.Min };
    MaxDim = std::max(MaxDim, ModelDim.x);
    MaxDim = std::max(MaxDim, ModelDim.y);
    MaxDim = std::max(MaxDim, ModelDim.z);

    auto     SceneScale = (1.0f / std::max(MaxDim, 0.01f)) * 0.5f;
    auto     Translate = -ModelAABB.Min - 0.5f * ModelDim;
    float4x4 InvYAxis = float4x4::Identity();
    InvYAxis._22 = -1;

    m_ModelTransform = float4x4::Translation(Translate) * float4x4::Scale(SceneScale) * InvYAxis;
    m_Model->ComputeTransforms(m_RenderParams.SceneIndex, m_Transforms[0], m_ModelTransform);
    m_Transforms[1] = m_Transforms[0];
}

void DiligentNetNativeImpl::CreateGLTFRenderer()
{
    GLTF_PBR_Renderer::CreateInfo RendererCI;
    RendererCI.FrontCounterClockwise = true;

    RendererCI.NumRenderTargets = 1;
    RendererCI.RTVFormats[0] = m_pSwapChain->GetDesc().ColorBufferFormat;
    RendererCI.DSVFormat = m_pSwapChain->GetDesc().DepthBufferFormat;

    if (RendererCI.RTVFormats[0] == TEX_FORMAT_RGBA8_UNORM || RendererCI.RTVFormats[0] == TEX_FORMAT_BGRA8_UNORM)
        m_RenderParams.Flags |= GLTF_PBR_Renderer::PSO_FLAG_CONVERT_OUTPUT_TO_SRGB;

    m_GLTFRenderer = std::make_unique<GLTF_PBR_Renderer>(m_pDevice, nullptr, m_pImmediateContext, RendererCI);
}

void CreateUniformBuffer(IRenderDevice* pDevice,
    Uint64           Size,
    const Char* Name,
    IBuffer** ppBuffer,
    USAGE            Usage,
    BIND_FLAGS       BindFlags,
    CPU_ACCESS_FLAGS CPUAccessFlags,
    void* pInitialData)
{
    if (Usage == USAGE_DEFAULT || Usage == USAGE_IMMUTABLE)
        CPUAccessFlags = CPU_ACCESS_NONE;

    BufferDesc CBDesc;
    CBDesc.Name = Name;
    CBDesc.Size = Size;
    CBDesc.Usage = Usage;
    CBDesc.BindFlags = BindFlags;
    CBDesc.CPUAccessFlags = CPUAccessFlags;

    BufferData InitialData;
    if (pInitialData != nullptr)
    {
        InitialData.pData = pInitialData;
        InitialData.DataSize = Size;
    }
    pDevice->CreateBuffer(CBDesc, pInitialData != nullptr ? &InitialData : nullptr, ppBuffer);
}

void DiligentNetNativeImpl::Initialize(void* hWnd)
{
    auto GetEngineFactoryD3D12 = LoadGraphicsEngineD3D12();
    auto* pFactoryD3D12 = GetEngineFactoryD3D12();
    pFactoryD3D12->CreateDeviceAndContextsD3D12(EngineCI, &m_pDevice, &m_pImmediateContext);
    Win32NativeWindow Window{ (HWND)hWnd };
    pFactoryD3D12->CreateSwapChainD3D12(m_pDevice, m_pImmediateContext, SCDesc, FullScreenModeDesc{}, Window, &m_pSwapChain);

    CreateGLTFRenderer();
    CreateUniformBuffer(m_pDevice, m_GLTFRenderer->GetPRBFrameAttribsSize(), "PBR frame attribs buffer", &m_FrameAttribsCB);

    RefCntAutoPtr<ITexture> EnvironmentMap;
    CreateTextureFromFile("papermill.ktx", TextureLoadInfo{ "Environment map" }, m_pDevice, &EnvironmentMap);
    m_pCurrentEnvMapSRV = EnvironmentMap->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    m_GLTFRenderer->PrecomputeCubemaps(m_pImmediateContext, m_pCurrentEnvMapSRV);

    LoadModel("DamagedHelmet.gltf");
}

float4x4 CalculateProjectionMatrix(ISwapChain* SwapChain, IRenderDevice* Device, float FOV, float NearPlane, float FarPlane)
{
    const auto& SCDesc = SwapChain->GetDesc();

    float AspectRatio = static_cast<float>(SCDesc.Width) / static_cast<float>(SCDesc.Height);
    float XScale, YScale;
    if (SCDesc.PreTransform == SURFACE_TRANSFORM_ROTATE_90 ||
        SCDesc.PreTransform == SURFACE_TRANSFORM_ROTATE_270 ||
        SCDesc.PreTransform == SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90 ||
        SCDesc.PreTransform == SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270)
    {
        // When the screen is rotated, vertical FOV becomes horizontal FOV
        XScale = 1.f / std::tan(FOV / 2.f);
        // Aspect ratio is inversed
        YScale = XScale * AspectRatio;
    }
    else
    {
        YScale = 1.f / std::tan(FOV / 2.f);
        XScale = YScale / AspectRatio;
    }

    float4x4 Proj;
    Proj._11 = XScale;
    Proj._22 = YScale;
    Proj.SetNearFarClipPlanes(NearPlane, FarPlane, Device->GetDeviceInfo().NDC.MinZ == -1);
    return Proj;
}

void DiligentNetNativeImpl::Render()
{
    float4x4 CameraWorld = float4x4::RotationY(PI_F) * float4x4::RotationZ(PI_F) * float4x4::Translation(0, 0, 1);
    float4x4 CameraView = CameraWorld.Inverse();

    const auto CameraProj = CalculateProjectionMatrix(m_pSwapChain, m_pDevice, PI_F / 4.0f, 0.1f, 1000.0f);
    const auto CameraViewProj = CameraView * CameraProj;
    float3 CameraWorldPos = float3::MakeVector(CameraWorld[3]);

    auto& CurrCamAttribs = m_CameraAttribs[m_CurrentFrameNumber & 0x01];
    const auto& PrevCamAttribs = m_CameraAttribs[(m_CurrentFrameNumber + 1) & 0x01];
    auto& CurrTransforms = m_Transforms[m_CurrentFrameNumber & 0x01];
    auto& PrevTransforms = m_Transforms[(m_CurrentFrameNumber + 1) & 0x01];

    const auto& SCDesc = m_pSwapChain->GetDesc();
    CurrCamAttribs.f4ViewportSize = float4{ static_cast<float>(SCDesc.Width), static_cast<float>(SCDesc.Height), 1.f / SCDesc.Width, 1.f / SCDesc.Height };
    CurrCamAttribs.fHandness = CameraView.Determinant() > 0 ? 1.f : -1.f;
    CurrCamAttribs.mViewT = CameraView.Transpose();
    CurrCamAttribs.mProjT = CameraProj.Transpose();
    CurrCamAttribs.mViewProjT = CameraViewProj.Transpose();
    CurrCamAttribs.mViewInvT = CameraView.Inverse().Transpose();
    CurrCamAttribs.mProjInvT = CameraProj.Inverse().Transpose();
    CurrCamAttribs.mViewProjInvT = CameraViewProj.Inverse().Transpose();
    CurrCamAttribs.f4Position = float4(CameraWorldPos, 1);

    auto rotatedModel = float4x4::RotationY(m_CurrentFrameNumber * 0.005f) * m_ModelTransform;
    m_Model->ComputeTransforms(m_RenderParams.SceneIndex, CurrTransforms, rotatedModel);

    // Set render targets before issuing any draw command.
    // Note that Present() unbinds the back buffer if it is set as render target.
    auto* pRTV = m_pSwapChain->GetCurrentBackBufferRTV();
    auto* pDSV = m_pSwapChain->GetDepthBufferDSV();
    m_pImmediateContext->SetRenderTargets(1, &pRTV, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    const float ClearColor[] = { 0.032f, 0.032f, 0.032f, 1.0f };
    m_pImmediateContext->ClearRenderTarget(pRTV, ClearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_pImmediateContext->ClearDepthStencil(pDSV, CLEAR_DEPTH_FLAG, 1.f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    MapHelper<HLSL::PBRFrameAttribs> FrameAttribs{ m_pImmediateContext, m_FrameAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD };
    FrameAttribs->Camera = CurrCamAttribs;
    FrameAttribs->PrevCamera = PrevCamAttribs;
    FrameAttribs->PrevCamera.f4ExtraData[0] = float4{ 1,0,0,0 };

    auto* Lights = reinterpret_cast<HLSL::PBRLightAttribs*>(FrameAttribs + 1);
    Lights[0].Type = static_cast<int>(GLTF::Light::TYPE::DIRECTIONAL);
    Lights[0].Direction = m_LightDirection;
    Lights[0].Intensity = m_LightColor;

    auto& Renderer = FrameAttribs->Renderer;
    Renderer.OcclusionStrength = 1;
    Renderer.EmissionScale = 1;
    Renderer.AverageLogLum = 0.3f;
    Renderer.MiddleGray = 0.18f;
    Renderer.WhitePoint = 3.f;
    Renderer.IBLScale = 1;
    Renderer.PointSize = 1;
    Renderer.LightCount = 1;

    m_GLTFRenderer->SetInternalShaderParameters(Renderer);
    m_GLTFRenderer->Begin(m_pImmediateContext);
    m_GLTFRenderer->Render(m_pImmediateContext, *m_Model, CurrTransforms, &PrevTransforms, m_RenderParams, &m_ModelResourceBindings);

    m_pSwapChain->Present();

    m_CurrentFrameNumber++;
}

void DiligentNetNativeImpl::Resize(unsigned int width, unsigned int height)
{
    if (m_pSwapChain)
    {
        m_pSwapChain->Resize(width, height);
    }
}

// Wrappers for the implementation
DiligentNetNative::DiligentNetNative()
{
    m_impl = new DiligentNetNativeImpl(); // TODO: Delete me
}

void DiligentNetNative::Initialize(void* hWnd)
{
    m_impl->Initialize(hWnd);
}

void DiligentNetNative::Render()
{
    m_impl->Render();
}

void DiligentNetNative::Resize(unsigned int width, unsigned int  height)
{
    m_impl->Resize(width, height);
}
