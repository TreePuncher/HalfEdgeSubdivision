#include <ResourceHandles.hpp>
#include <atomic>

namespace FlexKit
{
	class RenderSystem;

	template<typename TY_OP>
	struct MT_DoOnce
	{
		template<typename ... TY_args>
		void operator () (TY_args&& ... args)requires requires(TY_OP op, TY_args ... args)
		{
			op(args...);
		}
		{
			if (!completed.load(std::memory_order_relaxed))
			{
				if (auto res = lock.fetch_add(1, std::memory_order_acq_rel); res != 0)
					while (!completed.load(std::memory_order_acquire));
				else
				{
					op(std::forward<TY_args>(args)...);
					completed = true;
				}
			}
		}

		std::atomic_int		lock		= 0;
		std::atomic_bool	completed	= false;
		TY_OP				op;
	};


	template<typename TY_op>
	auto MakeDoOnce(TY_op fnOperation)
	{
		return MT_DoOnce<TY_op>{
			.op			= fnOperation
		};
	}


	constexpr uint64_t FindMSB(uint32_t) noexcept;
	constexpr uint64_t FindLSB(uint32_t) noexcept;

	uint ipow(uint32_t base, uint32_t exp) noexcept;

	inline uint32_t GetCBTSizeBitSize(const uint32_t maxDepth)
	{
		const uint32_t bitsRequired = ((ipow(2, maxDepth + 2)) - (maxDepth + 3));
		return bitsRequired;
	}

	inline uint32_t GetCBTSizeBytes(const uint32_t maxDepth, const uint32_t numTrees) noexcept
	{
		auto bitsRequired = GetCBTSizeBitSize(maxDepth) * numTrees;
		return (bitsRequired / 32) + ((bitsRequired % 32) != 0 ? 1 : 0);
	}

	struct CBTBufferDescription
	{
		uint32_t cbtTreeCount	= 1;
		uint32_t maxDepth		= 4;
	};


	constexpr FlexKit::PSOHandle UpdateCBTTree	= FlexKit::PSOHandle(GetTypeGUID(UpdateCBTTree));
	constexpr FlexKit::PSOHandle DrawCBTTree	= FlexKit::PSOHandle(GetTypeGUID(DrawCBTTree));


	struct CBTBuffer
	{
		CBTBuffer(RenderSystem& IN_renderSystem, iAllocator& persistent);
		~CBTBuffer();
		
		void Initialize	(const CBTBufferDescription& = {});
		void Update		(class FrameGraph&);
		void Clear		(class FrameGraph&);

		void ReloadShaders();

		ResourceHandle	GetBuffer()		const { return buffer; }
		uint32_t		GetMaxDepth()	const { return maxDepth; }
		
		struct PipelineStates
		{
			void Initialize(RenderSystem& renderSystem);
			void QueueReload(RenderSystem& renderSystem);

			static void _InitializeStates(PipelineStates& states, RenderSystem& renderSystem);

			using TY_helper = decltype(MakeDoOnce(&_InitializeStates));

			TY_helper init = MakeDoOnce(&_InitializeStates);
			PSOHandle sumReduce;
		};

		inline static PipelineStates states;

		uint Depth(uint Bit)
		{
			return maxDepth - log2(Bit + 1);
		}

		uint BitToHeapIndex(uint32_t x)
		{
			return 0;
		}

		uint GetBitOffset(uint32_t idx) const noexcept
		{
			const uint32_t d_k		= log2(idx);
			const uint32_t N_d_k	= maxDepth - d_k + 1;
			const uint32_t X_k		= ipow(2, d_k + 1) + idx * N_d_k;

			return X_k;
		}

		void SetBit(uint32_t idx, bool b)
		{
			auto bitIdx		= (maxDepth) * ipow(2, maxDepth) + idx;
			auto wordIdx	= bitIdx / 32;
			auto bitOffset	= bitIdx % 32; 

			const uint32_t bits		= bitField[wordIdx];
			const uint32_t newValue = (bits& ~(2 << bitOffset)) | ((b ? 0x01 : 0x00) << bitOffset);

			bitField[wordIdx] = newValue;
		}

		uint64_t GetBit(uint64_t idx, bool b)
		{
			auto bitIdx			= (maxDepth) * ipow(2, maxDepth) + idx;
			auto wordIdx		= bitIdx / 32;
			auto bitOffset		= bitIdx % 32;

			const uint64_t bits = bitField[wordIdx];
			return bits & (0x01 << bitOffset);
		}

		void SumReduction()
		{
			const int	end			= maxDepth;
			int			stepSize	= 1;
			int			steps		= ipow(2, maxDepth - 1);
			
			for (int i = 0; i < end; i++)
			{
				const int start_IN	= (maxDepth - i) * ipow(2, maxDepth);
				const int start_OUT = start_IN - ipow(2, maxDepth);
				//const uint32_t mask_in	= (1 << (stepSize)) - 1;
				//const uint32_t mask_out	= (1 << (2 * stepSize)) - 1;

				const uint32_t mask_in	= (1 << (i)) - 1;
				const uint32_t mask_out	= (1 << (i + 1)) - 1;

				int x = 0;
				for (int j = 0; j < steps; j++)
				{
					const int idx_a	= (start_IN + (j + 0) * stepSize) / 32;
					const int idx_b	= (start_IN + (j + 1) * stepSize) / 32;

					const int offset_a = (start_IN + (j + 0) * stepSize) % 32;
					const int offset_b = (start_IN + (j + 1) * stepSize) % 32;

					const int maskA = mask_in << offset_a;
					const int maskB = mask_in << offset_b;
					const uint32_t a = (bitField[idx_a] & maskA) >> (offset_a);
					const uint32_t b = (bitField[idx_b] & maskB) >> (offset_b);
					const uint32_t c = a + b;

					const int idx_out			= (start_OUT + j * stepSize * 2) / 32;
					const int Offset_out		= (start_OUT + j * stepSize * 2) % 32;
					const uint32_t c_shifted	= c << Offset_out;
					const uint32_t out_mask		= ~(mask_out << Offset_out);
					const uint32_t outWord		= bitField[idx_out];
					const uint32_t newValue		= (outWord & out_mask) | c_shifted;

					bitField[idx_out] = newValue;
				}

				stepSize *= 2;
				steps /= 2;
			}
		}

		uint HeapIndexToBitIndex(const uint k) { return k * ipow(2, maxDepth - FindMSB(k)) - ipow(2, maxDepth); }

	private:
		size_t				maxDepth	= 0;
		ResourceHandle		buffer		= InvalidHandle;
		RenderSystem&		renderSystem;
		Vector<uint32_t>	bitField;
	};


	struct CBTMemoryManager
	{
	public:
		CBTMemoryManager(RenderSystem& IN_renderSystem, iAllocator& IN_persistent) :
			cbt{ IN_renderSystem, IN_persistent } {}


		void Update(class FrameGraph&);
		void DrawDebugVIS(class FrameGraph&, ResourceHandle renderTarget);

		ResourceHandle		buffer;
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
