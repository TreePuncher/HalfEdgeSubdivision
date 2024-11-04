#include "TestComponent.hpp"
#include "HalfEdgeMesh.hpp"

#include <Application.hpp>
#include <atomic>
#include <CBT.hpp>
#include <ModifiableShape.hpp>
#include <MemoryUtilities.hpp>
#include <print>
#include <ranges>
#include <stb_image.h>
#include <TextureUtilities.hpp>
#include <Transforms.hpp>
#include <TriggerComponent.hpp>
#include <Win32Graphics.hpp>
#include <DepthBuffer.hpp>


/************************************************************************************************/


template<typename TY> requires std::is_trivial_v<TY>
struct AtomicVector
{
	AtomicVector(FlexKit::iAllocator* IN_allocator) : allocator{ IN_allocator } {}

	void push_back(const TY& v)
	{
		const auto idx = used.fetch_add(1, std::memory_order_acq_rel);

		if (idx >= max)
		{
			std::unique_lock ul{ m };
			if (idx >= max)
			{
				size_t newMax = max == 0 ? 16 : max * 2;
				auto new_ptr = allocator->malloc(sizeof(TY) * max);

				std::copy(elements, elements + max, new_ptr);
				std::destroy_n(elements, max);

				allocator->free(*elements);

				elements	= new_ptr;
				max			= newMax;
			}
		}

		std::construct_at<TY>(elements + idx, v);
	}

	void Clear()
	{
		used = 0;
	}

	TY* begin() { return elements; }
	TY* end()	{ return elements; }

	TY*						elements			= nullptr;
	size_t					max					= 0;
	FlexKit::iAllocator*	allocator			= nullptr;
	std::atomic_uint		used				= 0;
	std::mutex				m;

	size_t	size() { return used; }
};


/************************************************************************************************/


constexpr FlexKit::PSOHandle AdaptiveTerrainUpdateArgs	= FlexKit::PSOHandle(GetTypeGUID(AdaptiveTerrainUpdateArgs));
constexpr FlexKit::PSOHandle AdaptiveTerrainUpdate		= FlexKit::PSOHandle(GetTypeGUID(AdaptiveTerrainUpdate));
constexpr FlexKit::PSOHandle AdaptiveTerrainDrawArgs	= FlexKit::PSOHandle(GetTypeGUID(AdaptiveTerrainDrawArgs));

void RegisterAdaptiveUpdateCBT(FlexKit::RenderSystem& renderSystem)
{
	renderSystem.RegisterPSOLoader(
		AdaptiveTerrainUpdateArgs,
		[](FlexKit::RenderSystem* renderSystem, FlexKit::iAllocator& tempAllocator) 
		{
			return FlexKit::PipelineBuilder{ tempAllocator }.
				AddComputeShader("AdaptiveUpdateGetArgs", R"(assets\shaders\CBT\CBT_GetArguments.hlsl)", { .hlsl2021 = true }).
				Build(*renderSystem);
		});

	renderSystem.RegisterPSOLoader(
		AdaptiveTerrainUpdate,
		[](FlexKit::RenderSystem* renderSystem, FlexKit::iAllocator& tempAllocator) 
		{
			return FlexKit::PipelineBuilder{ tempAllocator }.
				AddComputeShader("UpdateAdaptiveTerrain", R"(assets\shaders\CBT\CBT_TerrainAdapt.hlsl)", { .hlsl2021 = true }).
				Build(*renderSystem);
		});

	renderSystem.RegisterPSOLoader(
		AdaptiveTerrainDrawArgs,
		[](FlexKit::RenderSystem* renderSystem, FlexKit::iAllocator& tempAllocator)
		{
			return FlexKit::PipelineBuilder{ tempAllocator }.
				AddComputeShader("AdaptiveDrawGetArgs", R"(assets\shaders\CBT\CBT_GetArguments.hlsl)", { .hlsl2021 = true }).
				Build(*renderSystem);
		});

	renderSystem.QueuePSOLoad(AdaptiveTerrainUpdateArgs);
	renderSystem.QueuePSOLoad(AdaptiveTerrainDrawArgs);
	renderSystem.QueuePSOLoad(AdaptiveTerrainUpdate);
}


