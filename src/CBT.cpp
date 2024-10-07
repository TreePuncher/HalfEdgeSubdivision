#include <CBT.hpp>
#include <FrameGraph.hpp>
#include <graphics.hpp>
#include <Type.hpp>
#include <bit>


namespace FlexKit
{	/************************************************************************************************/


	uint64_t FindMSB(uint64_t n) noexcept
	{
#ifdef WIN32
		if(std::is_constant_evaluated())
		{
			return std::bit_ceil(n);
		}
		else
		{
			unsigned long index = n;
			_BitScanReverse64(&index, n);
			return index;
		}
#else
		return std::bit_ceil(n);
#endif
	}


	/************************************************************************************************/


	uint64_t FindLSB(uint64_t n) noexcept
	{
#ifdef WIN32
		if(std::is_constant_evaluated())
		{
			return std::bit_floor(n);
		}
		else
		{
			unsigned long index = 0;
			_BitScanForward64(&index, n) + 1;
			return index;
		}
#else
		return std::bit_floor(n);
#endif
	}


	/************************************************************************************************/

	uint32_t ipow(uint32_t base, uint32_t exp) noexcept
	{
		uint result = 1;
		for (;;)
		{
			if (exp & 1)
				result *= base;
			exp >>= 1;
			if (!exp)
				break;
			base *= base;
		}

		return result;
	}

	/************************************************************************************************/


	CBTBuffer::CBTBuffer(RenderSystem& IN_renderSystem, iAllocator& IN_persistent) :
		renderSystem	{ IN_renderSystem	},
		bitField		{ IN_persistent		}
	{
		states.Initialize(renderSystem);
	}


	/************************************************************************************************/


	CBTBuffer::~CBTBuffer()
	{
		renderSystem.ReleaseResource(buffer);
	}


	/************************************************************************************************/


	void CBTBuffer::PipelineStates::_InitializeStates(PipelineStates& states, RenderSystem& renderSystem)
	{
		renderSystem.RegisterPSOLoader(
			UpdateCBTTree, [](RenderSystem* renderSystem, iAllocator& allocator) -> LoadPipelineStateRes
			{
				return PipelineBuilder{ allocator }.
						AddComputeShader("UpdateCBT", "assets\\shaders\\cbt\\CBT.hlsl", { .hlsl2021 = true }).
						Build(*renderSystem);
			});

		renderSystem.RegisterPSOLoader(
			DrawCBTTree, [](RenderSystem* renderSystem, iAllocator& allocator) -> LoadPipelineStateRes
			{
				return PipelineBuilder{ allocator }.
						AddInputTopology(ETopology::EIT_TRIANGLE).
						AddVertexShader("DrawCBT_VS", "assets\\shaders\\cbt\\CBT.hlsl").
						AddPixelShader("DrawCBT_PS", "assets\\shaders\\cbt\\CBT.hlsl").
						AddRasterizerState({ .fill = EFillMode::SOLID, .CullMode = ECullMode::NONE }).
						AddRenderTargetState(
							{	.targetCount	= 1, 
								.targetFormats	= { DeviceFormat::R16G16B16A16_FLOAT } }).
						Build(*renderSystem);
			});

		renderSystem.QueuePSOLoad(UpdateCBTTree);
	}


	/************************************************************************************************/


	void CBTBuffer::PipelineStates::QueueReload(RenderSystem& renderSystem)
	{
		renderSystem.QueuePSOLoad(UpdateCBTTree);
	}


	/************************************************************************************************/


	void CBTBuffer::PipelineStates::Initialize(RenderSystem& renderSystem)
	{
		init(*this, renderSystem);
	}


	/************************************************************************************************/


	void CBTBuffer::ReloadShaders()
	{
		states.QueueReload(renderSystem);
	}


	/************************************************************************************************/


	void CBTBuffer::Initialize(const CBTBufferDescription& description)
	{
		if (buffer)
			renderSystem.ReleaseResource(buffer);

		auto bufferSize = GetCBTSizeBytes(description.maxDepth, description.cbtTreeCount);
		buffer			= renderSystem.CreateGPUResource(GPUResourceDesc::UAVResource(bufferSize));
		maxDepth		= description.maxDepth;

		bitField.resize(Max(1, bufferSize / sizeof(uint64_t)));
		memset(bitField.data(), 0x00, bitField.ByteSize());
	}


	/************************************************************************************************/


	void CBTBuffer::Update(FrameGraph& frameGraph)
	{
		struct InitializeCBTree
		{
			FrameResourceHandle buffer;
		};

		frameGraph.AddNode<InitializeCBTree>(
			{},
			[&](FrameGraphNodeBuilder& builder, InitializeCBTree& args)
			{
				args.buffer = builder.UnorderedAccess(buffer);
			},
			[](InitializeCBTree& args, ResourceHandler& handler, Context& ctx, iAllocator& allocator)
			{
				uint32_t bufferSize = 32;

				ctx.SetComputePipelineState(UpdateCBTTree, allocator);
				ctx.SetComputeUnorderedAccessView(0, handler.UAV(args.buffer, ctx));
				ctx.SetComputeConstantValue(1, 1, &bufferSize);
				ctx.Dispatch({ 1, 1, 1 });
			}
		);
	}


	/************************************************************************************************/


	void CBTBuffer::Upload(FrameGraph& frameGraph)
	{
		struct InitializeCBTree
		{
			FrameResourceHandle buffer;
		};

		frameGraph.AddNode<InitializeCBTree>(
			{},
			[&](FrameGraphNodeBuilder& builder, InitializeCBTree& args)
			{
				args.buffer = builder.CopyDest(buffer);
			},
			[this](InitializeCBTree& args, ResourceHandler& handler, Context& ctx, iAllocator& allocator)
			{
				auto upload = ctx.ReserveDirectUploadSpace(bitField.ByteSize());
				memcpy(upload.buffer, bitField.data(), bitField.ByteSize());
				ctx.CopyBuffer(upload, handler.CopyDest(args.buffer, ctx));
			}
		);
	}


	/************************************************************************************************/


	void CBTBuffer::Clear(FrameGraph& frameGraph)
	{
		struct InitializeCBTree
		{
			FrameResourceHandle buffer;
		};

		frameGraph.AddNode<InitializeCBTree>(
			{},
			[&](FrameGraphNodeBuilder& builder, InitializeCBTree& args)
			{
				args.buffer = builder.UnorderedAccess(buffer);
			},
			[](InitializeCBTree& args, ResourceHandler& handler, Context& ctx, iAllocator& allocator)
			{
				uint32_t bufferSize = 32;

				ctx.ClearUAVBufferRange(handler.UAV(args.buffer, ctx), 0, bufferSize / 32);
			}
		);
	}


	/************************************************************************************************/


	void CBTMemoryManager::Update(FrameGraph& frameGraph)
	{
		cbt.Update(frameGraph);
	}

	void CBTMemoryManager::DrawDebugVIS(class FrameGraph&, ResourceHandle renderTarget)
	{

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
