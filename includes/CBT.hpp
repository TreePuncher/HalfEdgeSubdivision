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

		uint32_t Depth(uint32_t Bit) const
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
			const uint32_t bitIdx	= GetBitOffset(heapIdx);
			const uint32_t bitWidth = maxDepth - FindMSB(heapIdx) + 1;
			return ReadValue(bitIdx, bitWidth);
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

		uint64_t GetBit(uint64_t idx, bool b) const
		{
			auto bitIdx			= (maxDepth) * ipow(2, maxDepth) + idx;
			auto wordIdx		= bitIdx / (8 * sizeof(uint64_t));
			auto bitOffset		= bitIdx % (8 * sizeof(uint64_t));

			const uint64_t bits = bitField[wordIdx];
			return bits & (0x01 << bitOffset);
		}

		uint64_t ReadValue(uint64_t start, uint64_t bitWidth) const
		{
			const uint64_t	mask	= (uint64_t(1) << (bitWidth)) - 1;
			const uint64_t	idx		= start / 64;
			const uint64_t	offset	= start % 64;

			uint64_t value	= (bitField[idx] >> offset) & mask;
			if (((start % 64) + bitWidth) > 64)
			{
				uint32_t bitsRemaining = ((start % 64) + bitWidth) % 64;
				uint64_t remainingBits = bitField[idx + 1] << (bitWidth - bitsRemaining);

				value |= (remainingBits & mask);
			}

			return value;
		}

		void WriteValue(uint64_t start, uint64_t bitWidth, uint64_t value)
		{
			const uint64_t	idx		= start / 64;
			const uint64_t	offset	= start % 64;
			uint64_t		mask	= ~(((uint64_t(1) << (bitWidth)) - 1) << offset);

			auto QWord = (bitField[idx] & mask) | (~mask & (value << offset));
			bitField[idx] = QWord;

			if (((start % 64) + bitWidth) > 64)
			{
				const uint32_t bitsRemaining	= ((start % 64) + bitWidth) % 64;
				const uint64_t remainingBits	= value >> (bitWidth - bitsRemaining);
				const uint64_t mask				= ~(((uint64_t(1) << (bitWidth)) - 1) >> (bitWidth - bitsRemaining));

				bitField[idx + 1] = remainingBits | (bitField[idx + 1] & mask);
			}
		}

		void SumReduction()
		{
			const int	end			= maxDepth;
			uint64_t	stepSize	= 1;
			uint64_t	steps		= ipow(2, maxDepth - 1);
			
			for (uint64_t i = 0; i < end; i++)
			{
				const uint64_t start_IN		= GetBitOffset(ipow(2, maxDepth - 0 - i));
				const uint64_t start_OUT	= GetBitOffset(ipow(2, maxDepth - 1 - i));

				for (int j = 0; j < steps; j++)
				{
					const uint64_t a = ReadValue(start_IN + (2 * j + 0) * stepSize, i + 1);
					const uint64_t b = ReadValue(start_IN + (2 * j + 1) * stepSize, i + 1);
					const uint64_t c = a + b;

					WriteValue(start_OUT + j * (stepSize + 1), i + 2, c);
				}

				stepSize += 1;
				steps /= 2;
			}
		}

		uint HeapIndexToBitIndex(const uint k) { return k * ipow(2, maxDepth - FindMSB(k)) - ipow(2, maxDepth); }

	//private:
		uint32_t			maxDepth	= 0;
		uint32_t			bufferSize	= 0;
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
