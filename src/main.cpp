#include <Application.hpp>
#include <Win32Graphics.hpp>
#include "TestComponent.hpp"
#include <print>
#include <atomic>
#include <CBT.hpp>
#include <ModifiableShape.hpp>
#include <ranges>
#include <TextureUtilities.hpp>
#include <stb_image.h>
#include <Transforms.hpp>
#include <TriggerComponent.hpp>
#include <MemoryUtilities.hpp>

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


struct HEEdge
{
	uint32_t twin;
	uint32_t next;
	uint32_t prev;
	uint32_t vert;
};


struct HEVertex
{
	FlexKit::float3 point;
	FlexKit::float2 UV;
};


struct HalfEdgeMesh
{
	struct XYZ
	{
		float xyz[3];
	};


	HalfEdgeMesh(
		const	FlexKit::ModifiableShape&	shape,
				FlexKit::RenderSystem&		IN_renderSystem, 
				FlexKit::iAllocator&		IN_allocator) : 
		cbt		{ IN_renderSystem, IN_allocator } 
	{
		FlexKit::Vector<HEEdge> halfEdges{ IN_allocator };

		halfEdges.reserve(shape.wEdges.size());
		for (const auto&& [idx, edge] : std::views::enumerate(shape.wEdges))
		{
			HEEdge hEdge
			{
				.twin = edge.twin,
				.next = edge.next,
				.prev = edge.prev,
				.vert = edge.vertices[0],
				//.face = edge.face,
				//.edge = (uint32_t)idx
			};

			halfEdges.push_back(hEdge);
		}

		FlexKit::Vector<FlexKit::uint2> faces{ IN_allocator };
		faces.reserve(shape.wFaces.size());
		for (const auto& face : shape.wFaces)
			faces.emplace_back(face.edgeStart, face.GetEdgeCount(shape));

		FlexKit::Vector<XYZ> meshPoints{ IN_allocator };
		meshPoints.resize(shape.wVertices.size());
		for (const auto&& [idx, point] : std::views::enumerate(shape.wVertices))
			memcpy(meshPoints.data() + idx, &point, sizeof(XYZ));

		controlFaces		= IN_renderSystem.CreateGPUResource(FlexKit::GPUResourceDesc::StructuredResource(faces.ByteSize()));
		controlCage			= IN_renderSystem.CreateGPUResource(FlexKit::GPUResourceDesc::StructuredResource(halfEdges.ByteSize()));
		controlPoints		= IN_renderSystem.CreateGPUResource(FlexKit::GPUResourceDesc::StructuredResource(meshPoints.ByteSize()));
		controlCageSize		= halfEdges.size();
		controlCageFaces	= faces.size();
		const uint32_t level0PointCount = shape.wFaces.size() + shape.wEdges.size() * 2;
		const uint32_t level1PointCount = level0PointCount * 5;

		levels[0] = IN_renderSystem.CreateGPUResource(FlexKit::GPUResourceDesc::UAVResource(halfEdges.ByteSize() * 4));
		levels[1] = IN_renderSystem.CreateGPUResource(FlexKit::GPUResourceDesc::UAVResource(halfEdges.ByteSize() * 4));
		points[0] = IN_renderSystem.CreateGPUResource(FlexKit::GPUResourceDesc::UAVResource(level0PointCount * sizeof(XYZ)));
		points[1] = IN_renderSystem.CreateGPUResource(FlexKit::GPUResourceDesc::UAVResource(level1PointCount * sizeof(XYZ)));


		auto uploadQueue = IN_renderSystem.GetImmediateCopyQueue();
		IN_renderSystem.UpdateResourceByUploadQueue(
				IN_renderSystem.GetDeviceResource(controlCage),
				uploadQueue, 
				halfEdges.data(), 
				halfEdges.ByteSize(), 1, FlexKit::DASNonPixelShaderResource);

		IN_renderSystem.UpdateResourceByUploadQueue(
				IN_renderSystem.GetDeviceResource(controlPoints),
				uploadQueue,
				meshPoints.data(),
				meshPoints.ByteSize(), 1, FlexKit::DASNonPixelShaderResource);

		IN_renderSystem.UpdateResourceByUploadQueue(
				IN_renderSystem.GetDeviceResource(controlFaces),
				uploadQueue,
				faces.data(),
				faces.ByteSize(), 1, FlexKit::DASNonPixelShaderResource);

		cbt.Initialize({ 512 });

		static bool registerStates = 
			[&]
			{
				IN_renderSystem.RegisterPSOLoader(
					FaceInitiate,
					[](FlexKit::RenderSystem* renderSystem, FlexKit::iAllocator& allocator) 
					{
						return FlexKit::PipelineBuilder{ allocator }.
							AddComputeShader("FacePassInitiate", "assets\\shaders\\HalfEdge\\HalfEdge.hlsl", { .hlsl2021 = true }).
							Build(*renderSystem);
					});

				IN_renderSystem.RegisterPSOLoader(
					EdgeUpdate,
					[](FlexKit::RenderSystem* renderSystem, FlexKit::iAllocator& allocator)
					{
						return FlexKit::PipelineBuilder{ allocator }.
							AddComputeShader("EdgePass", "assets\\shaders\\HalfEdge\\HalfEdge.hlsl", { .hlsl2021 = true }).
							Build(*renderSystem);
					});

				IN_renderSystem.RegisterPSOLoader(
					RenderFaces,
					[](FlexKit::RenderSystem* renderSystem, FlexKit::iAllocator& allocator)
					{
						return FlexKit::PipelineBuilder{ allocator }.
							AddMeshShader("MeshMain",	"assets\\shaders\\HalfEdge\\DebugVIS.hlsl", { .hlsl2021 = true }).
							AddPixelShader("PMain",		"assets\\shaders\\HalfEdge\\DebugVIS.hlsl", { .hlsl2021 = true }).
							AddRasterizerState({ .fill = FlexKit::EFillMode::WIREFRAME, .CullMode = FlexKit::ECullMode::NONE }).
							AddRenderTargetState(
								{	.targetCount	= 1,
									.targetFormats	= { FlexKit::DeviceFormat::R16G16B16A16_FLOAT } }).
							Build(*renderSystem);
					});

				IN_renderSystem.QueuePSOLoad(FaceInitiate);
				IN_renderSystem.QueuePSOLoad(EdgeUpdate);
				IN_renderSystem.QueuePSOLoad(RenderFaces);

				return true;
			}();
	}

