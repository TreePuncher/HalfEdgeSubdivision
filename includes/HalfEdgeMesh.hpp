#pragma once
#include "CBT.hpp"
#include <FrameGraph.hpp>
#include <Graphics.hpp>
#include <ModifiableShape.hpp>

namespace FlexKit
{

	struct HEEdge
	{
		uint32_t twin;
		uint32_t next;
		uint32_t prev;
		uint32_t vert;
	};


	struct HEVertex
	{
		float3 point;
		float2 UV;
	};


	struct HalfEdgeMesh
	{
		struct XYZ
		{
			float xyz[3];
		};


		HalfEdgeMesh(
			const	ModifiableShape&	shape,
					RenderSystem&		IN_renderSystem, 
					iAllocator&		IN_allocator);


		~HalfEdgeMesh();


		void BuildSubDivLevel(FlexKit::FrameGraph& frameGraph);


		/************************************************************************************************/


		void DrawSubDivLevel_DEBUG(FrameGraph& frameGraph, CameraHandle camera, UpdateTask* update, ResourceHandle renderTarget);

		static constexpr PSOHandle EdgeUpdate		= PSOHandle{ GetTypeGUID(HEEdgeUpdate) };
		static constexpr PSOHandle FaceInitiate		= PSOHandle{ GetTypeGUID(HEFaceInitiate) };
		static constexpr PSOHandle FacePass			= PSOHandle{ GetTypeGUID(HEFacePass) };
		static constexpr PSOHandle VertexUpdate		= PSOHandle{ GetTypeGUID(HEVertexUpdate) };
		static constexpr PSOHandle RenderFaces		= PSOHandle{ GetTypeGUID(HERenderFaces) };


		uint32_t			controlCageSize		= 0;
		uint32_t			controlCageFaces	= 0;
		ResourceHandle		controlFaces		= InvalidHandle;
		ResourceHandle		controlCage			= InvalidHandle;
		ResourceHandle		controlPoints		= InvalidHandle;
		ResourceHandle		levels[2]			= { InvalidHandle, InvalidHandle };
		ResourceHandle		points[2]			= { InvalidHandle, InvalidHandle };
		uint32_t			edgeCount[2]		= { 0, 0 };
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
