#pragma once
#include <Graphics.hpp>
#include <Containers.hpp>
#include <string_view>

namespace FlexKit
{

	struct ProgramIdentifier
	{
		D3D12_PROGRAM_IDENTIFIER id;

		//ProgramIdentifier& operator = (const D3D12_PROGRAM_IDENTIFIER& IN_id) { id = IN_id; }

		operator D3D12_PROGRAM_IDENTIFIER () { return id; }
	};

	class GPUStateObject : NoCopy
	{
	public:
		GPUStateObject(
			ID3D12StateObject*				IN_stateObject, 
			ID3D12StateObjectProperties1*	IN_properties, 
			ID3D12WorkGraphProperties*		IN_workGraphProperties) :
			stateObject			{ IN_stateObject			},
			properties			{ IN_properties				}, 
			workGraphProperties	{ IN_workGraphProperties	}	{}


		~GPUStateObject()
		{
			if(globalSignature)
				globalSignature->Release();

			if (workGraphProperties)
				workGraphProperties->Release();

			if (properties)
				properties->Release();
		}


		uint32_t GetBackingMemory(uint32_t idx = 0) const noexcept
		{
			if (workGraphProperties) 
			{
				D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS requirements;
				workGraphProperties->GetWorkGraphMemoryRequirements(idx, &requirements);
				return requirements.MaxSizeInBytes;
			}
			else
				return 0;
		}


		ProgramIdentifier GetProgramID(std::string_view fnName) const noexcept
		{
			if (properties)
			{
				wchar_t convertedFnName[128];
				if (mbstowcs_s(nullptr, convertedFnName, fnName.data(), 128))
					return {};

				return { properties->GetProgramIdentifier(convertedFnName) };
			}
			else
				return {};
		}


		uint32_t GetEntryPointIndex(std::string_view fnName, uint32_t workGraphIdx = 0) const noexcept
		{
			if (workGraphProperties)
			{
				wchar_t convertedFnName[128];
				if (mbstowcs_s(nullptr, convertedFnName, fnName.data(), 128))
					return -1;

				auto x = workGraphProperties->GetEntrypointIndex(workGraphIdx, D3D12_NODE_ID{ .Name = convertedFnName, .ArrayIndex = 0 });
				return x;
			}
			else
				return -1;
		}


		ID3D12StateObject*				stateObject			= nullptr;
		ID3D12StateObjectProperties1*	properties			= nullptr;
		ID3D12WorkGraphProperties*		workGraphProperties = nullptr;
		RootSignature*					globalSignature		= nullptr;

		struct Deleter
		{
			iAllocator* allocator;

			void operator () (GPUStateObject* _ptr)
			{
				allocator->free(_ptr);
			}
		};
	};

	using GPUStateObject_ptr = std::unique_ptr<GPUStateObject, GPUStateObject::Deleter>;
	
	class LibraryBuilder
	{
	public:
		LibraryBuilder(iAllocator& tempAllocator) : 
			shaders		{ tempAllocator },
			subObjects	{ tempAllocator }, 
			allocator	{ tempAllocator } {}

		LibraryBuilder& LoadShaderLibrary(std::string_view path)
		{
			auto shaderLibrary = RenderSystem::_GetInstance().LoadShader(
				nullptr, "lib_6_8", path.data(),
				ShaderOptions{ 
					.hlsl2021			= true, 
					.loadRootSignature	= true });

			D3D12_DXIL_LIBRARY_DESC* dxil_desc = &allocator->allocate<D3D12_DXIL_LIBRARY_DESC>();
			dxil_desc->DXILLibrary.pShaderBytecode	= shaderLibrary.buffer;
			dxil_desc->DXILLibrary.BytecodeLength	= shaderLibrary.bufferSize;
			dxil_desc->NumExports					= 0;
			dxil_desc->pExports						= nullptr;

			shaders.emplace_back(std::move(shaderLibrary));
			subObjects.push_back(
				D3D12_STATE_SUBOBJECT{
					.Type	= D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY,
					.pDesc	= dxil_desc,
				});

			return *this;
		}