	~HalfEdgeMesh()
	{
		FlexKit::RenderSystem::globalInstance->ReleaseResource(controlFaces);
		FlexKit::RenderSystem::globalInstance->ReleaseResource(controlCage);
		FlexKit::RenderSystem::globalInstance->ReleaseResource(levels[0]);
		FlexKit::RenderSystem::globalInstance->ReleaseResource(levels[1]);
		FlexKit::RenderSystem::globalInstance->ReleaseResource(points[0]);
		FlexKit::RenderSystem::globalInstance->ReleaseResource(points[1]);
	}

	void BuildSubDivLevel(FlexKit::FrameGraph& frameGraph)
	{
		if (levelsBuilt >= 2)
			return;

		struct buildLevel
		{
			FlexKit::FrameResourceHandle inputCage;
			FlexKit::FrameResourceHandle inputVerts;
			FlexKit::FrameResourceHandle inputFaces;

			FlexKit::FrameResourceHandle outputCage;
			FlexKit::FrameResourceHandle outputVerts;
			FlexKit::FrameResourceHandle counters;

			uint32_t inputEdgeCount;
			uint32_t faceCount;
		};

		if(levelsBuilt == 0)
		{
			frameGraph.AddOutput(levels[0]);
			frameGraph.AddOutput(points[0]);

			frameGraph.AddNode<buildLevel>(
				{},
				[&](FlexKit::FrameGraphNodeBuilder& builder, buildLevel& subDivData)
				{
					builder.Requires(FlexKit::DRAW_PSO);
					subDivData.inputCage		= builder.NonPixelShaderResource(controlCage);
					subDivData.inputVerts		= builder.NonPixelShaderResource(controlPoints);
					subDivData.inputFaces		= builder.NonPixelShaderResource(controlFaces);
					subDivData.inputEdgeCount	= controlCageSize;
					subDivData.faceCount		= controlCageFaces;
					edgeCount[levelsBuilt]		= controlCageSize * 4;

					subDivData.outputCage		= builder.NonPixelShaderResource(levels[levelsBuilt]);
					subDivData.outputVerts		= builder.NonPixelShaderResource(points[levelsBuilt]);
					subDivData.counters			= builder.AcquireVirtualResource(FlexKit::GPUResourceDesc::UAVResource(512), FlexKit::DASUAV);

					levelsBuilt++;
				},
				[this](buildLevel& subDivData, FlexKit::ResourceHandler& resources, FlexKit::Context& ctx, FlexKit::iAllocator& threadLocalAllocator)
				{
					ctx.BeginEvent_DEBUG("Subdivision : Build Level");

					ctx.DiscardResource(resources.GetResource(subDivData.counters));
					ctx.ClearUAVBuffer(resources.UAV(subDivData.counters, ctx));

					ctx.SetComputePipelineState(FaceInitiate, threadLocalAllocator);
					ctx.SetComputeUnorderedAccessView(0, resources.UAV(subDivData.outputCage, ctx));
					ctx.SetComputeUnorderedAccessView(1, resources.UAV(subDivData.outputVerts, ctx));
					ctx.SetComputeUnorderedAccessView(2, resources.UAV(subDivData.counters, ctx));
					ctx.SetComputeShaderResourceView(3, resources.NonPixelShaderResource(subDivData.inputCage, ctx));
					ctx.SetComputeShaderResourceView(4, resources.NonPixelShaderResource(subDivData.inputVerts, ctx));
					ctx.SetComputeShaderResourceView(5, resources.NonPixelShaderResource(subDivData.inputFaces, ctx));
					ctx.SetComputeConstantValue(6, 1, &subDivData.faceCount);
					ctx.Dispatch({ FlexKit::Max(subDivData.faceCount / 64, 1), 1, 1 });

					ctx.EndEvent_DEBUG();
				});
		}
	}