uint32_t GetBitOffset(uint32_t idx, uint32_t maxDepth)
{
	const uint32_t d_k		= FlexKit::FindMSB(idx);
	const uint32_t N_d_k	= maxDepth - d_k + 1;
	const uint32_t X_k		= FlexKit::ipow(2, d_k + 1) + idx * N_d_k;

	return X_k;
}

struct CBTTerrainState : FlexKit::FrameworkState
{
	CBTTerrainState(FlexKit::GameFramework& in_framework) :
		FlexKit::FrameworkState	{ in_framework },
		testComponent			{ in_framework.core.GetBlockMemory() },
		complexComponent		{ in_framework.core.GetBlockMemory() },
		tree					{ in_framework.GetRenderSystem(), in_framework.core.GetBlockMemory() },
		runOnce					{ in_framework.core.GetBlockMemory() },
		poolAllocator			{ in_framework.GetRenderSystem(), 64 * MEGABYTE, 
									FlexKit::DefaultBlockSize, FlexKit::DeviceHeapFlags::UAVBuffer, 
									in_framework.core.GetBlockMemory() },
		cameras					{ in_framework.core.GetBlockMemory() },
		triggers				{ in_framework.core.GetBlockMemory(), in_framework.core.GetBlockMemory() },
		depthBuffer				{ in_framework.GetRenderSystem(), { 1920, 1080 } }
	{
		using namespace FlexKit;

		auto& renderSystem = framework.GetRenderSystem();
		RegisterAdaptiveUpdateCBT(renderSystem);

		indirectDispatchLayout = renderSystem.CreateIndirectLayout(
			{
				IndirectDrawDescription{ 
					IndirectLayoutEntryType::ILE_RootDescriptorUINT, 
					IndirectDrawDescription::Constant{ .rootParameterIdx = 3, .destinationOffset = 0, .numValues = 1 } },
				IndirectDrawDescription{ IndirectLayoutEntryType::ILE_DispatchCall },
			}, 
			in_framework.core.GetBlockMemory(), std::get<1>(renderSystem.GetPSOAndRootSignature(AdaptiveTerrainUpdate, in_framework.core.GetTempMemory())));

		indirectDrawLayout = renderSystem.CreateIndirectLayout(
			{
				IndirectDrawDescription{ IndirectLayoutEntryType::ILE_DrawCall },
			},
			in_framework.core.GetBlockMemory());


		renderWindow = CreateWin32RenderWindow(framework.GetRenderSystem(),
			{
				.height = 1080,
				.width	= 1920,
			});

		FlexKit::EventNotifier<>::Subscriber sub;
		sub.Notify	= &FlexKit::EventsWrapper;
		sub._ptr	= &framework;
		renderWindow->Handler.Subscribe(sub);
		renderWindow->SetWindowTitle("HelloWorld");

		vertexBuffer = renderSystem.CreateVertexBuffer(512, false);

		renderSystem.RegisterPSOLoader(FlexKit::DRAW_PSO, FlexKit::CreateDrawTriStatePSO);


		const uint32_t depth = 18;
		tree.Initialize({ .maxDepth = depth });

		tree.SetBit(0, true);
		tree.SetBit(1 << (depth - 1), true);

		auto heapBitIdx1 = tree.HeapToBitIndex(1);
		auto heapBitIdx2 = tree.HeapToBitIndex(2);
		auto heapBitIdx3 = tree.HeapToBitIndex(3);
		auto heapBitIdx4 = tree.HeapToBitIndex(4);
		auto heapBitIdx5 = tree.HeapToBitIndex(5);
		auto heapBitIdx6 = tree.HeapToBitIndex(6);
		auto heapBitIdx7 = tree.HeapToBitIndex(7);

		auto start = GetBitOffset(FlexKit::ipow(2, 4), 4);
		auto temp1 = GetBitOffset(1, 4);
		auto temp2 = GetBitOffset(2, 4);
		auto temp3 = GetBitOffset(3, 4);
		auto temp4 = GetBitOffset(4, 4);

		runOnce.push_back([&](FlexKit::FrameGraph& frameGraph)
			{
				tree.Upload(frameGraph);
				tree.SumReduction_GPU(frameGraph);
			});


		ModifiableShape shape{ framework.core.GetBlockMemory() };

		const uint32_t face0[] = {
			shape.AddVertex({   0.0f, 0.0f, -0.9f }),
			shape.AddVertex({  -0.9f, 0.0f,  0.0f }),
			shape.AddVertex({   0.0f, 0.0f,  0.9f }) 
		};
		
		const uint32_t face1[] = {
			face0[0], face0[2],
			shape.AddVertex({  0.9f, 0.0f,  0.9f }),
			shape.AddVertex({  0.9f, 0.0f, -0.9f })
		};

		//const uint32_t face2[] = {
		//	shape.AddVertex({ -0.9f,  0.9f, 0 }),
		//	shape.AddVertex({  0.9f,  0.9f, 0 }),
		//	shape.AddVertex({  0.9f, -0.9f, 0 }),
		//	shape.AddVertex({ -0.9f, -0.9f, 0 })
		//};

		shape.AddPolygon(face0, face0 + 3);
		shape.AddPolygon(face1, face1 + 4);
		//shape.AddPolygon(face2, face2 + 4);

		HEMesh = std::make_unique<HalfEdgeMesh>(
							shape,
							framework.GetRenderSystem(), 
							framework.core.GetBlockMemory());

		runOnce.push_back(
			[&](FlexKit::FrameGraph& frameGraph) 
			{
				HEMesh->BuildSubDivLevel(frameGraph);
			});

		auto& cameraNode	= camera.AddView<SceneNodeView>();
		auto& cameraView	= camera.AddView<CameraView>();

		cameraNode.Pitch(FlexKit::pi / -5);
		cameraNode.TranslateWorld({  0, 2.5, 15 });
		cameraView.SetCameraNode(cameraNode);
		cameraView.SetCameraFOV(FlexKit::pi / 3);
		cameraView.SetCameraAspectRatio(1080.0f / 1920.0f);

		activeCamera = cameraView;


		if(false)
		{
			int width, height, channels;
			auto heightValues = stbi_loadf("assets\\HeightMap.png", &width, &height, &channels, 1);

			if (!heightValues)
				throw std::runtime_error("Failed to load Height Map!");

			const uint2 wh{ width, height };
			size_t rowPitch		= FlexKit::AlignedSize(width * 4);
			size_t bufferSize	= rowPitch * height;

			TextureBuffer converted				{ wh, 4, bufferSize, FlexKit::SystemAllocator };
			TextureBuffer buffer				{ wh, (std::byte*)heightValues, 4 };
			TextureBufferView<float> inputView	{ buffer };
			TextureBufferView<float> outputView	{ converted, rowPitch };

			for (size_t y_itr = 0; y_itr < wh[1]; y_itr++)
			{
				for (size_t x_itr = 0; x_itr < wh[0]; x_itr++)
				{
					FlexKit::uint2 px	= { x_itr, y_itr };
					outputView[px]		= inputView[px];
				}
			}

			free(heightValues);

		
			heightMap = LoadTexture(&converted, renderSystem.GetImmediateCopyQueue(), renderSystem, framework.core.GetBlockMemory(), DeviceFormat::R32_FLOAT);

			auto descAllocation = renderSystem._AllocateDescriptorRange(1);
			textureDesc = descAllocation.value();
			PushTextureToDescHeap(renderSystem, DXGI_FORMAT_R32_FLOAT, (uint32_t)0u, heightMap, textureDesc);
		}
		else
		{
			auto descAllocation = renderSystem._AllocateDescriptorRange(1);
			textureDesc = descAllocation.value();
			PushTextureToDescHeap(renderSystem, DXGI_FORMAT_R8G8B8A8_UNORM, (uint32_t)0u, renderSystem.DefaultTexture, textureDesc);
		}
	}


