#include "HalfEdgeMesh.hpp"
#include <LibraryBuilder.hpp>
#include <Containers.hpp>


namespace FlexKit
{	/************************************************************************************************/


	struct RegularQuadStencil
	{
		uint4 points[4];
	};


	RegularQuadStencil CreateStencil(uint32_t faceIdx, const ModifiableShape& shape) noexcept
	{
		RegularQuadStencil stencil;
		memset(&stencil, 0, sizeof(stencil));

		const auto& face = shape.wFaces[faceIdx];
		ConstFaceIterator face0{ &shape, face.edgeStart };

		auto v11 = face0.Edge().vertices[0];
		auto v12 = face0.Edge().vertices[1];
		auto v22 = ((face0 + 1).Edge().vertices[1]);
		auto v21 = ((face0 + 2).Edge().vertices[1]);

		// Center Quad
		stencil.points[1][1] = face0.Edge().vertices[0];
		stencil.points[1][2] = face0.Edge().vertices[1];
		stencil.points[2][2] = (face0 + 2).Edge().vertices[0];
		stencil.points[2][1] = (face0 + 2).Edge().vertices[1];

		uint2 outputStencils[] = { 
			uint2{ 0, 1 }, uint2{ 0, 0 }, uint2{ 1, 0 },
			uint2{ 1, 3 }, uint2{ 0, 3 }, uint2{ 0, 2 },
			uint2{ 3, 2 }, uint2{ 3, 3 }, uint2{ 2, 3 },
			uint2{ 2, 0 }, uint2{ 3, 0 }, uint2{ 3, 1 },
		};

		for (int i = 0; i < 4; i++)
		{
			auto e0 = RotateEdgeSelectorCCW((face0 + i).current, shape);
			auto e1 = RotateEdgeSelectorCW(Twin(e0, shape), shape);
			auto e2 = PrevEdge(e1, shape);

			auto e1_v = shape.wEdges[e2].vertices;

			const uint v00 = shape.wEdges[e1].vertices[0];
			const uint v01 = shape.wEdges[e0].vertices[1];
			const uint v10 = shape.wEdges[e2].vertices[0];

			const uint2 a = outputStencils[3 * i + 0];
			const uint2 b = outputStencils[3 * i + 1];
			const uint2 c = outputStencils[3 * i + 2];

			stencil.points[a[0]][(size_t)a[1]] = v01;
			stencil.points[b[0]][(size_t)b[1]] = v00;
			stencil.points[c[0]][(size_t)c[1]] = v10;
		}

		return stencil;
	}


	HalfEdgeMesh::HalfEdgeMesh(
			const	ModifiableShape&	shape,
					RenderSystem&		IN_renderSystem, 
					iAllocator&			IN_allocator, 
					iAllocator&			IN_temp)	
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
		Vector<RegularQuadStencil> regularQuads{ IN_allocator };

		for (const auto&& [idx, face] : enumerate(shape.wFaces))
		{ 
			if (IsRegularQuad(idx, shape))
			{
				RegularQuadStencil quadStencil = CreateStencil(idx, shape);
				regularQuads.push_back(quadStencil);
			}
		}

		
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

		quadBuffer		= IN_renderSystem.CreateGPUResource(GPUResourceDesc::StructuredResource(regularQuads.ByteSize()));
		vertexBuffer	= IN_renderSystem.CreateGPUResource(GPUResourceDesc::StructuredResource(meshPoints.ByteSize()));

		IN_renderSystem.SetDebugName(quadBuffer,	"QuadBuffer");
		IN_renderSystem.SetDebugName(vertexBuffer,	"VertexBuffer");

		auto uploadQueue = IN_renderSystem.GetImmediateCopyQueue();
		IN_renderSystem.UpdateResourceByUploadQueue(
			IN_renderSystem.GetDeviceResource(quadBuffer),
			uploadQueue,
			regularQuads.data(),
			regularQuads.ByteSize(), 1, FlexKit::DASNonPixelShaderResource);

		IN_renderSystem.UpdateResourceByUploadQueue(
			IN_renderSystem.GetDeviceResource(vertexBuffer),
			uploadQueue,
			meshPoints.data(),
			meshPoints.ByteSize(), 1, FlexKit::DASNonPixelShaderResource);

