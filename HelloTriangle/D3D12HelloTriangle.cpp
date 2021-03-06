//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "stdafx.h"
#include "D3D12HelloTriangle.h"

#define USE_DXC

#ifdef USE_DXC
#include <locale>
#include <codecvt>
#include <string>
#include "dxc/dxcapi.use.h"
//#include "dxc/addref.h"
#endif

#ifdef USE_DXC
static dxc::DxcDllSupport       s_dxcSupport;
static IDxcCompiler *           s_dxcCompiler = nullptr;
static IDxcLibrary *            s_dxcLibrary = nullptr;
#endif

D3D12HelloTriangle::D3D12HelloTriangle(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    m_frameIndex(0),
    m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
    m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
    m_rtvDescriptorSize(0)
{
}

void D3D12HelloTriangle::OnInit()
{
#ifdef USE_DXC
    s_dxcSupport.Initialize( );

    HRESULT hr = E_FAIL;

    if( s_dxcSupport.IsEnabled( ) )
        hr = s_dxcSupport.CreateInstance( CLSID_DxcCompiler, &s_dxcCompiler );
    ThrowIfFailed( hr );
    if( SUCCEEDED( hr ) )
        hr = s_dxcSupport.CreateInstance( CLSID_DxcLibrary, &s_dxcLibrary );
    ThrowIfFailed( hr );    // Unable to create DirectX12 shader compiler - are 'dxcompiler.dll' and 'dxil.dll' files in place?
#endif

    LoadPipeline();
    LoadAssets();
}

// Load the rendering pipeline dependencies.
void D3D12HelloTriangle::LoadPipeline()
{
    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after device creation will invalidate the active device.
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();

            // Enable additional debug layers.
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    if (m_useWarpDevice)
    {
        ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

        ThrowIfFailed(D3D12CreateDevice(
            warpAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
            ));
    }
    else
    {
        ComPtr<IDXGIAdapter1> hardwareAdapter;
        GetHardwareAdapter(factory.Get(), &hardwareAdapter);

        ThrowIfFailed(D3D12CreateDevice(
            hardwareAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
            ));
    }

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
        Win32Application::GetHwnd(),
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain
        ));

    // This sample does not support fullscreen transitions.
    ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

    ThrowIfFailed(swapChain.As(&m_swapChain));
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Create descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    // Create frame resources.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

        // Create a RTV for each frame.
        for (UINT n = 0; n < FrameCount; n++)
        {
            ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
            m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
            rtvHandle.Offset(1, m_rtvDescriptorSize);
        }
    }

    ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
}