	void DrawSubDivLevel_DEBUG(FlexKit::FrameGraph& frameGraph, FlexKit::CameraHandle camera, FlexKit::UpdateTask* update, FlexKit::ResourceHandle renderTarget)
	{
		if (levelsBuilt == 0)
			return;

		struct DrawLevel
		{
			FlexKit::FrameResourceHandle renderTarget;
			FlexKit::FrameResourceHandle inputCage;
			FlexKit::FrameResourceHandle inputVerts;
		};

		frameGraph.AddNode<DrawLevel>(
			{},
			[&](FlexKit::FrameGraphNodeBuilder& builder, DrawLevel& visData)
			{
				if (update)
					builder.AddDataDependency(*update);

				visData.renderTarget	= builder.RenderTarget(renderTarget);
				visData.inputCage		= builder.NonPixelShaderResource(levels[levelsBuilt - 1]);
				visData.inputVerts		= builder.NonPixelShaderResource(points[levelsBuilt - 1]);
			},
			[this, camera](DrawLevel& visData, FlexKit::ResourceHandler& resources, FlexKit::Context& ctx, FlexKit::iAllocator& threadLocalAllocator)
			{
				ctx.BeginEvent_DEBUG("Draw HE Mesh");

				FlexKit::RenderTargetList renderTargets = { resources.RenderTarget(visData.renderTarget, ctx) };
				ctx.SetGraphicsPipelineState(RenderFaces, threadLocalAllocator);
				ctx.SetGraphicsShaderResourceView(0, resources.NonPixelShaderResource(visData.inputCage, ctx));
				ctx.SetGraphicsShaderResourceView(1, resources.NonPixelShaderResource(visData.inputVerts, ctx));

				struct {
					FlexKit::float4x4_GPU	PV;
					uint32_t				patchCount;
				}	constants{
						.PV			= FlexKit::GetCameraConstants(camera).PV,
						.patchCount = edgeCount[levelsBuilt - 1] / 4
				};

				ctx.SetGraphicsConstantValue(2, 17, &constants);
				ctx.SetScissorAndViewports(renderTargets);
				ctx.SetRenderTargets(renderTargets);
				ctx.DispatchMesh({ 1, 1, 1 });

				ctx.EndEvent_DEBUG();
			});
	}