		static bool registerStates =
			[&](RenderSystem& renderSystem) -> bool
			{
				renderSystem.RegisterPSOLoader(
					DrawRegularQuadFace, 
					[&](RenderSystem* renderSystem, iAllocator& allocator)
					{
						return PipelineBuilder{ allocator }.
							AddInputTopology(ETopology::EIT_PATCH).
							AddInputLayout({
									.inputs = 
										{	
									EInputElement{ 
										.name	= "POSITION", 
										.index	= 0, 
										.format	= DeviceFormat::R32G32B32_FLOAT
								} }, 
									.count = 1 }).
							AddVertexShader("VMain",	R"(assets\shaders\halfedge\FACC.hlsl)").
							AddHullShader("HMain",		R"(assets\shaders\halfedge\FACC.hlsl)").
							AddDomainShader("DMain",	R"(assets\shaders\halfedge\FACC.hlsl)").
							AddPixelShader("PMain",		R"(assets\shaders\halfedge\FACC.hlsl)").
							AddRenderTargetState({
								.targetCount = 1,
								.targetFormats = { DeviceFormat::R16G16B16A16_FLOAT },
								}).
							AddRasterizerState({.fill = EFillMode::WIREFRAME, .CullMode = ECullMode::NONE }).
							Build(*renderSystem);
					});

				renderSystem.QueuePSOLoad(DrawRegularQuadFace);
				return true;
			}(IN_renderSystem);
	}


	/************************************************************************************************/


	HalfEdgeMesh::~HalfEdgeMesh()
	{
		RenderSystem::_GetInstance().ReleaseResource(vertexBuffer);
		RenderSystem::_GetInstance().ReleaseResource(quadBuffer);
	}


	/************************************************************************************************/


	void HalfEdgeMesh::InitializeMesh(FlexKit::FrameGraph& frameGraph)
	{
		struct BuildLevels
		{
		};

		frameGraph.AddNode<BuildLevels>(
			{},
			[&](FrameGraphNodeBuilder& builder, BuildLevels& subDivData)
			{
			},
			[this](BuildLevels& subDivData, ResourceHandler& resources, Context& ctx, iAllocator& threadLocalAllocator)
			{
				ctx.BeginEvent_DEBUG("Initialize Half Edge Mesh");
				ctx.EndEvent_DEBUG();
			}
		);
	}


	/************************************************************************************************/

	struct AdaptiveUpdate
	{
	};


	AdaptiveUpdate& HalfEdgeMesh::AdaptiveSubdivUpdate(FrameGraph& frameGraph, CameraHandle camera)
	{
		return frameGraph.AddNode<AdaptiveUpdate>(
			{},
			[&](FrameGraphNodeBuilder& builder, AdaptiveUpdate& subDivData)
			{
			},
			[this, camera](AdaptiveUpdate& subDivData, ResourceHandler& resources, Context& ctx, iAllocator& threadLocalAllocator)
			{
			}
		);
	}


	/************************************************************************************************/


	void HalfEdgeMesh::DrawSubDivLevel_DEVEL(FrameGraph& frameGraph, CameraHandle camera, UpdateTask* update, ResourceHandle renderTarget, ResourceHandle depthTarget, AdaptiveUpdate& subDivTask)
	{
		struct DrawLevel
		{
			FrameResourceHandle renderTarget;
			FrameResourceHandle depthTarget;
		};

		frameGraph.AddNode<DrawLevel>(
			{},
			[&](FrameGraphNodeBuilder& builder, DrawLevel& visData)
			{
				if (update)
					builder.AddDataDependency(*update);

				visData.renderTarget	= builder.RenderTarget(renderTarget);
				visData.depthTarget		= builder.DepthTarget(depthTarget);

				builder.Requires(DrawRegularQuadFace);
			},
			[this, camera](DrawLevel& visData, ResourceHandler& resources, Context& ctx, iAllocator& threadLocalAllocator)
			{
				ctx.BeginEvent_DEBUG("Draw HE Mesh");

				auto constants = GetCameraConstants(camera);

				ctx.SetGraphicsPipelineState(DrawRegularQuadFace, threadLocalAllocator);
				ctx.SetInputPrimitive(EInputPrimitive::INPUTPRIMITIVEPATCH_CP_16);
				ctx.SetIndexBuffer(quadBuffer, DeviceFormat::R32_UINT);
				ctx.SetVertexBuffers({ VertexBufferResource{.resource = vertexBuffer, .stride = sizeof(HalfEdgeVertex)}});
				ctx.SetRenderTargets({ resources.GetResource(visData.renderTarget) }, true, resources.GetResource(visData.depthTarget));
				ctx.SetScissorAndViewports({ resources.GetResource(visData.renderTarget) });
				ctx.SetGraphicsConstantValue(5, 16, &constants.PV);
				ctx.DrawIndexed(16, 0, 0);

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