#if defined(USE_DXC)
HRESULT DXCCompileFromFile( _In_ LPCWSTR pFileName, CONST D3D_SHADER_MACRO* pDefines, _In_opt_ ID3DInclude* pInclude, _In_ LPCSTR pEntrypoint, 
                            _In_ LPCSTR pTarget, _In_ UINT Flags1, _In_ UINT Flags2, _Out_ ID3DBlob** ppCode, ID3DBlob** ppErrorMsgs )
{
    if( pDefines != nullptr )       // unsupported
        throw std::exception(); 
    if( pInclude != nullptr )       // unsupported
        throw std::exception( );
    if( ppErrorMsgs != nullptr )    // unsupported
        throw std::exception( );
    if( Flags2 != 0 )               // unsupported
        throw std::exception( );

    UINT codePage = 0;
    ComPtr<IDxcBlobEncoding> shaderFileBlob;
    ThrowIfFailed( s_dxcLibrary->CreateBlobFromFile( pFileName, &codePage, shaderFileBlob.GetAddressOf() ) );

    // convert flags to args
    std::vector<LPCWSTR> arguments;
    {
        // /Gec, /Ges Not implemented:
        //if(Flags1 & D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY) arguments.push_back(L"/Gec");
        if( Flags1 & D3DCOMPILE_ENABLE_STRICTNESS ) arguments.push_back( L"/Ges" );
        if( Flags1 & D3DCOMPILE_IEEE_STRICTNESS ) arguments.push_back( L"/Gis" );
        if( Flags1 & D3DCOMPILE_OPTIMIZATION_LEVEL2 )
        {
            switch( Flags1 & D3DCOMPILE_OPTIMIZATION_LEVEL2 )
            {
            case D3DCOMPILE_OPTIMIZATION_LEVEL0: arguments.push_back( L"/O0" ); break;
            case D3DCOMPILE_OPTIMIZATION_LEVEL2: arguments.push_back( L"/O2" ); break;
            case D3DCOMPILE_OPTIMIZATION_LEVEL3: arguments.push_back( L"/O3" ); break;
            }
        }
        if( Flags1 & D3DCOMPILE_WARNINGS_ARE_ERRORS )
            arguments.push_back( L"/WX" );
        // Currently, /Od turns off too many optimization passes, causing incorrect DXIL to be generated.
        // Re-enable once /Od is implemented properly:
        //if(Flags1 & D3DCOMPILE_SKIP_OPTIMIZATION) arguments.push_back(L"/Od");
        if( Flags1 & D3DCOMPILE_DEBUG )
        {
            arguments.push_back( L"/Zi" );
            arguments.push_back( L"-Qembed_debug" ); // this is for the "warning: no output provided for debug - embedding PDB in shader container.  Use -Qembed_debug to silence this warning."
        }
        if( Flags1 & D3DCOMPILE_PACK_MATRIX_ROW_MAJOR ) arguments.push_back( L"/Zpr" );
        if( Flags1 & D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR ) arguments.push_back( L"/Zpc" );
        if( Flags1 & D3DCOMPILE_AVOID_FLOW_CONTROL ) arguments.push_back( L"/Gfa" );
        if( Flags1 & D3DCOMPILE_PREFER_FLOW_CONTROL ) arguments.push_back( L"/Gfp" );
        // We don't implement this:
        //if(Flags1 & D3DCOMPILE_PARTIAL_PRECISION) arguments.push_back(L"/Gpp");
        if( Flags1 & D3DCOMPILE_RESOURCES_MAY_ALIAS ) arguments.push_back( L"/res_may_alias" );

    }

    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;

    std::wstring longEntryPoint = converter.from_bytes( pEntrypoint );
    std::wstring longShaderModel = converter.from_bytes( pTarget );

    // we've got to up the shader model - old ones no longer supported
    if( longShaderModel[3] < L'6' )
        longShaderModel[3] = L'6';

    ComPtr<IDxcOperationResult> operationResult;

    ThrowIfFailed( s_dxcCompiler->Compile( shaderFileBlob.Get( ), pFileName, longEntryPoint.c_str( ), longShaderModel.c_str( ), arguments.data( ), (UINT32)arguments.size( ), nullptr, 0, nullptr, operationResult.GetAddressOf( ) ) );

    HRESULT hr;
    if( operationResult != nullptr )
        operationResult->GetStatus( &hr );
    else
    {
        OutputDebugStringA( "operationResult == nullptr" );
        return E_FAIL;
    }

    if( SUCCEEDED( hr ) )
        return operationResult->GetResult( (IDxcBlob**)ppCode );
    else
    {
        std::string outErrorInfo;
        ComPtr<IDxcBlobEncoding> blobErrors;
        if( FAILED( operationResult->GetErrorBuffer( blobErrors.GetAddressOf() ) ) )
            { outErrorInfo = "Unknown shader compilation error"; assert( false ); }
            
        BOOL known = false; UINT32 codePage;
        if( blobErrors == nullptr || FAILED( blobErrors->GetEncoding( &known, &codePage ) ) || blobErrors->GetBufferSize() == 0 )
            { outErrorInfo = "Unknown shader compilation error"; assert( false ); }
        else
        {
            if( !known || codePage != CP_UTF8 )
            {
                outErrorInfo = "Unknown shader compilation error - unsupported error message encoding";
            }
            else
            {
                outErrorInfo = std::string( (char*)blobErrors->GetBufferPointer( ), blobErrors->GetBufferSize()-1 );
            }
        }
        OutputDebugStringA( outErrorInfo.c_str() );
        return hr;
    }
}

#endif

// Load the sample assets.
void D3D12HelloTriangle::LoadAssets()
{
#define TEST_COMPILE_IN_LOOP
//#define DISABLE_VALIDATION_BUT_COMPARE_OUTPUTS

    // loop a couple of times until we trigger the "Gradient operations are not affected by wave-sensitive data or control flow." error
#ifdef TEST_COMPILE_IN_LOOP
    {
        UINT codePage = 0;
        ComPtr<IDxcBlobEncoding> shaderFileBlob;
        ThrowIfFailed( s_dxcLibrary->CreateBlobFromFile( L"shaders.hlsl", &codePage, shaderFileBlob.GetAddressOf( ) ) );

        std::vector<LPCWSTR> arguments;
        arguments.push_back( L"/Zi" );
        arguments.push_back( L"-Qembed_debug" );

        ComPtr<IDxcOperationResult> operationResult;

#ifdef DISABLE_VALIDATION_BUT_COMPARE_OUTPUTS
        ComPtr<IDxcBlob> compareResult;
        arguments.push_back( L"-Vd" );          // disable validation
#endif

        for( int i = 0; i < 200; i++ )
        {
            OutputDebugStringA( ( "loop: " + std::to_string( i ) + " : " ).c_str( ) );
            ComPtr<IDxcOperationResult> operationResult;
            // ThrowIfFailed( s_dxcCompiler->Compile( shaderFileBlob.Get( ), L"shaders.hlsl", L"PSMain", L"ps_6_0", nullptr, 0, nullptr, 0, nullptr, operationResult.GetAddressOf( ) ) );
            ThrowIfFailed( s_dxcCompiler->Compile( shaderFileBlob.Get( ), L"shaders.hlsl", L"PSMain", L"ps_6_0", arguments.data( ), (UINT32)arguments.size( ), nullptr, 0, nullptr, operationResult.GetAddressOf( ) ) );
            HRESULT hr;
            ThrowIfFailed( operationResult->GetStatus( &hr ) );
            if( SUCCEEDED( hr ) )
            {
                OutputDebugStringA( "   all good\n" );

#ifdef DISABLE_VALIDATION_BUT_COMPARE_OUTPUTS
                ComPtr<IDxcBlob> localResult;
                ThrowIfFailed( operationResult->GetResult( localResult.GetAddressOf() ) );

                if( compareResult == nullptr )
                {
                    compareResult = localResult;
                    OutputDebugStringA( "   - compare result stored\n" );
                }
                else
                {
                    if( compareResult->GetBufferSize() == localResult->GetBufferSize() && 
                        memcmp( compareResult->GetBufferPointer(), localResult->GetBufferPointer(), localResult->GetBufferSize() ) == 0 )
                    {
                        OutputDebugStringA( "   - compare result identical, all good!\n" );
                    }
                    else
                    {
                        OutputDebugStringA( "   - ERROR: compare result different!\n" );
                        ThrowIfFailed( E_FAIL );
                    }

                }
#endif
            }
            else
            {
                OutputDebugStringA( "   ERROR:\n" );
                ComPtr<IDxcBlobEncoding> blobErrors;
                ThrowIfFailed( operationResult->GetErrorBuffer( blobErrors.GetAddressOf( ) ) );
                OutputDebugStringA( (char*)blobErrors->GetBufferPointer( ) );
            }
            ThrowIfFailed( hr );
        }
    }
#endif

    // Create an empty root signature.
    {
        CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
        ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
    }

    // Create the pipeline state, which includes compiling and loading shaders.
    {
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif

#if defined(USE_DXC)
        ThrowIfFailed( DXCCompileFromFile( GetAssetFullPath( L"shaders.hlsl" ).c_str( ), nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr ) );
        ThrowIfFailed( DXCCompileFromFile( GetAssetFullPath( L"shaders.hlsl" ).c_str( ), nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr ) );
#else
        ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr));
        ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr));
