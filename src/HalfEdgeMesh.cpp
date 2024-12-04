#include "HalfEdgeMesh.hpp"
#include <LibraryBuilder.hpp>
#include <Containers.hpp>


namespace FlexKit
{	/************************************************************************************************/


	HalfEdgeMesh::HalfEdgeMesh(
			const	ModifiableShape&	shape,
					RenderSystem&		IN_renderSystem, 
					iAllocator&			IN_allocator, 
					iAllocator&			IN_temp) : 
			cbt		{ IN_renderSystem, IN_allocator } 
	{
		Vector<HEEdge>		halfEdges{ IN_allocator };
		Vector<uint>		faceLookupBuffer{ IN_allocator };

		halfEdges.reserve(shape.wEdges.size());
		for (const auto&& [idx, edge] : std::views::enumerate(shape.wEdges))
		{
			const bool isOnEdge	= shape.IsEdgeVertex(edge.vertices[0]);

			auto CalculateTwinValue = [&]() -> uint32_t
			{
				return	(edge.twin & (0xffffffff >> 2)) |
						(shape.GetVertexValence(edge.vertices[0]) == 2 ? (1 << 31) : 0) |
						(isOnEdge ? (1 << 30) : 0);
			};

			HEEdge hEdge
			{
				.twin = CalculateTwinValue(),
				.next = edge.next,
				.prev = edge.prev,
				.vert = edge.vertices[0],
			};

			halfEdges.push_back(hEdge);
		}

		uint32_t edgeCount = 0;
		uint32_t vertexCount = 0;
		Vector<uint3> faces{ IN_allocator };
		faces.reserve(shape.wFaces.size());

		for (const auto&& [idx, face] : enumerate(shape.wFaces))
		{ 
			auto faceEdgeCount = (uint32_t)face.GetEdgeCount(shape);
			faces.emplace_back(face.edgeStart, faceEdgeCount, vertexCount);
			edgeCount += faceEdgeCount;
			vertexCount += 1 + 2 * faceEdgeCount;

			for (int i = 0; i < faceEdgeCount; i++)
				faceLookupBuffer.emplace_back((uint32_t)idx);
		}
		
		std::cout << "Half edge count: " << edgeCount << "\n";
		std::cout << "L0 Half edge count: " << edgeCount * 4 << "\n";
		std::cout << "L1 Half edge count: " << edgeCount * 16 << "\n";
		std::cout << "L2 Half edge count: " << edgeCount * 64 << "\n";
		std::cout << "Vertex count: " << vertexCount << "\n";

		Vector<HalfEdgeVertex> meshPoints{ IN_allocator };
		meshPoints.resize(shape.wVertices.size());
		for (const auto&& [idx, point] : std::views::enumerate(shape.wVertices))
		{
			HalfEdgeVertex v;
			v.rgba = 0xff00ff00;
			v.UV   = float2(0.0f, 0.0f);
			memcpy(&v.xyz, &point, sizeof(HalfEdgeVertex));
			meshPoints[idx] = v;
		}

		controlFaces		= IN_renderSystem.CreateGPUResource(GPUResourceDesc::StructuredResource(faces.ByteSize()));
		controlCage			= IN_renderSystem.CreateGPUResource(GPUResourceDesc::StructuredResource(halfEdges.ByteSize()));
		controlPoints		= IN_renderSystem.CreateGPUResource(GPUResourceDesc::StructuredResource(meshPoints.ByteSize()));
		faceLookup			= IN_renderSystem.CreateGPUResource(GPUResourceDesc::StructuredResource(faceLookupBuffer.ByteSize()));

		controlCageSize		= halfEdges.size();
		controlCageFaces	= faces.size();
		const uint32_t level0PointCount = (shape.wFaces.size() + shape.wEdges.size()) * 2;
		const uint32_t level1PointCount = level0PointCount * 9;
		const uint32_t level2PointCount = level1PointCount * 9;

		levels[0] = IN_renderSystem.CreateGPUResource(GPUResourceDesc::UAVResource(halfEdges.ByteSize() * 4));
		levels[1] = IN_renderSystem.CreateGPUResource(GPUResourceDesc::UAVResource(halfEdges.ByteSize() * 16));
		levels[2] = IN_renderSystem.CreateGPUResource(GPUResourceDesc::UAVResource(halfEdges.ByteSize() * 64));
		points[0] = IN_renderSystem.CreateGPUResource(GPUResourceDesc::UAVResource(level0PointCount * sizeof(HalfEdgeVertex)));
		points[1] = IN_renderSystem.CreateGPUResource(GPUResourceDesc::UAVResource(level1PointCount * sizeof(HalfEdgeVertex)));
		points[2] = IN_renderSystem.CreateGPUResource(GPUResourceDesc::UAVResource(level2PointCount * sizeof(HalfEdgeVertex)));

		patchCount[0] = edgeCount;
		patchCount[1] = patchCount[0] * 4;
		patchCount[2] = patchCount[1] * 4;

		IN_renderSystem.SetDebugName(levels[0], "level_0");
		IN_renderSystem.SetDebugName(levels[1], "level_1");
		IN_renderSystem.SetDebugName(levels[2], "level_2");

		IN_renderSystem.SetDebugName(points[0], "points_0");
		IN_renderSystem.SetDebugName(points[1], "points_1");
		IN_renderSystem.SetDebugName(points[2], "points_2");


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
		
		IN_renderSystem.UpdateResourceByUploadQueue(
				IN_renderSystem.GetDeviceResource(faceLookup),
				uploadQueue,
				faceLookupBuffer.data(),
				faceLookupBuffer.ByteSize(), 1, FlexKit::DASNonPixelShaderResource);

		cbt.Initialize({ .maxDepth = 14 });

		static bool registerStates = 
			[&]
			{
				DesciptorHeapLayout heap0{};
				DesciptorHeapLayout heap1{};
				heap0.SetParameterAsShaderUAV(0, 0, -1, 1);
				heap1.SetParameterAsShaderUAV(0, 0, -1, 2);

				RootSignatureBuilder builder{ IN_allocator };
				builder.SetParameterAsUINT(0, 18, 0, 0);
				builder.SetParameterAsSRV(1, 0, 0);
				builder.SetParameterAsSRV(2, 1, 0);
				builder.SetParameterAsSRV(3, 2, 0);
				builder.SetParameterAsDescriptorTable(4, heap0);
				builder.SetParameterAsDescriptorTable(5, heap1);
				builder.SetParameterAsCBV(6, 1);
				builder.SetParameterAsSRV(7, 3);
				globalRoot = builder.Build(IN_renderSystem, IN_temp);

				updateState = LibraryBuilder{ IN_temp }.
					LoadShaderLibrary("assets\\shaders\\HalfEdge\\HE_AdaptiveCC.hlsl").
					//LoadShaderLibrary("assets\\shaders\\HalfEdge\\HE_CatmullClark.hlsl").
					AddGlobalRootSignature(*globalRoot).
					AddWorkGroup("HE_Builder", {}).
					BuildStateObject();

				programID		= updateState->GetProgramID("HE_Builder");
				initiate		= updateState->GetEntryPointIndex("InitiateHalfEdgeMesh");
				subdivide		= updateState->GetEntryPointIndex("SubdivideHalfEdgeMesh");

				IN_renderSystem.RegisterPSOLoader(
					BuildBisectors,
					[](FlexKit::RenderSystem* renderSystem, FlexKit::iAllocator& allocator) 
					{
						return FlexKit::PipelineBuilder{ allocator }.
							AddComputeShader("BuildBisectors", "assets\\shaders\\HalfEdge\\HE_Initialize.hlsl", { .hlsl2021 = true }).
							Build(*renderSystem);
					});

				IN_renderSystem.RegisterPSOLoader(
					BuildLevel,
					[](FlexKit::RenderSystem* renderSystem, FlexKit::iAllocator& allocator)
					{
						return FlexKit::PipelineBuilder{ allocator }.
							AddComputeShader("BuildLevel", "assets\\shaders\\HalfEdge\\HE_ComputeLevel.hlsl", { .hlsl2021 = true }).
							Build(*renderSystem);
					});

				//IN_renderSystem.RegisterPSOLoader(
				//	EdgeUpdate,
				//	[](FlexKit::RenderSystem* renderSystem, FlexKit::iAllocator& allocator)
				//	{
				//		return FlexKit::PipelineBuilder{ allocator }.
				//			AddComputeShader("EdgePass", "assets\\shaders\\HalfEdge\\HalfEdge.hlsl", { .hlsl2021 = true }).
				//			Build(*renderSystem);
				//	});
				
				IN_renderSystem.RegisterPSOLoader(
					RenderFaces,
					[](FlexKit::RenderSystem* renderSystem, FlexKit::iAllocator& allocator)
					{
						return FlexKit::PipelineBuilder{ allocator }.
							AddMeshShader("MeshMain",	"assets\\shaders\\HalfEdge\\HE_BasicForwardRender.hlsl", { .hlsl2021 = true }).
							AddPixelShader("PMain",		"assets\\shaders\\HalfEdge\\HE_BasicForwardRender.hlsl", { .hlsl2021 = true }).
							AddRasterizerState({ .fill = FlexKit::EFillMode::SOLID, .CullMode = FlexKit::ECullMode::NONE}).
							AddRenderTargetState(
								{	.targetCount	= 1,
									.targetFormats	= { FlexKit::DeviceFormat::R16G16B16A16_FLOAT } }).
							AddDepthStencilFormat(DeviceFormat::D32_FLOAT).
							AddDepthStencilState({ .depthEnable = true, .depthFunc = EComparison::LESS }).
							Build(*renderSystem);
					});

				IN_renderSystem.RegisterPSOLoader(
					RenderWireframe,
					[](FlexKit::RenderSystem* renderSystem, FlexKit::iAllocator& allocator)
					{
						return FlexKit::PipelineBuilder{ allocator }.
							AddMeshShader("WireMain",			"assets\\shaders\\HalfEdge\\HE_BasicForwardRender.hlsl", { .hlsl2021 = true }).
							AddPixelShader("WhiteWireframe",	"assets\\shaders\\HalfEdge\\HE_BasicForwardRender.hlsl", { .hlsl2021 = true }).
							AddRasterizerState({ .fill = FlexKit::EFillMode::SOLID, .CullMode = FlexKit::ECullMode::BACK }).
							AddRenderTargetState(
								{	.targetCount	= 1,
									.targetFormats	= { FlexKit::DeviceFormat::R16G16B16A16_FLOAT } }).
							AddDepthStencilFormat(DeviceFormat::D32_FLOAT).
							AddDepthStencilState({ .depthEnable = true, .depthFunc = EComparison::LESS }).
							Build(*renderSystem);
					});

				IN_renderSystem.QueuePSOLoad(BuildBisectors);
				IN_renderSystem.QueuePSOLoad(BuildLevel);
				IN_renderSystem.QueuePSOLoad(RenderWireframe);
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
					builder.Requires(BuildBisectors);
					subDivData.inputCage		= builder.NonPixelShaderResource(controlCage);
					subDivData.inputVerts		= builder.NonPixelShaderResource(controlPoints);
					subDivData.inputFaces		= builder.NonPixelShaderResource(controlFaces);
					subDivData.inputEdgeCount	= controlCageSize;
					subDivData.faceCount		= controlCageFaces;
					edgeCount[levelsBuilt]		= controlCageSize * 4;

					subDivData.outputCage		= builder.UnorderedAccess(levels[levelsBuilt]);
					subDivData.outputVerts		= builder.UnorderedAccess(points[levelsBuilt]);
					subDivData.counters			= builder.AcquireVirtualResource(GPUResourceDesc::UAVResource(512), FlexKit::DASUAV);

					levelsBuilt++;
				},
				[this](buildLevel& subDivData, ResourceHandler& resources, Context& ctx, iAllocator& threadLocalAllocator)
				{
					ctx.BeginEvent_DEBUG("Subdivision : Build Level");

					ctx.DiscardResource(resources.GetResource(subDivData.counters));
					ctx.ClearUAVBuffer(resources.UAV(subDivData.counters, ctx));

					ctx.SetComputePipelineState(BuildBisectors, threadLocalAllocator);
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
		else
		{
			frameGraph.AddOutput(levels[levelsBuilt]);
			frameGraph.AddOutput(points[levelsBuilt]);

			frameGraph.AddNode<buildLevel>(
				{},
				[&](FrameGraphNodeBuilder& builder, buildLevel& subDivData)
				{
					builder.Requires(BuildLevel);
					subDivData.inputCage		= builder.NonPixelShaderResource(levels[levelsBuilt - 1]);
					subDivData.inputVerts		= builder.NonPixelShaderResource(points[levelsBuilt - 1]);
					subDivData.inputEdgeCount	= controlCageSize;
					subDivData.faceCount		= controlCageFaces << (levelsBuilt * 2);
					edgeCount[levelsBuilt]		= edgeCount[levelsBuilt - 1] * 4;

					subDivData.outputCage		= builder.UnorderedAccess(levels[levelsBuilt]);
					subDivData.outputVerts		= builder.UnorderedAccess(points[levelsBuilt]);
					subDivData.counters			= builder.AcquireVirtualResource(GPUResourceDesc::UAVResource(512), FlexKit::DASUAV);

					levelsBuilt++;
				},
				[this](buildLevel& subDivData, ResourceHandler& resources, Context& ctx, iAllocator& threadLocalAllocator)
				{
					ctx.BeginEvent_DEBUG("Subdivision : Build Level");

					ctx.DiscardResource(resources.GetResource(subDivData.counters));
					ctx.ClearUAVBuffer(resources.UAV(subDivData.counters, ctx));

					ctx.SetComputePipelineState(BuildLevel, threadLocalAllocator);
					ctx.SetComputeUnorderedAccessView(0, resources.UAV(subDivData.outputCage, ctx));
					ctx.SetComputeUnorderedAccessView(1, resources.UAV(subDivData.outputVerts, ctx));
					ctx.SetComputeUnorderedAccessView(2, resources.UAV(subDivData.counters, ctx));
					ctx.SetComputeShaderResourceView(3, resources.NonPixelShaderResource(subDivData.inputCage, ctx));
					ctx.SetComputeShaderResourceView(4, resources.NonPixelShaderResource(subDivData.inputVerts, ctx));
					ctx.SetComputeConstantValue(5, 1, &subDivData.faceCount);
					ctx.Dispatch({ Max(1, 1), 1, 1 });

					ctx.EndEvent_DEBUG();
				});
		}
	}


	/************************************************************************************************/


	void HalfEdgeMesh::InitializeMesh(FlexKit::FrameGraph& frameGraph)
	{
		struct BuildLevels
		{
			FrameResourceHandle constantSpace		= InvalidHandle;

			FrameResourceHandle backingSpace		= InvalidHandle;
			FrameResourceHandle localRootSigSpace	= InvalidHandle;

			FrameResourceHandle inputCage	= InvalidHandle;
			FrameResourceHandle inputPoints	= InvalidHandle;
			FrameResourceHandle InputFaces	= InvalidHandle;

			FrameResourceHandle faceLookup	= InvalidHandle;
			FrameResourceHandle outputCages[3];
			FrameResourceHandle outputVerts[3];

			uint patchCount = 0;
		};

		levelsBuilt++;

		frameGraph.AddNode<BuildLevels>(
			{},
			[&](FrameGraphNodeBuilder& builder, BuildLevels& subDivData)
			{
				subDivData.inputCage	= builder.NonPixelShaderResource(controlCage);
				subDivData.inputPoints	= builder.NonPixelShaderResource(controlPoints);
				subDivData.InputFaces	= builder.NonPixelShaderResource(controlFaces);
				subDivData.patchCount	= controlCageFaces;

				for(uint32_t i = 0; i < 3; i++)
				{
					frameGraph.AddOutput(levels[i]);
					frameGraph.AddOutput(points[i]);

					subDivData.outputCages[i] = builder.UnorderedAccess(levels[i]);
					subDivData.outputVerts[i] = builder.UnorderedAccess(points[i]);
				}

				if (auto spaceRequired = updateState->GetBackingMemory(); spaceRequired)
					subDivData.backingSpace = builder.AcquireVirtualResource(GPUResourceDesc::UAVResource(spaceRequired), DASUAV);

				subDivData.localRootSigSpace	= builder.AcquireVirtualResource(GPUResourceDesc::UAVResource(1024), DASCopyDest);
				subDivData.constantSpace		= builder.AcquireVirtualResource(GPUResourceDesc::StructuredResource(1024), DASCopyDest);
			},
			[this](BuildLevels& subDivData, ResourceHandler& resources, Context& ctx, iAllocator& threadLocalAllocator)
			{
				ctx.BeginEvent_DEBUG("Initialize Half Edge Mesh");

				resources.SetDebugName(subDivData.localRootSigSpace, "localRootSigSpace");

				auto space = resources.GetDevicePointerRange(subDivData.backingSpace);

				D3D12_SET_PROGRAM_DESC setProgram;
				setProgram.WorkGraph.BackingMemory					= resources.GetDevicePointerRange(subDivData.backingSpace);
				setProgram.WorkGraph.ProgramIdentifier				= programID;
				setProgram.WorkGraph.NodeLocalRootArgumentsTable	= {	.StartAddress	= 0, 
																		.SizeInBytes	= 0, 
																		.StrideInBytes	= 0 };
				setProgram.WorkGraph.Flags							= D3D12_SET_WORK_GRAPH_FLAG_INITIALIZE;
				setProgram.Type										= D3D12_PROGRAM_TYPE_WORK_GRAPH;
				
				ctx.FlushBarriers();

				ctx.SetComputeRootSignature(globalRoot);
				ctx.SetComputeShaderResourceView(1, resources.GetResource(subDivData.inputCage));
				ctx.SetComputeShaderResourceView(2, resources.GetResource(subDivData.inputPoints));
				ctx.SetComputeShaderResourceView(3, resources.GetResource(subDivData.InputFaces));
				ctx.SetComputeConstantBufferView(6, resources.NonPixelShaderResource(subDivData.constantSpace, ctx, Sync_Copy, Sync_Compute));

				DescriptorHeap cages;
				DescriptorHeap points;
				cages.Init2(ctx, globalRoot->GetDescHeap(0), 3, threadLocalAllocator);
				points.Init2(ctx, globalRoot->GetDescHeap(1), 3, threadLocalAllocator);

				for(auto&& [idx, cage] : enumerate(subDivData.outputCages))
					cages.SetUAVStructured(ctx, idx, resources.GetResource(cage), 16);
	
				for (auto&& [idx, p] : enumerate(subDivData.outputVerts))
					points.SetUAVStructured(ctx, idx, resources.GetResource(p), sizeof(HalfEdgeVertex));

				ctx.SetComputeDescriptorTable(4, cages);	// cages
				ctx.SetComputeDescriptorTable(5, points);	// points

				ctx.DeviceContext->SetProgram(&setProgram);

				struct
				{
					const uint4		xyzw;
				}	arguments{
					.xyzw	= { 1, 1, 1, subDivData.patchCount },
				};

				ctx.FlushBarriers();

				D3D12_DISPATCH_GRAPH_DESC dispatch;
				dispatch.Mode = D3D12_DISPATCH_MODE::D3D12_DISPATCH_MODE_NODE_CPU_INPUT;
				dispatch.NodeCPUInput.EntrypointIndex		= initiate;
				dispatch.NodeCPUInput.NumRecords			= 1;
				dispatch.NodeCPUInput.pRecords				= &arguments;
				dispatch.NodeCPUInput.RecordStrideInBytes	= sizeof(arguments);
				ctx.DeviceContext->DispatchGraph(&dispatch);
				
				for (auto&& [idx, cage] : enumerate(subDivData.outputCages))
					ctx.AddUAVBarrier(resources.GetResource(cage));

				for (auto&& [idx, verts] : enumerate(subDivData.outputVerts))
					ctx.AddUAVBarrier(resources.GetResource(verts));

				ctx.EndEvent_DEBUG();
			}
		);
	}


	/************************************************************************************************/


	void HalfEdgeMesh::AdaptiveSubdivUpdate(FlexKit::FrameGraph& frameGraph, FlexKit::CameraHandle camera)
	{
		struct BuildLevels
		{
			FrameResourceHandle constantSpace		= InvalidHandle;

			FrameResourceHandle backingSpace		= InvalidHandle;
			FrameResourceHandle localRootSigSpace	= InvalidHandle;

			FrameResourceHandle inputCage	= InvalidHandle;
			FrameResourceHandle inputPoints	= InvalidHandle;
			FrameResourceHandle InputFaces	= InvalidHandle;
			FrameResourceHandle faceLookup	= InvalidHandle;

			FrameResourceHandle outputCages[3];
			FrameResourceHandle outputVerts[3];
		};

		frameGraph.AddNode<BuildLevels>(
			{},
			[&](FrameGraphNodeBuilder& builder, BuildLevels& subDivData)
			{
				subDivData.inputCage	= builder.NonPixelShaderResource(controlCage);
				subDivData.inputPoints	= builder.NonPixelShaderResource(controlPoints);
				subDivData.InputFaces	= builder.NonPixelShaderResource(controlFaces);

				for(uint32_t i = 0; i < 3; i++)
				{
					frameGraph.AddOutput(levels[i]);
					frameGraph.AddOutput(points[i]);

					subDivData.outputCages[i] = builder.UnorderedAccess(levels[i]);
					subDivData.outputVerts[i] = builder.UnorderedAccess(points[i]);
				}

				if (auto spaceRequired = updateState->GetBackingMemory(); spaceRequired)
					subDivData.backingSpace = builder.AcquireVirtualResource(GPUResourceDesc::UAVResource(spaceRequired), DASUAV);

				subDivData.faceLookup			= builder.NonPixelShaderResource(faceLookup);
				subDivData.localRootSigSpace	= builder.AcquireVirtualResource(GPUResourceDesc::UAVResource(1024), DASCopyDest);
				subDivData.constantSpace		= builder.AcquireVirtualResource(GPUResourceDesc::StructuredResource(1024), DASCopyDest);
			},
			[this, camera](BuildLevels& subDivData, ResourceHandler& resources, Context& ctx, iAllocator& threadLocalAllocator)
			{
				ctx.BeginEvent_DEBUG("Update Subdivision Levels");

				resources.SetDebugName(subDivData.localRootSigSpace, "localRootSigSpace");

				auto space = resources.GetDevicePointerRange(subDivData.backingSpace);

				D3D12_SET_PROGRAM_DESC setProgram;
				setProgram.WorkGraph.BackingMemory					= resources.GetDevicePointerRange(subDivData.backingSpace);
				setProgram.WorkGraph.ProgramIdentifier				= programID;
				setProgram.WorkGraph.NodeLocalRootArgumentsTable	= {	.StartAddress	= 0, 
																		.SizeInBytes	= 0, 
																		.StrideInBytes	= 0 };
				setProgram.WorkGraph.Flags							= D3D12_SET_WORK_GRAPH_FLAG_INITIALIZE;
				setProgram.Type										= D3D12_PROGRAM_TYPE_WORK_GRAPH;
				
				const auto constants = GetCameraConstants(camera);
				
				const auto f = GetFrustumVS(camera);

				UploadReservation upload = ctx.ReserveDirectUploadSpace(sizeof(f));
				memcpy(upload.buffer, &f, sizeof(f));

				ctx.ReserveDirectUploadSpace(upload);
				ctx.CopyBuffer(upload, resources.GetResource(subDivData.constantSpace));

				ctx.FlushBarriers();

				ctx.SetComputeRootSignature(globalRoot);
				ctx.SetComputeConstantValue(0, 16, &constants.View);
				ctx.SetComputeConstantValue(0, 1, &patchCount[0], 16);
				ctx.SetComputeConstantValue(0, 1, &controlCageFaces, 17);
				ctx.SetComputeShaderResourceView(1, resources.GetResource(subDivData.inputCage));
				ctx.SetComputeShaderResourceView(2, resources.GetResource(subDivData.inputPoints));
				ctx.SetComputeShaderResourceView(3, resources.GetResource(subDivData.InputFaces));
				ctx.SetComputeConstantBufferView(6, resources.NonPixelShaderResource(subDivData.constantSpace, ctx, Sync_Copy, Sync_Compute));
				ctx.SetComputeShaderResourceView(7, resources.GetResource(subDivData.faceLookup));

				DescriptorHeap cages;
				DescriptorHeap points;
				cages.Init2(ctx, globalRoot->GetDescHeap(0), 3, threadLocalAllocator);
				points.Init2(ctx, globalRoot->GetDescHeap(1), 3, threadLocalAllocator);

				for(auto&& [idx, cage] : enumerate(subDivData.outputCages))
					cages.SetUAVStructured(ctx, idx, resources.GetResource(cage), 16);
	
				for (auto&& [idx, p] : enumerate(subDivData.outputVerts))
					points.SetUAVStructured(ctx, idx, resources.GetResource(p), sizeof(HalfEdgeVertex));

				ctx.SetComputeDescriptorTable(4, cages); // cages
				ctx.SetComputeDescriptorTable(5, points); // points

				ctx.DeviceContext->SetProgram(&setProgram);

				const uint dispatchX = 1;// patchCount[0] / 1024 + (patchCount[0] % 1024 == 0 ? 0 : 1);
				
				struct
				{
					uint patchCount;
					uint halfEdgeCount;
				}	const arguments{
					.patchCount		= controlCageFaces,
					.halfEdgeCount	= patchCount[0]
				};

				ctx.FlushBarriers();

				D3D12_DISPATCH_GRAPH_DESC dispatch;
				dispatch.Mode = D3D12_DISPATCH_MODE::D3D12_DISPATCH_MODE_NODE_CPU_INPUT;
				dispatch.NodeCPUInput.EntrypointIndex		= subdivide;
				dispatch.NodeCPUInput.NumRecords			= 1;
				dispatch.NodeCPUInput.pRecords				= &arguments;
				dispatch.NodeCPUInput.RecordStrideInBytes	= sizeof(arguments);
				ctx.DeviceContext->DispatchGraph(&dispatch);
				
				for (auto&& [idx, cage] : enumerate(subDivData.outputCages))
					ctx.AddUAVBarrier(resources.GetResource(cage));

				for (auto&& [idx, verts] : enumerate(subDivData.outputVerts))
					ctx.AddUAVBarrier(resources.GetResource(verts));

				ctx.EndEvent_DEBUG();
			}
		);
	}


	/************************************************************************************************/


	void HalfEdgeMesh::DrawSubDivLevel_DEBUG(FrameGraph& frameGraph, CameraHandle camera, UpdateTask* update, ResourceHandle renderTarget, ResourceHandle depthTarget, uint32_t targetLevel)
	{
		if (levelsBuilt == 0)
			return;

		struct DrawLevel
		{
			FrameResourceHandle renderTarget;
			FrameResourceHandle depthTarget;
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
				visData.depthTarget		= builder.DepthTarget(depthTarget);
				visData.inputCage		= builder.NonPixelShaderResource(levels[targetLevel]);
				visData.inputVerts		= builder.NonPixelShaderResource(points[targetLevel]);
			},
			[this, camera, targetLevel](DrawLevel& visData, ResourceHandler& resources, Context& ctx, iAllocator& threadLocalAllocator)
			{
				ctx.BeginEvent_DEBUG("Draw HE Mesh");
				ctx.FlushBarriers();

				RenderTargetList renderTargets = { resources.RenderTarget(visData.renderTarget, ctx) };
				ctx.SetGraphicsPipelineState(RenderWireframe, threadLocalAllocator);
				ctx.SetGraphicsShaderResourceView(0, resources.NonPixelShaderResource(visData.inputCage, ctx, Sync_Compute, Sync_All));
				ctx.SetGraphicsShaderResourceView(1, resources.NonPixelShaderResource(visData.inputVerts, ctx, Sync_Compute, Sync_All));

				struct {
					float4x4_GPU	PV;
					uint32_t		patchCount;
				}	constants{
						.PV			= GetCameraConstants(camera).PV,
						.patchCount = patchCount[targetLevel]
				};

				ctx.SetGraphicsConstantValue(2, 17, &constants);
				ctx.SetScissorAndViewports(renderTargets);
				ctx.SetRenderTargets(renderTargets, true, resources.GetResource(visData.depthTarget));
				ctx.DispatchMesh({ patchCount[targetLevel] / 32 + (patchCount[targetLevel] % 32 == 0 ? 0 : 1), 1, 1 });

				ctx.EndEvent_DEBUG();
			});
	}


}	/************************************************************************************************/


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