		struct EntryPoints
		{
			std::string_view	entryPointName;
			uint32_t			idx;
		};


		LibraryBuilder& AddGlobalRootSignature(RootSignature& rootSignature)
		{
			auto globalRoot						= &allocator->allocate<D3D12_GLOBAL_ROOT_SIGNATURE>();
			globalRoot->pGlobalRootSignature	= rootSignature;
			globalSignature						= &rootSignature;

			subObjects.push_back(
				D3D12_STATE_SUBOBJECT{
					.Type	= D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE,
					.pDesc	= globalRoot,
				});

			return *this;
		}


		LibraryBuilder& AddWorkGroup(std::string_view programName, std::span<EntryPoints> IN_entryPoints)
		{
			auto converted = (wchar_t*)allocator->malloc(programName.size() * 2);
			size_t convertedCharacters;
			mbstowcs_s(&convertedCharacters, converted, programName.size() * 2, programName.data(), programName.size());

			Vector<D3D12_NODE_ID>	entryPoints				{ allocator };
			Vector<D3D12_NODE>		explicitlyDefinedNodes	{ allocator };

			D3D12_WORK_GRAPH_DESC* workGroupDesc =
				&allocator->allocate<D3D12_WORK_GRAPH_DESC>(
					D3D12_WORK_GRAPH_DESC{
						.ProgramName				= converted,
						.Flags						= D3D12_WORK_GRAPH_FLAGS::D3D12_WORK_GRAPH_FLAG_INCLUDE_ALL_AVAILABLE_NODES,
						.NumEntrypoints				= (UINT)entryPoints.size(),
						.pEntrypoints				= entryPoints.size() ? entryPoints.data() : nullptr,
						.NumExplicitlyDefinedNodes	= (UINT)explicitlyDefinedNodes.size(),
						.pExplicitlyDefinedNodes	= explicitlyDefinedNodes.size() ? explicitlyDefinedNodes.data() : nullptr,
				});

			subObjects.push_back(
				D3D12_STATE_SUBOBJECT{
					.Type	= D3D12_STATE_SUBOBJECT_TYPE_WORK_GRAPH,
					.pDesc	= workGroupDesc,
				});

			return *this;
		}


		GPUStateObject_ptr BuildStateObject() const
		{
			D3D12_STATE_OBJECT_DESC descs = {
				.Type			= D3D12_STATE_OBJECT_TYPE_EXECUTABLE,
				.NumSubobjects	= (UINT)subObjects.size(),
				.pSubobjects	= subObjects.data(),
			};

			ID3D12StateObject* stateObject = nullptr;
			if (FAILED(RenderSystem::_GetInstance().pDevice14->CreateStateObject(&descs, IID_PPV_ARGS(&stateObject))))
			{
				FK_LOG_ERROR("Failed to create State Object");
				return nullptr;
			}
			else
			{
				ID3D12StateObjectProperties1*	properties			= nullptr;
				ID3D12WorkGraphProperties*		workGraphProperties = nullptr;

				if (FAILED(stateObject->QueryInterface<ID3D12StateObjectProperties1>(&properties)) || 
					FAILED(stateObject->QueryInterface<ID3D12WorkGraphProperties>(&workGraphProperties)))
				{
					if(stateObject)
						stateObject->Release();
					if(properties)
						properties->Release();
					if (workGraphProperties)
						workGraphProperties->Release();
				}
				else
				{
					GPUStateObject_ptr SO_ptr{ 
						&allocator->allocate<GPUStateObject>(
							stateObject,
							properties, 
							workGraphProperties),
						GPUStateObject::Deleter{ .allocator = allocator } };
					
					return SO_ptr;
				}
			}
		}

		Vector<D3D12_STATE_SUBOBJECT>	subObjects;
		Vector<Shader>					shaders;
		RootSignature*					globalSignature	= nullptr;
		iAllocator*						allocator		= nullptr;
	};
}