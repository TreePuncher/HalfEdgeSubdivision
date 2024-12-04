#pragma once
#include "CBT.hpp"
#include <FrameGraph.hpp>
#include <Graphics.hpp>
#include <ModifiableShape.hpp>
#include <LibraryBuilder.hpp>

namespace FlexKit
{
	struct HEEdge
	{
		uint32_t twin;
		uint32_t next;
		uint32_t prev;
		uint32_t vert;
	};

	struct HE_Face
	{
		uint32_t begin;
		uint32_t vertexRange;
		uint16_t edgeCount;
		uint16_t level;

		uint32_t GetVertexCount()
		{
			return 1 + 2 * edgeCount;
		}
	};

	struct HEVertex
	{
		float3 point;
		float2 UV;
	};


	struct HalfEdgeMesh
	{
		struct HalfEdgeVertex
		{
			float	xyz[3];
			uint4_8	rgba;
			float2	UV;
		};

		HalfEdgeMesh(
			const	ModifiableShape&	shape,
					RenderSystem&		IN_renderSystem, 
					iAllocator&			IN_allocator, 
					iAllocator&			IN_temp);


		~HalfEdgeMesh();


		void InitializeMesh(FlexKit::FrameGraph& frameGraph);
		void BuildSubDivLevel(FlexKit::FrameGraph& frameGraph);
		void AdaptiveSubdivUpdate(FlexKit::FrameGraph& frameGraph, FlexKit::CameraHandle camera);

		
		/************************************************************************************************/


		void DrawSubDivLevel_DEBUG(FrameGraph& frameGraph, CameraHandle camera, UpdateTask* update, ResourceHandle renderTarget, ResourceHandle depthTarget, uint32_t targetLevel = 0);

		static constexpr PSOHandle EdgeUpdate		= PSOHandle{ GetTypeGUID(HEEdgeUpdate) };
		static constexpr PSOHandle BuildBisectors	= PSOHandle{ GetTypeGUID(HEBuildBisectors) };
		static constexpr PSOHandle BuildLevel		= PSOHandle{ GetTypeGUID(HEBuildLevel) };
		static constexpr PSOHandle FacePass			= PSOHandle{ GetTypeGUID(HEFacePass) };
		static constexpr PSOHandle VertexUpdate		= PSOHandle{ GetTypeGUID(HEVertexUpdate) };
		static constexpr PSOHandle RenderFaces		= PSOHandle{ GetTypeGUID(HERenderFaces) };
		static constexpr PSOHandle RenderWireframe	= PSOHandle{ GetTypeGUID(RenderWireframe) };
		
		inline static GPUStateObject_ptr	updateState		= nullptr;
		inline static RootSignature*		globalRoot		= nullptr;
		inline static uint32_t				initiate		= -1;
		inline static uint32_t				subdivide		= -1;
		inline static ProgramIdentifier		programID;

		uint32_t			controlCageSize		= 0;
		uint32_t			controlCageFaces	= 0;
		ResourceHandle		controlFaces		= InvalidHandle;
		ResourceHandle		controlCage			= InvalidHandle;
		ResourceHandle		controlPoints		= InvalidHandle;
		ResourceHandle		faceLookup			= InvalidHandle;
		ResourceHandle		levels[3]			= { InvalidHandle, InvalidHandle, InvalidHandle };
		ResourceHandle		points[3]			= { InvalidHandle, InvalidHandle, InvalidHandle };
		uint32_t			edgeCount[3]		= { 0, 0, 0 };
		uint32_t			patchCount[3]		= { 0, 0, 0 };
		uint8_t				levelsBuilt			= 0;
		CBTBuffer			cbt;
	};

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