#endif

        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
        psoDesc.pRootSignature = m_rootSignature.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;
        ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
    }

    // Create the command list.
    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList)));

    // Command lists are created in the recording state, but there is nothing
    // to record yet. The main loop expects it to be closed, so close it now.
    ThrowIfFailed(m_commandList->Close());

    // Create the vertex buffer.
    {
        // Define the geometry for a triangle.
        Vertex triangleVertices[] =
        {
            { { 0.0f, 0.25f * m_aspectRatio, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
            { { 0.25f, -0.25f * m_aspectRatio, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
            { { -0.25f, -0.25f * m_aspectRatio, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } }
        };

        const UINT vertexBufferSize = sizeof(triangleVertices);

        // Note: using upload heaps to transfer static data like vert buffers is not 
        // recommended. Every time the GPU needs it, the upload heap will be marshalled 
        // over. Please read up on Default Heap usage. An upload heap is used here for 
        // code simplicity and because there are very few verts to actually transfer.
        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_vertexBuffer)));

        // Copy the triangle data to the vertex buffer.
        UINT8* pVertexDataBegin;
        CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
        memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
        m_vertexBuffer->Unmap(0, nullptr);

        // Initialize the vertex buffer view.
        m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
        m_vertexBufferView.StrideInBytes = sizeof(Vertex);
        m_vertexBufferView.SizeInBytes = vertexBufferSize;
    }

    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    {
        ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
        m_fenceValue = 1;

        // Create an event handle to use for frame synchronization.
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_fenceEvent == nullptr)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }

        // Wait for the command list to execute; we are reusing the same command 
        // list in our main loop but for now, we just want to wait for setup to 
        // complete before continuing.
        WaitForPreviousFrame();
    }
}

// Update frame-based values.
void D3D12HelloTriangle::OnUpdate()
{
}

// Render the scene.
void D3D12HelloTriangle::OnRender()
{
    // Record all the commands we need to render the scene into the command list.
    PopulateCommandList();

    // Execute the command list.
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present the frame.
    ThrowIfFailed(m_swapChain->Present(1, 0));

    WaitForPreviousFrame();
}

void D3D12HelloTriangle::OnDestroy()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForPreviousFrame();

    CloseHandle(m_fenceEvent);
}

void D3D12HelloTriangle::PopulateCommandList()
{
    // Command list allocators can only be reset when the associated 
    // command lists have finished execution on the GPU; apps should use 
    // fences to determine GPU execution progress.
    ThrowIfFailed(m_commandAllocator->Reset());

    // However, when ExecuteCommandList() is called on a particular command 
    // list, that command list can then be reset at any time and must be before 
    // re-recording.
    ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

    // Set necessary state.
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);

    // Indicate that the back buffer will be used as a render target.
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // Record commands.
    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_commandList->DrawInstanced(3, 1, 0, 0);

    // Indicate that the back buffer will now be used to present.
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    ThrowIfFailed(m_commandList->Close());
}

void D3D12HelloTriangle::WaitForPreviousFrame()
{
    // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
    // This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
    // sample illustrates how to use fences for efficient resource usage and to
    // maximize GPU utilization.

    // Signal and increment the fence value.
    const UINT64 fence = m_fenceValue;
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
    m_fenceValue++;

    // Wait until the previous frame is finished.
    if (m_fence->GetCompletedValue() < fence)
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}
