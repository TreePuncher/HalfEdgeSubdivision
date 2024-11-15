#include "HalfEdgeMesh.hpp"
#include <Containers.hpp>


namespace FlexKit
{	/************************************************************************************************/


	HalfEdgeMesh::HalfEdgeMesh(
			const	ModifiableShape&	shape,
					RenderSystem&		IN_renderSystem, 
					iAllocator&		IN_allocator) : 
			cbt		{ IN_renderSystem, IN_allocator } 
	{
		Vector<HEEdge> halfEdges{ IN_allocator };

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
			faces.emplace_back(face.edgeStart, (uint32_t)face.GetEdgeCount(shape));

		FlexKit::Vector<XYZ> meshPoints{ IN_allocator };
		meshPoints.resize(shape.wVertices.size());
		for (const auto&& [idx, point] : std::views::enumerate(shape.wVertices))
			memcpy(meshPoints.data() + idx, &point, sizeof(XYZ));

		controlFaces		= IN_renderSystem.CreateGPUResource(GPUResourceDesc::StructuredResource(faces.ByteSize()));
		controlCage			= IN_renderSystem.CreateGPUResource(GPUResourceDesc::StructuredResource(halfEdges.ByteSize()));
		controlPoints		= IN_renderSystem.CreateGPUResource(GPUResourceDesc::StructuredResource(meshPoints.ByteSize()));
		controlCageSize		= halfEdges.size();
		controlCageFaces	= faces.size();
		const uint32_t level0PointCount = shape.wFaces.size() + shape.wEdges.size() * 2;
		const uint32_t level1PointCount = level0PointCount * 5;

		levels[0] = IN_renderSystem.CreateGPUResource(GPUResourceDesc::UAVResource(halfEdges.ByteSize() * 4));
		levels[1] = IN_renderSystem.CreateGPUResource(GPUResourceDesc::UAVResource(halfEdges.ByteSize() * 4));
		points[0] = IN_renderSystem.CreateGPUResource(GPUResourceDesc::UAVResource(level0PointCount * sizeof(XYZ)));
		points[1] = IN_renderSystem.CreateGPUResource(GPUResourceDesc::UAVResource(level1PointCount * sizeof(XYZ)));


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
							AddRasterizerState({ .fill = FlexKit::EFillMode::SOLID, .CullMode = FlexKit::ECullMode::BACK }).
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


	/************************************************************************************************/


	HalfEdgeMesh::~HalfEdgeMesh()
	{
		RenderSystem::globalInstance->ReleaseResource(controlFaces);
		RenderSystem::globalInstance->ReleaseResource(controlCage);
		RenderSystem::globalInstance->ReleaseResource(levels[0]);
		RenderSystem::globalInstance->ReleaseResource(levels[1]);
		RenderSystem::globalInstance->ReleaseResource(points[0]);
		RenderSystem::globalInstance->ReleaseResource(points[1]);
	}


	/************************************************************************************************/


	void HalfEdgeMesh::BuildSubDivLevel(FlexKit::FrameGraph& frameGraph)
	{
		if (levelsBuilt >= 2)
			return;

		struct buildLevel
		{
			FrameResourceHandle inputCage;
			FrameResourceHandle inputVerts;
			FrameResourceHandle inputFaces;

			FrameResourceHandle outputCage;
			FrameResourceHandle outputVerts;
			FrameResourceHandle counters;

			uint32_t inputEdgeCount;
			uint32_t faceCount;
		};

		if(levelsBuilt == 0)
		{
			frameGraph.AddOutput(levels[0]);
			frameGraph.AddOutput(points[0]);

			frameGraph.AddNode<buildLevel>(
				{},
				[&](FrameGraphNodeBuilder& builder, buildLevel& subDivData)
				{
					builder.Requires(DRAW_PSO);
					subDivData.inputCage		= builder.NonPixelShaderResource(controlCage);
					subDivData.inputVerts		= builder.NonPixelShaderResource(controlPoints);
					subDivData.inputFaces		= builder.NonPixelShaderResource(controlFaces);
					subDivData.inputEdgeCount	= controlCageSize;
					subDivData.faceCount		= controlCageFaces;
					edgeCount[levelsBuilt]		= controlCageSize * 4;

					subDivData.outputCage		= builder.NonPixelShaderResource(levels[levelsBuilt]);
					subDivData.outputVerts		= builder.NonPixelShaderResource(points[levelsBuilt]);
					subDivData.counters			= builder.AcquireVirtualResource(GPUResourceDesc::UAVResource(512), FlexKit::DASUAV);

					levelsBuilt++;
				},
				[this](buildLevel& subDivData, ResourceHandler& resources, Context& ctx, iAllocator& threadLocalAllocator)
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
					ctx.Dispatch({ Max(subDivData.faceCount / 64, 1), 1, 1 });

					ctx.EndEvent_DEBUG();
				});
		}
	}


	/************************************************************************************************/


	void HalfEdgeMesh::DrawSubDivLevel_DEBUG(FrameGraph& frameGraph, CameraHandle camera, UpdateTask* update, ResourceHandle renderTarget)
	{
		if (levelsBuilt == 0)
			return;

		struct DrawLevel
		{
			FrameResourceHandle renderTarget;
			FrameResourceHandle inputCage;
			FrameResourceHandle inputVerts;
		};

		frameGraph.AddNode<DrawLevel>(
			{},
			[&](FrameGraphNodeBuilder& builder, DrawLevel& visData)
			{
				if (update)
					builder.AddDataDependency(*update);

				visData.renderTarget	= builder.RenderTarget(renderTarget);
				visData.inputCage		= builder.NonPixelShaderResource(levels[levelsBuilt - 1]);
				visData.inputVerts		= builder.NonPixelShaderResource(points[levelsBuilt - 1]);
			},
			[this, camera](DrawLevel& visData, ResourceHandler& resources, Context& ctx, iAllocator& threadLocalAllocator)
			{
				ctx.BeginEvent_DEBUG("Draw HE Mesh");

				RenderTargetList renderTargets = { resources.RenderTarget(visData.renderTarget, ctx) };
				ctx.SetGraphicsPipelineState(RenderFaces, threadLocalAllocator);
				ctx.SetGraphicsShaderResourceView(0, resources.NonPixelShaderResource(visData.inputCage, ctx));
				ctx.SetGraphicsShaderResourceView(1, resources.NonPixelShaderResource(visData.inputVerts, ctx));

				struct {
					float4x4_GPU	PV;
					uint32_t		patchCount;
				}	constants{
						.PV			= GetCameraConstants(camera).PV,
						.patchCount = edgeCount[levelsBuilt - 1] / 4
				};

				ctx.SetGraphicsConstantValue(2, 17, &constants);
				ctx.SetScissorAndViewports(renderTargets);
				ctx.SetRenderTargets(renderTargets);
				ctx.DispatchMesh({ 1, 1, 1 });

				ctx.EndEvent_DEBUG();
			});
	}


}	/************************************************************************************************/
