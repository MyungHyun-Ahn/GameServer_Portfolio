#pragma once

namespace NetworkLib::Packet::Buffer
{
	class FSendSegmentPool
	{
	public:
		inline static constexpr std::size_t kDefaultRegionSizeBytes = 64 * 1024;
		inline static constexpr std::uint32_t kInvalidIndex = UINT32_MAX;

		struct SAllocation
		{
			char* data = nullptr;
			std::uint32_t bucketIndex = kInvalidIndex;
			std::uint32_t regionIndex = kInvalidIndex;
			std::uint32_t slotIndex = kInvalidIndex;
			std::uint32_t offset = 0;
			std::uint32_t capacity = 0;
			RIO_BUFFERID rioBufferId = RIO_INVALID_BUFFERID;

			bool IsValid() const noexcept
			{
				return data != nullptr && bucketIndex != kInvalidIndex;
			}
		};

	public:
		static void Configure(std::size_t regionSizeBytes = kDefaultRegionSizeBytes, std::uint32_t regionsPerBucket = 8) noexcept;
		static bool Initialize(bool registerForRio, const RIO_EXTENSION_FUNCTION_TABLE* rioFunctionTable) noexcept;
		static void Shutdown(const RIO_EXTENSION_FUNCTION_TABLE* rioFunctionTable) noexcept;

		static bool TryAllocate(std::size_t requiredLength, SAllocation& outAllocation) noexcept;
		static void Release(SAllocation& allocation) noexcept;

		static bool IsInitialized() noexcept;
		static std::size_t GetRegionSizeBytes() noexcept;
		static std::uint32_t GetRegionCount() noexcept;
		static std::uint64_t GetTotalBytes() noexcept;
		static std::uint64_t GetInUseBytes() noexcept;
		static std::uint64_t GetAllocationFailureCount() noexcept;

	private:
		FSendSegmentPool() = delete;
	};
}