	static constexpr FlexKit::PSOHandle EdgeUpdate		= FlexKit::PSOHandle{ GetTypeGUID(HEEdgeUpdate) };
	static constexpr FlexKit::PSOHandle FaceInitiate	= FlexKit::PSOHandle{ GetTypeGUID(HEFaceInitiate) };
	static constexpr FlexKit::PSOHandle FacePass		= FlexKit::PSOHandle{ GetTypeGUID(HEFacePass) };
	static constexpr FlexKit::PSOHandle VertexUpdate	= FlexKit::PSOHandle{ GetTypeGUID(HEVertexUpdate) };
	static constexpr FlexKit::PSOHandle RenderFaces		= FlexKit::PSOHandle{ GetTypeGUID(HERenderFaces) };


	uint32_t					controlCageSize		= 0;
	uint32_t					controlCageFaces	= 0;
	FlexKit::ResourceHandle		controlFaces		= FlexKit::InvalidHandle;
	FlexKit::ResourceHandle		controlCage			= FlexKit::InvalidHandle;
	FlexKit::ResourceHandle		controlPoints		= FlexKit::InvalidHandle;
	FlexKit::ResourceHandle		levels[2]			= { FlexKit::InvalidHandle, FlexKit::InvalidHandle };
	FlexKit::ResourceHandle		points[2]			= { FlexKit::InvalidHandle, FlexKit::InvalidHandle };
	uint32_t					edgeCount[2]		= { 0, 0 };
	uint8_t						levelsBuilt			= 0;
	FlexKit::CBTBuffer			cbt;
};