	/************************************************************************************************/


	FlexKit::UpdateTask* Update(FlexKit::EngineCore& core, FlexKit::UpdateDispatcher&, double dT) override
	{ 
		FlexKit::UpdateInput();

		return nullptr; 
	};


	/************************************************************************************************/


	void DrawHelloWorldTriangle(FlexKit::UpdateTask* update, FlexKit::EngineCore& core, FlexKit::UpdateDispatcher& dispatcher, double dT, FlexKit::FrameGraph& frameGraph)
	{
		auto color			= FlexKit::float4{ (rand() % 1024) / 1024.0f, (rand() % 1024) / 1024.0f, (rand() % 1024) / 1024.0f, 1 };
		auto vbPushBuffer	= FlexKit::CreateVertexBufferReserveObject(vertexBuffer, core.RenderSystem, core.GetTempMemoryMT());

		struct Trangle 
		{
			FlexKit::ReserveVertexBufferFunction	reserveVB;
			FlexKit::FrameResourceHandle			renderTarget;
		};

		frameGraph.AddNode<Trangle>(
			{
				.reserveVB = vbPushBuffer
			}, 
			[&](FlexKit::FrameGraphNodeBuilder& builder, Trangle& trangle)
			{
				builder.Requires(FlexKit::DRAW_PSO);
				trangle.renderTarget = builder.RenderTarget(renderWindow->GetBackBuffer());
			}, 
			[](Trangle& trangle, FlexKit::ResourceHandler& resources, FlexKit::Context& ctx, FlexKit::iAllocator& threadLocalAllocator)
			{
				ctx.BeginEvent_DEBUG("Draw Hello World");

				struct Vertex
				{
					FlexKit::float2 XY;
					FlexKit::float2 UV;
					float color[3] = { 1.0f, 1.0f, 1.0f };
				} triangle[3] =
				{
					{
						.XY { -1.0f, -1.0f }
					},
					{
						.XY { 0.0f, 1.0f }
					},
					{
						.XY { 1.0f, -1.0f }
					}
				};


				FlexKit::VBPushBuffer			vbPushAllocator{ trangle.reserveVB(512) };
				FlexKit::VertexBufferDataSet	vb{ triangle, vbPushAllocator };

				auto renderTarget = resources.RenderTarget(trangle.renderTarget, ctx);
				ctx.SetGraphicsPipelineState(FlexKit::DRAW_PSO, threadLocalAllocator);
				ctx.SetVertexBuffers({ vb });
				ctx.SetRenderTargets({ renderTarget });
				ctx.SetScissorAndViewports({ renderTarget });
				ctx.Draw(3);

				ctx.EndEvent_DEBUG();
			});
	}


