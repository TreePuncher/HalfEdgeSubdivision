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


	uint64_t FindMSB(uint64_t) noexcept;
	uint64_t FindLSB(uint64_t) noexcept;

	uint ipow(uint32_t base, uint32_t exp) noexcept;

	inline uint32_t GetCBTSizeBitSize(const uint32_t maxDepth)
	{
		const uint32_t bitsRequired = ((ipow(2, maxDepth + 2)) - (maxDepth + 3));
		return bitsRequired;
	}

	inline uint32_t GetCBTSizeBytes(const uint32_t maxDepth, const uint32_t numTrees) noexcept
	{
		auto bitsRequired = GetCBTSizeBitSize(maxDepth) * numTrees;
		return (bitsRequired / 8) + ((bitsRequired % 8) != 0 ? 1 : 0);
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
		void Upload		(class FrameGraph&);
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

		uint32_t Depth(uint32_t Bit)
		{
			return maxDepth - log2(Bit + 1);
		}

		uint32_t BitToHeapIndex(uint32_t x)
		{
			return 0;
		}

		uint64_t GetBitOffset(uint64_t idx) const noexcept
		{
			const uint64_t d_k		= log2(idx);
			const uint64_t N_d_k	= maxDepth - d_k + 1;
			const uint64_t X_k		= ipow(2, d_k + 1) + idx * N_d_k;

			return X_k;
		}

		uint64_t GetHeapValue(uint32_t heapIdx) const noexcept
		{
			const uint64_t bitIdx		= GetBitOffset(heapIdx);
			const uint64_t bitWidth		= maxDepth - FindMSB(heapIdx) + 1;
			const uint64_t wordIdx		= bitIdx / (8 * sizeof(uint64_t));
			const uint64_t bitOffset	= bitIdx % (8 * sizeof(uint64_t));
			const uint64_t mask			= ((uint64_t{ 1 } << (bitWidth)) - 1) << bitOffset;
			const uint64_t qw			= bitField[wordIdx];
			const uint64_t value		= qw & mask;

			return value >> bitOffset;
		}

		uint32_t DecodeNode(int32_t leafID)
		{
			uint32_t heapID = 1;
			uint32_t value	= GetHeapValue(heapID);

			for (int i = 0; i < maxDepth; i++)
			{
				if (value > 1)
				{
					uint32_t temp = GetHeapValue(2 * heapID);
					if (leafID < temp)
					{
						heapID *= 2;
						value = temp;
					}
					else
					{
						leafID = leafID - temp;
						heapID = 2 * heapID + 1;
						value = GetHeapValue(heapID);
					}
				}
				else
					break;
			}

			return heapID;
		}

		void SetBit(uint64_t idx, bool b)
		{
			const uint64_t bitIdx		= GetBitOffset(ipow(2, maxDepth) + idx);
			const uint64_t wordIdx		= bitIdx / (8 * sizeof(uint64_t));
			const uint64_t bitOffset	= bitIdx % (8 * sizeof(uint64_t)); 

			const uint64_t bits		= bitField[wordIdx];
			const uint64_t mask		= ~(uint64_t{ 1} << bitOffset);
			const uint64_t newValue = (bits & mask) | ((b ? uint64_t{ 0x01 } : uint64_t{ 0x00 }) << bitOffset);

			bitField[wordIdx] = newValue;
		}

		uint64_t GetBit(uint64_t idx, bool b)
		{
			auto bitIdx			= (maxDepth) * ipow(2, maxDepth) + idx;
			auto wordIdx		= bitIdx / (8 * sizeof(uint64_t));
			auto bitOffset		= bitIdx % (8 * sizeof(uint64_t));

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
				const uint64_t start_IN		= GetBitOffset(ipow(2, maxDepth - 0 - i));
				const uint64_t start_OUT	= GetBitOffset(ipow(2, maxDepth - 1 - i));
				const uint64_t mask_in		= (uint64_t{ 1 } << (i + 1)) - 1;
				const uint64_t mask_out		= (uint64_t{ 1 } << (i + 2)) - 1;

				for (int j = 0; j < steps; j++)
				{
					const uint64_t idx_a	= (start_IN + (2 * j + 0) * stepSize) / (8 * sizeof(uint64_t));
					const uint64_t idx_b	= (start_IN + (2 * j + 1) * stepSize) / (8 * sizeof(uint64_t));

					const uint64_t offset_a = (start_IN + (2 * j + 0) * stepSize) % (8 * sizeof(uint64_t));
					const uint64_t offset_b = (start_IN + (2 * j + 1) * stepSize) % (8 * sizeof(uint64_t));

					const uint64_t maskA = mask_in << offset_a;
					const uint64_t maskB = mask_in << offset_b;
					const uint64_t a = (bitField[idx_a] & maskA) >> (offset_a);
					const uint64_t b = (bitField[idx_b] & maskB) >> (offset_b);
					const uint64_t c = a + b;

					const int idx_out			= (start_OUT + j * (stepSize + 1)) / (8 * sizeof(uint64_t));
					const int Offset_out		= (start_OUT + j * (stepSize + 1)) % (8 * sizeof(uint64_t));
					const uint64_t c_shifted	= c << Offset_out;
					const uint64_t out_mask		= ~(mask_out << Offset_out);
					const uint64_t outWord		= bitField[idx_out];
					const uint64_t newValue		= (outWord & out_mask) | c_shifted;

					bitField[idx_out] = newValue;
				}

				stepSize += 1;
				steps /= 2;
			}
		}

		uint HeapIndexToBitIndex(const uint k) { return k * ipow(2, maxDepth - FindMSB(k)) - ipow(2, maxDepth); }

	private:
		uint32_t			maxDepth	= 0;
		ResourceHandle		buffer		= InvalidHandle;
		RenderSystem&		renderSystem;
		Vector<uint64_t>	bitField;
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