struct CBTTerrainState : FlexKit::FrameworkState
{
	CBTTerrainState(FlexKit::GameFramework& in_framework) :
		FlexKit::FrameworkState	{ in_framework },
		testComponent			{ in_framework.core.GetBlockMemory() },
		complexComponent		{ in_framework.core.GetBlockMemory() },
		tree					{ in_framework.GetRenderSystem(), in_framework.core.GetBlockMemory() },
		runOnce					{ in_framework.core.GetBlockMemory() },
		poolAllocator			{ in_framework.GetRenderSystem(), 64 * MEGABYTE, FlexKit::DefaultBlockSize, FlexKit::DeviceHeapFlags::UAVBuffer, in_framework.core.GetBlockMemory() },
		cameras					{ in_framework.core.GetBlockMemory() },
		triggers				{ in_framework.core.GetBlockMemory(), in_framework.core.GetBlockMemory() }
	{
		auto& renderSystem = framework.GetRenderSystem();
		renderWindow = FlexKit::CreateWin32RenderWindow(framework.GetRenderSystem(),
			{
				.height = 1080,
				.width	= 1920,
			});

		FlexKit::EventNotifier<>::Subscriber sub;
		sub.Notify = &FlexKit::EventsWrapper;
		sub._ptr = &framework;
		renderWindow->Handler.Subscribe(sub);
		renderWindow->SetWindowTitle("HelloWorld");

		vertexBuffer = renderSystem.CreateVertexBuffer(512, false);

		renderSystem.RegisterPSOLoader(FlexKit::DRAW_PSO, FlexKit::CreateDrawTriStatePSO);

		const uint32_t depth = 18;
		tree.Initialize({ .maxDepth = depth });

		uint32_t expectedSum = 0;
		for (int i = 0; i < (1 << depth); i += 1)
			tree.SetBit(i, true);

		runOnce.push_back([&](FlexKit::FrameGraph& frameGraph)
			{
				tree.Upload(frameGraph);
				tree.Update(frameGraph);
			});


		FlexKit::ModifiableShape shape{ framework.core.GetBlockMemory() };

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

		auto& cameraNode	= camera.AddView<FlexKit::SceneNodeView>();
		auto& cameraView	= camera.AddView<FlexKit::CameraView>();

		cameraNode.Pitch(FlexKit::pi / -6);
		cameraNode.TranslateWorld({ -2.5f, 2.5, 5 });
		cameraView.SetCameraNode(cameraNode);
		cameraView.SetCameraFOV(FlexKit::pi / 3);
		cameraView.SetCameraAspectRatio(1080.0f / 1920.0f);

		activeCamera = cameraView;

		int width, height, channels;
		auto heightValues = stbi_loadf("assets\\HeightMap.png", &width, &height, &channels, 1);
		
		FlexKit::uint2 wh{ width, height };
		size_t rowPitch		= FlexKit::AlignedSize(width * 4);
		size_t bufferSize	= rowPitch * height;

		FlexKit::TextureBuffer converted{ wh, 4, bufferSize, FlexKit::SystemAllocator };

		//FlexKit::TextureBuffer buffer							{ wh, 4, bufferSize , FlexKit::SystemAllocator };
		FlexKit::TextureBuffer buffer					{ wh, (std::byte*)heightValues, 4 };
		FlexKit::TextureBufferView<float> inputView		{ buffer };
		FlexKit::TextureBufferView<float> outputView	{ converted, rowPitch };

		memset(converted.Buffer, 0, converted.BufferSize());

		for (size_t y_itr = 0; y_itr < wh[1]; y_itr++)
		{
			for (size_t x_itr = 0; x_itr < wh[0]; x_itr++)
			{
				FlexKit::uint2 px	= { x_itr, y_itr };
				outputView[px]		= inputView[px];
			}
		}

		if (!heightValues)
			throw std::runtime_error("Failed to load Height Map!");
		
		heightMap = FlexKit::LoadTexture(&converted, renderSystem.GetImmediateCopyQueue(), renderSystem, framework.core.GetBlockMemory(), FlexKit::DeviceFormat::R32_FLOAT);

		auto descAllocation = renderSystem._AllocateDescriptorRange(1);
		textureDesc = descAllocation.value();
		FlexKit::PushTextureToDescHeap(renderSystem, DXGI_FORMAT_R32_FLOAT, (uint32_t)0u, heightMap, textureDesc);
	}


	FlexKit::UpdateTask* Update(FlexKit::EngineCore& core, FlexKit::UpdateDispatcher&, double dT) override
	{ 
		FlexKit::UpdateInput();

		return nullptr; 
	};


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


	void DebugVisCBTTree(FlexKit::CameraHandle camera, FlexKit::UpdateTask* update, FlexKit::EngineCore& core, FlexKit::UpdateDispatcher& dispatcher, double dT, FlexKit::FrameGraph& frameGraph)
	{
		struct CBTDebugVis
		{
			FlexKit::FrameResourceHandle renderTarget;
			FlexKit::FrameResourceHandle cbtBuffer;
		};

		frameGraph.AddNode<CBTDebugVis>(
			{
			},
			[&](FlexKit::FrameGraphNodeBuilder& builder, CBTDebugVis& debugVis)
			{
				if (update)
					builder.AddDataDependency(*update);

				builder.Requires(FlexKit::DRAW_PSO);
				debugVis.renderTarget	= builder.RenderTarget(renderWindow->GetBackBuffer());
				debugVis.cbtBuffer		= builder.UnorderedAccess(tree.GetBuffer());
			},
			[&, camera](CBTDebugVis& debugVis, FlexKit::ResourceHandler& resources, FlexKit::Context& ctx, FlexKit::iAllocator& threadLocalAllocator)
			{
				ctx.BeginEvent_DEBUG("DebugVisCBTTree");

				struct {
					FlexKit::float4x4_GPU	PV;
					uint32_t				maxDepth;
				} constants{
					.PV			= FlexKit::GetCameraConstants(camera).PV,
					.maxDepth	= tree.GetMaxDepth(),
				};
				
				ctx.SetComputePipelineState(FlexKit::UpdateCBTTree, threadLocalAllocator);
				ctx.SetComputeUnorderedAccessView(0, resources.UAV(debugVis.cbtBuffer, ctx));
				ctx.SetComputeConstantValue(1, 1, &constants.maxDepth);
				ctx.Dispatch({ 1, 1, 1 });
				
				ctx.AddUAVBarrier(resources.UAV(debugVis.cbtBuffer, ctx));

				ctx.SetGraphicsPipelineState(FlexKit::DrawCBTTree, threadLocalAllocator);
				ctx.SetGraphicsUnorderedAccessView(0, resources.UAV(debugVis.cbtBuffer, ctx));
				ctx.SetGraphicsConstantValue(1, 17, &constants);
				ctx.SetGraphicsDescriptorTable(2, textureDesc);
				
				FlexKit::RenderTargetList renderTargets = { resources.RenderTarget(debugVis.renderTarget, ctx) };
				ctx.SetScissorAndViewports(renderTargets);
				ctx.SetInputPrimitive(FlexKit::EInputPrimitive::INPUTPRIMITIVETRIANGLELIST);
				ctx.SetRenderTargets(renderTargets);

				auto temp = tree.GetHeapValue(1);

				ctx.Draw(3 * (1 << tree.maxDepth));

				ctx.EndEvent_DEBUG();
			});
	}