	/************************************************************************************************/


	void TerrainAdaptiveLODUpdate(FlexKit::CameraHandle camera, FlexKit::FrameGraph& frameGraph, double dT)
	{
		struct CBT_UpdateAdaptiveTerrain
		{
			FlexKit::FrameResourceHandle indirectArgumentsBuffer;
			FlexKit::FrameResourceHandle cbtBuffer;
		};

		frameGraph.AddNode<CBT_UpdateAdaptiveTerrain>(
			{},
			[&](FlexKit::FrameGraphNodeBuilder& builder, CBT_UpdateAdaptiveTerrain& args)
			{
				args.indirectArgumentsBuffer	= builder.AcquireVirtualResource(FlexKit::GPUResourceDesc::UAVResource(1024), FlexKit::DeviceAccessState::DASUAV);
				args.cbtBuffer					= builder.UnorderedAccess(tree.GetBuffer());
			},
			[&, camera, dT](CBT_UpdateAdaptiveTerrain& args, FlexKit::ResourceHandler& resources, FlexKit::Context& ctx, FlexKit::iAllocator& threadLocalAllocator)
			{
				ctx.BeginEvent_DEBUG("Update CBT Adaptive Terrain");
				const uint32_t maxDepth = tree.GetMaxDepth();

				ctx.DiscardResource(resources.GetResource(args.indirectArgumentsBuffer));
				ctx.SetComputePipelineState(AdaptiveTerrainUpdateArgs, threadLocalAllocator);
				ctx.SetComputeUnorderedAccessView(0, resources.GetResource(args.cbtBuffer));
				ctx.SetComputeUnorderedAccessView(1, resources.GetResource(args.indirectArgumentsBuffer));
				ctx.SetComputeConstantValue(2, 1, &maxDepth);
				ctx.Dispatch({ 1, 1, 1 });

				static double t = (FlexKit::pi * 3.0f / 2.0f);

				struct {
					FlexKit::float4x4_GPU	PV;
					uint32_t				maxDepth;
					float					scale;
				} constants{ 
					.PV			= FlexKit::GetCameraConstants(camera).PV,
					.maxDepth	= tree.GetMaxDepth(),
					.scale		= 50 * (sinf(t) / 2.0f + 0.5f)
				};

				t += dT / 10;

				ctx.SetComputePipelineState(AdaptiveTerrainUpdate, threadLocalAllocator);
				ctx.SetComputeUnorderedAccessView(0, resources.UAV(args.cbtBuffer, ctx));
				ctx.SetComputeConstantValue(1, 18, &constants);
				ctx.SetComputeDescriptorTable(2, textureDesc);
				ctx.ExecuteIndirect(resources.IndirectArgs(args.indirectArgumentsBuffer, ctx), indirectDispatchLayout, 0);

				ctx.AddUAVBarrier(resources.GetResource(args.cbtBuffer));
				ctx.EndEvent_DEBUG();
			});

		tree.SumReduction_GPU(frameGraph);
	}


