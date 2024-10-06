#include <Application.hpp>
#include <Win32Graphics.hpp>
#include "TestComponent.hpp"
#include <print>
#include <atomic>
#include <CBT.hpp>

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


struct CBTTerrainState : FlexKit::FrameworkState
{
	CBTTerrainState(FlexKit::GameFramework& in_framework) :
		FlexKit::FrameworkState	{ in_framework },
		testComponent			{ in_framework.core.GetBlockMemory() },
		complexComponent		{ in_framework.core.GetBlockMemory() },
		tree					{ in_framework.GetRenderSystem(), in_framework.core.GetBlockMemory() },
		runOnce					{ in_framework.core.GetBlockMemory() }
	{
		renderWindow = FlexKit::CreateWin32RenderWindow(framework.GetRenderSystem(), 
			{
				.height = 600,
				.width	= 800,
			});

		FlexKit::EventNotifier<>::Subscriber sub;
		sub.Notify	= &FlexKit::EventsWrapper;
		sub._ptr	= &framework;
		renderWindow->Handler.Subscribe(sub);
		renderWindow->SetWindowTitle("HelloWorld");

		vertexBuffer = framework.GetRenderSystem().CreateVertexBuffer(512, false);

		framework.GetRenderSystem().RegisterPSOLoader(FlexKit::DRAW_PSO,
			[](FlexKit::RenderSystem* renderSystem, FlexKit::iAllocator& allocator)
			{
				return FlexKit::CreateDrawTriStatePSO(renderSystem, allocator);
			});

		tree.Initialize({ .maxDepth = 4 });
		uint32_t heapIndex = tree.BitToHeapIndex(0);


		tree.SetBit(0, true);
		tree.SetBit(1, false);
		tree.SetBit(2, true);
		tree.SetBit(3, false);
		tree.SetBit(4, true);
		tree.SetBit(5, true);
		tree.SetBit(6, true);
		tree.SetBit(7, false);
		tree.SetBit(8, true);
		tree.SetBit(9, false);
		tree.SetBit(10, true);
		tree.SetBit(11, true);
		tree.SetBit(12, true);
		tree.SetBit(13, true);
		tree.SetBit(14, true);
		tree.SetBit(15, false);

		auto bit0 = tree.GetHeapValue(16);

		auto a = tree.GetBitOffset(16);
		auto b = tree.GetBitOffset(8);
		auto c = tree.GetBitOffset(4);
		auto d = tree.GetBitOffset(2);
		auto e = tree.GetBitOffset(1);

		tree.SumReduction();
		auto res0 = tree.GetHeapValue(1);
		auto res1 = tree.GetHeapValue(2);
		auto res2 = tree.GetHeapValue(3);
		auto res3 = tree.GetHeapValue(4);
		auto res4 = tree.GetHeapValue(5);
		auto res5 = tree.GetHeapValue(6);
		auto res6 = tree.GetHeapValue(7);
		auto res7 = tree.GetHeapValue(8);
		auto res8 = tree.GetHeapValue(9);
		auto res9 = tree.GetHeapValue(10);
		auto res10 = tree.GetHeapValue(11);
		auto res11 = tree.GetHeapValue(12);
		auto res12 = tree.GetHeapValue(13);
		auto res13 = tree.GetHeapValue(14);
		auto res14 = tree.GetHeapValue(15);

		auto n1 = tree.DecodeNode(0);
		auto n2 = tree.DecodeNode(1);
		auto n3 = tree.DecodeNode(2);
		auto n4 = tree.DecodeNode(3);
		auto n5 = tree.DecodeNode(4);
		auto n6 = tree.DecodeNode(5);
		auto n7 = tree.DecodeNode(6);
		auto n8 = tree.DecodeNode(7);
		auto n9 = tree.DecodeNode(8);
		auto n10 = tree.DecodeNode(9);
		auto n11 = tree.DecodeNode(10);

		runOnce.push_back([&](FlexKit::FrameGraph& frameGraph) 
			{
				tree.Upload(frameGraph);
			});

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
			});
	}


	void DebugVisCBTTree(FlexKit::UpdateTask* update, FlexKit::EngineCore& core, FlexKit::UpdateDispatcher& dispatcher, double dT, FlexKit::FrameGraph& frameGraph)
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
				builder.Requires(FlexKit::DRAW_PSO);
				debugVis.renderTarget = builder.RenderTarget(renderWindow->GetBackBuffer());
				debugVis.cbtBuffer = builder.UnorderedAccess(tree.GetBuffer());
			},
			[&](CBTDebugVis& debugVis, FlexKit::ResourceHandler& resources, FlexKit::Context& ctx, FlexKit::iAllocator& threadLocalAllocator)
			{
				struct {
					uint32_t maxDepth;
				} constants{
					.maxDepth = tree.GetMaxDepth()
				};
				
				ctx.SetComputePipelineState(FlexKit::UpdateCBTTree, threadLocalAllocator);
				ctx.SetComputeUnorderedAccessView(0, resources.UAV(debugVis.cbtBuffer, ctx));
				ctx.SetComputeConstantValue(1, sizeof(constants) / 4, &constants);
				ctx.Dispatch({ 1, 1, 1 });
				
				ctx.AddUAVBarrier(resources.UAV(debugVis.cbtBuffer, ctx));
				ctx.SetGraphicsPipelineState(FlexKit::DrawCBTTree, threadLocalAllocator);
				ctx.SetGraphicsUnorderedAccessView(0, resources.UAV(debugVis.cbtBuffer, ctx));
				ctx.SetGraphicsConstantValue(1, sizeof(constants) / 4, &constants);
				
				FlexKit::RenderTargetList renderTargets = { resources.RenderTarget(debugVis.renderTarget, ctx) };
				ctx.SetScissorAndViewports(renderTargets);
				ctx.SetInputPrimitive(FlexKit::EInputPrimitive::INPUTPRIMITIVETRIANGLELIST);
				ctx.SetRenderTargets(renderTargets);
				ctx.Draw(3);
			});
	}


	FlexKit::UpdateTask* Draw(FlexKit::UpdateTask* update, FlexKit::EngineCore& core, FlexKit::UpdateDispatcher& dispatcher, double dT, FlexKit::FrameGraph& frameGraph) override
	{ 
		frameGraph.AddOutput(renderWindow->GetBackBuffer());

		FlexKit::ClearBackBuffer(frameGraph, renderWindow->GetBackBuffer());


		runOnce.Process(frameGraph);

		tree.Update(frameGraph);
		//gpuMemoryManager.DrawDebugVIS(frameGraph, renderWindow->GetBackBuffer());

		DebugVisCBTTree(nullptr, core, dispatcher, dT, frameGraph);

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

	FlexKit::GameObject			gameObjects[512];

	FlexKit::CBTBuffer				tree;
	TestComponent					testComponent;
	TestMultiFieldComponent			complexComponent;

	FlexKit::VertexBufferHandle	vertexBuffer;
	FlexKit::Win32RenderWindow* renderWindow = nullptr;
};

int main()
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