	FlexKit::UpdateTask* Draw(FlexKit::UpdateTask* update, FlexKit::EngineCore& core, FlexKit::UpdateDispatcher& dispatcher, double dT, FlexKit::FrameGraph& frameGraph) override
	{ 
		frameGraph.AddOutput(renderWindow->GetBackBuffer());
		frameGraph.AddMemoryPool(poolAllocator);

		FlexKit::ClearBackBuffer(frameGraph, renderWindow->GetBackBuffer());

		runOnce.Process(frameGraph);

		//gpuMemoryManager.DrawDebugVIS(frameGraph, renderWindow->GetBackBuffer());
		
		FlexKit::Yaw(camera, dT * FlexKit::pi / 4.0f);
		FlexKit::MarkCameraDirty(activeCamera);

		auto& transformUpdate	= FlexKit::QueueTransformUpdateTask(dispatcher);
		auto& cameraUpdate		= cameras.QueueCameraUpdate(dispatcher);

		cameraUpdate.AddInput(transformUpdate);

		DebugVisCBTTree(activeCamera, &cameraUpdate, core, dispatcher, dT, frameGraph);

		//if(HEMesh && activeCamera != FlexKit::InvalidHandle)
		//	HEMesh->DrawSubDivLevel_DEBUG(frameGraph, activeCamera, &cameraUpdate, renderWindow->GetBackBuffer());

		FlexKit::PresentBackBuffer(frameGraph, renderWindow->GetBackBuffer());

		return nullptr; 
	}

	void PostDrawUpdate(FlexKit::EngineCore& core, double dT) override
	{
		renderWindow->Present(1);
	}

	bool EventHandler(FlexKit::Event evt) 
	{
		if (evt.E_SystemEvent && evt.Action == FlexKit::Event::InputAction::Exit)
			framework.quit = true;

		return false; 
	}

	FlexKit::RunOnceQueue<void (FlexKit::FrameGraph&)>	runOnce;

	FlexKit::CameraComponent		cameras;
	FlexKit::SceneNodeComponent		nodes;
	FlexKit::TriggerComponent		triggers;

	FlexKit::CBTBuffer				tree;
	TestComponent					testComponent;
	TestMultiFieldComponent			complexComponent;
	FlexKit::ResourceHandle			heightMap	= FlexKit::InvalidHandle;

	std::unique_ptr<HalfEdgeMesh>	HEMesh;

	FlexKit::MemoryPoolAllocator	poolAllocator;
	FlexKit::VertexBufferHandle		vertexBuffer;
	FlexKit::Win32RenderWindow*		renderWindow = nullptr;

	FlexKit::CameraHandle		activeCamera = FlexKit::InvalidHandle;
	FlexKit::GameObject			gameObjects[512];
	FlexKit::GameObject			camera;

	FlexKit::DescriptorRange	textureDesc;
};


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