	/************************************************************************************************/


	void DebugVisCBTTree(FlexKit::CameraHandle camera, FlexKit::UpdateTask* update, FlexKit::ResourceHandle depthTarget, FlexKit::EngineCore& core, FlexKit::UpdateDispatcher& dispatcher, double dT, FlexKit::FrameGraph& frameGraph)
	{
		using namespace FlexKit;

		struct CBTDebugVis
		{
			FrameResourceHandle renderTarget;
			FrameResourceHandle depthTarget;
			FrameResourceHandle cbtBuffer;
			FrameResourceHandle indirectArgumentsBuffer;
		};

		frameGraph.AddNode<CBTDebugVis>(
			{
			},
			[&](FlexKit::FrameGraphNodeBuilder& builder, CBTDebugVis& debugVis)
			{
				if (update)
					builder.AddDataDependency(*update);

				builder.Requires(FlexKit::DRAW_PSO);
				debugVis.renderTarget				= builder.RenderTarget(renderWindow->GetBackBuffer());
				debugVis.depthTarget				= builder.DepthTarget(depthTarget);
				debugVis.cbtBuffer					= builder.NonPixelShaderResource(tree.GetBuffer());
				debugVis.indirectArgumentsBuffer	= builder.AcquireVirtualResource(GPUResourceDesc::UAVResource(1024), DeviceAccessState::DASUAV);
			},
			[&, camera, dT](CBTDebugVis& debugVis, ResourceHandler& resources, Context& ctx, iAllocator& threadLocalAllocator)
			{
				ctx.BeginEvent_DEBUG("DebugVisCBTTree");

				static double t = pi * 3.0f / 2.0f;

				struct {
					float4x4_GPU	PV;
					uint32_t		maxDepth;
					float			scale;
				} constants{
					.PV			= GetCameraConstants(camera).PV,
					.maxDepth	= tree.GetMaxDepth(),
					.scale		= 50 * (sinf(t) / 2.0f + 0.5f)
				};

				t += dT / 10;

				ctx.DiscardResource(resources.GetResource(debugVis.indirectArgumentsBuffer));
				ctx.SetComputePipelineState(AdaptiveTerrainDrawArgs, threadLocalAllocator);
				ctx.SetComputeUnorderedAccessView(0, resources.UAV(debugVis.cbtBuffer, ctx));
				ctx.SetComputeUnorderedAccessView(1, resources.UAV(debugVis.indirectArgumentsBuffer, ctx));
				ctx.SetComputeConstantValue(2, 1, &constants.maxDepth);
				ctx.Dispatch({ 1, 1, 1 });

				if(wireframe)
					ctx.SetGraphicsPipelineState(DrawCBTWireframe, threadLocalAllocator);
				else
					ctx.SetGraphicsPipelineState(DrawCBT, threadLocalAllocator);

				ctx.SetGraphicsConstantValue(0, 18, &constants);
				ctx.SetGraphicsShaderResourceView(1, resources.NonPixelShaderResource(debugVis.cbtBuffer, ctx));
				ctx.SetGraphicsDescriptorTable(2, textureDesc);
				
				RenderTargetList renderTargets = { resources.RenderTarget(debugVis.renderTarget, ctx) };
				ctx.SetScissorAndViewports(renderTargets);
				ctx.SetInputPrimitive(EInputPrimitive::INPUTPRIMITIVETRIANGLELIST);
				ctx.SetRenderTargets(renderTargets, true, resources.DepthTarget(debugVis.depthTarget, ctx));

				ctx.ExecuteIndirect(
					resources.IndirectArgs(debugVis.indirectArgumentsBuffer, ctx), 
					indirectDrawLayout);

				ctx.EndEvent_DEBUG();
			});
	}


