#include "TestComponent.hpp"
#include "HalfEdgeMesh.hpp"

#include <Application.hpp>
#include <atomic>
#include <CameraUtilities.hpp>
#include <filesystem>
#include <ModifiableShape.hpp>
#include <MemoryUtilities.hpp>
#include <print>
#include <ranges>
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


struct CBTTerrainState : FlexKit::FrameworkState
{
	CBTTerrainState(FlexKit::GameFramework& in_framework) :
		FlexKit::FrameworkState	{ in_framework },
		testComponent			{ in_framework.core.GetBlockMemory() },
		complexComponent		{ in_framework.core.GetBlockMemory() },
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


		constantBuffer	= renderSystem.CreateConstantBuffer(MEGABYTE, false);
		vertexBuffer	= renderSystem.CreateVertexBuffer(512, false);
		renderSystem.RegisterPSOLoader(FlexKit::DRAW_PSO, FlexKit::CreateDrawTriStatePSO);

		runOnce.push_back([&](FlexKit::FrameGraph& frameGraph)
			{
			});

		ModifiableShape shape{};

		const uint32_t face0[] = {
			shape.AddVertex({   0.0f, -0.9f, 0.0f }),
			shape.AddVertex({  -1.9f,  0.0f, 0.0f }),
			shape.AddVertex({   0.0f,  0.9f, 0.0f }) 
		};
		const uint32_t face1[] = {
			face0[0], face0[2],
			shape.AddVertex({  0.9f,  0.9f, 0.0f }),
			shape.AddVertex({  0.9f, -0.9f, 0.0f })
		};

		const uint32_t face2[] = {
			face0[0], 
			face1[3],
			shape.AddVertex({  0.0f, -1.0f, 0.0f }),
		};

		shape.AddPolygon(face0, face0 + 3);
		shape.AddPolygon(face1, face1 + 4);
		//shape.AddPolygon(face2, face2 + 3);

		HEMesh = std::make_unique<HalfEdgeMesh>(
							shape,
							framework.GetRenderSystem(), 
							framework.core.GetBlockMemory(), 
							framework.core.GetTempMemory());

		runOnce.push_back(
			[&](FlexKit::FrameGraph& frameGraph) 
			{
				HEMesh->BuildAllSubDivLevel(frameGraph);
			});

		auto& cameraNode	= camera.AddView<SceneNodeView>();
		auto& orbitCamera	= camera.AddView<OrbitCameraBehavior>();

		orbitCamera.acceleration = 500.0f;
		orbitCamera.TranslateWorld({  0.0f, 0.0f, 5.0f });
		orbitCamera.SetCameraFOV(0.523599);
		orbitCamera.SetCameraAspectRatio(1920.0f / 1080.0f);

		activeCamera = orbitCamera;
	}


	/************************************************************************************************/


	FlexKit::UpdateTask* Update(FlexKit::EngineCore& core, FlexKit::UpdateDispatcher&, double dT) override
	{ 
		FlexKit::UpdateInput();
		renderWindow->UpdateCapturedMouseInput(dT);

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


	FlexKit::UpdateTask* Draw(FlexKit::UpdateTask* update, FlexKit::EngineCore& core, FlexKit::UpdateDispatcher& dispatcher, double dT, FlexKit::FrameGraph& frameGraph) override
	{ 
		using namespace FlexKit;

		frameGraph.AddOutput(renderWindow->GetBackBuffer());
		frameGraph.AddMemoryPool(poolAllocator);

		auto constBufferAllocator	= CreateConstantBufferReserveObject(constantBuffer, core.RenderSystem, core.GetTempMemory());
		auto depthTarget			= depthBuffer.Get();

		ClearBackBuffer(frameGraph, renderWindow->GetBackBuffer());
		ClearDepthBuffer(frameGraph, depthTarget, 1.0f);

		runOnce.Process(frameGraph);

		//gpuMemoryManager.DrawDebugVIS(frameGraph, renderWindow->GetBackBuffer());
		
		//Pitch(camera, -dT * FlexKit::pi / 4.0f);
		MarkCameraDirty(activeCamera);

		auto& transformUpdate	= QueueTransformUpdateTask(dispatcher);
		auto& cameraUpdate		= cameras.QueueCameraUpdate(dispatcher);

		cameraUpdate.AddInput(transformUpdate);

		if (auto orbitCamera = camera.GetView<FlexKit::OrbitCameraBehavior>(); orbitCamera)
			transformUpdate.AddInput(QueueOrbitCameraUpdateTask(dispatcher, *orbitCamera, renderWindow->mouseState, dT));

		if(HEMesh && activeCamera != FlexKit::InvalidHandle)
			HEMesh->DrawSubDivLevel_DEBUG(frameGraph, activeCamera, &cameraUpdate, renderWindow->GetBackBuffer());

		PresentBackBuffer(frameGraph, renderWindow->GetBackBuffer());

		return nullptr; 
	}


	/************************************************************************************************/


	void PostDrawUpdate(FlexKit::EngineCore& core, double dT) override
	{
		core.RenderSystem.ResetConstantBuffer(constantBuffer);
		depthBuffer.Increment();
		renderWindow->Present(1);
	}


	/************************************************************************************************/


	bool EventHandler(FlexKit::Event evt) 
	{
		switch (evt.InputSource)
		{
		case FlexKit::Event::E_SystemEvent:
			if(evt.Action == FlexKit::Event::InputAction::Exit)
				framework.quit = true;
			break;
		case FlexKit::Event::Keyboard:
		{
			switch(evt.mData1.mKC[0])
			{
			case FlexKit::KC_SPACE:
				if (evt.Action == FlexKit::Event::Release)
				{
					return true;
				}
				break;
			case FlexKit::KC_M:
				if (evt.Action == FlexKit::Event::Release)
				{
					renderWindow->ToggleMouseCapture();
					return true;
				}
			case FlexKit::KC_P:
				if (evt.Action == FlexKit::Event::Release)
				{
					adaptiveLOD = !adaptiveLOD;
					return true;
				}
				break;
			case FlexKit::KC_W:
			case FlexKit::KC_A:
			case FlexKit::KC_S:
			case FlexKit::KC_D:
			case FlexKit::KC_Q:
			case FlexKit::KC_E:
			{
				if (auto orbiter = camera.GetView<FlexKit::OrbitCameraBehavior>(); orbiter)
				{
					auto remappedEvt = evt;
					return orbiter->HandleEvent(remappedEvt);
				}
			}	break;
			}
		}	break;
		default:
		{
		}	break;
		}

		return false; 
	}

	FlexKit::RunOnceQueue<void (FlexKit::FrameGraph&)>	runOnce;

	FlexKit::MemoryPoolAllocator	poolAllocator;
	FlexKit::VertexBufferHandle		vertexBuffer;
	FlexKit::ConstantBufferHandle	constantBuffer;
	FlexKit::Win32RenderWindow*		renderWindow = nullptr;
	FlexKit::DepthBuffer			depthBuffer;

	FlexKit::CameraComponent		cameras;
	FlexKit::SceneNodeComponent		nodes;
	FlexKit::TriggerComponent		triggers;

	TestComponent					testComponent;
	TestMultiFieldComponent			complexComponent;

	std::unique_ptr<FlexKit::HalfEdgeMesh>	HEMesh;

	FlexKit::CameraHandle		activeCamera = FlexKit::InvalidHandle;
	FlexKit::GameObject			gameObjects[512];
	FlexKit::GameObject			camera;

	bool	adaptiveLOD		= true;
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