	/************************************************************************************************/


	FlexKit::UpdateTask* Draw(FlexKit::UpdateTask* update, FlexKit::EngineCore& core, FlexKit::UpdateDispatcher& dispatcher, double dT, FlexKit::FrameGraph& frameGraph) override
	{ 
		using namespace FlexKit;

		if (!playing)
			return nullptr;

		frameGraph.AddOutput(renderWindow->GetBackBuffer());
		frameGraph.AddMemoryPool(poolAllocator);

		auto depthTarget = depthBuffer.Get();

		ClearBackBuffer(frameGraph, renderWindow->GetBackBuffer());
		ClearDepthBuffer(frameGraph, depthTarget, 1.0f);

		runOnce.Process(frameGraph);

		//gpuMemoryManager.DrawDebugVIS(frameGraph, renderWindow->GetBackBuffer());
		
		//Yaw(camera, dT * FlexKit::pi / 4.0f);
		MarkCameraDirty(activeCamera);

		auto& transformUpdate	= QueueTransformUpdateTask(dispatcher);
		auto& cameraUpdate		= cameras.QueueCameraUpdate(dispatcher);

		cameraUpdate.AddInput(transformUpdate);

		TerrainAdaptiveLODUpdate(activeCamera, frameGraph, dT);
		DebugVisCBTTree(activeCamera, &cameraUpdate, depthTarget, core, dispatcher, dT, frameGraph);

		//if(HEMesh && activeCamera != FlexKit::InvalidHandle)
		//	HEMesh->DrawSubDivLevel_DEBUG(frameGraph, activeCamera, &cameraUpdate, renderWindow->GetBackBuffer());

		PresentBackBuffer(frameGraph, renderWindow->GetBackBuffer());

		return nullptr; 
	}


	/************************************************************************************************/


	void PostDrawUpdate(FlexKit::EngineCore& core, double dT) override
	{
		depthBuffer.Increment();
		renderWindow->Present(1);
	}


	/************************************************************************************************/


	bool EventHandler(FlexKit::Event evt) 
	{
		if (evt.InputSource == evt.E_SystemEvent && evt.Action == FlexKit::Event::InputAction::Exit)
			framework.quit = true;

		if (evt.InputSource == FlexKit::Event::Keyboard && evt.mData1.mKC[0] == FlexKit::KC_SPACE && evt.Action == FlexKit::Event::Release)
			wireframe = !wireframe;

		if (evt.InputSource == FlexKit::Event::Keyboard && evt.mData1.mKC[0] == FlexKit::KC_P && evt.Action == FlexKit::Event::Release)
			playing = !playing;

		return false; 
	}

	FlexKit::RunOnceQueue<void (FlexKit::FrameGraph&)>	runOnce;

	FlexKit::CameraComponent		cameras;
	FlexKit::SceneNodeComponent		nodes;
	FlexKit::TriggerComponent		triggers;

	FlexKit::CBTBuffer				tree;
	TestComponent					testComponent;
	TestMultiFieldComponent			complexComponent;

	FlexKit::IndirectLayout			indirectDispatchLayout;
	FlexKit::IndirectLayout			indirectDrawLayout;
	FlexKit::ResourceHandle			heightMap		= FlexKit::InvalidHandle;
	std::unique_ptr<FlexKit::HalfEdgeMesh>	HEMesh;

	FlexKit::MemoryPoolAllocator	poolAllocator;
	FlexKit::VertexBufferHandle		vertexBuffer;
	FlexKit::Win32RenderWindow*		renderWindow = nullptr;
	FlexKit::DepthBuffer			depthBuffer;

	FlexKit::CameraHandle		activeCamera = FlexKit::InvalidHandle;
	FlexKit::GameObject			gameObjects[512];
	FlexKit::GameObject			camera;

	FlexKit::DescriptorRange	textureDesc;

	bool	wireframe = true;
	bool	playing		= false;
};


/************************************************************************************************/


int main(int argc, const char* argv[])
{
	auto memory = FlexKit::CreateEngineMemory();
	FlexKit::FKApplication app{ memory, FlexKit::CoreOptions{ .GPUdebugMode = true } };

	app.PushState<CBTTerrainState>();
	app.Run();

	return 0;
}


/**********************************************************************

Copyright (c) 2024 Robert May

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

**********************************************************************/
