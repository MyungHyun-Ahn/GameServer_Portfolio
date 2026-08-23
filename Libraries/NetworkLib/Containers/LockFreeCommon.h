#pragma once

namespace NetworkLib::Containers
{
	template <typename T>
	concept FundamentalOrPointer = std::is_fundamental_v<T> || std::is_pointer_v<T>;

	constexpr std::uint64_t kAddressMask = 0x0000FFFFFFFFFFFFULL;
	constexpr std::uint32_t kTagBitCount = 16;
	constexpr std::uint64_t kTagMask = ~kAddressMask;

	inline std::uint64_t MakeTaggedPointer(
		std::uint64_t tag,
		const void* pointer) noexcept
	{
		const std::uint64_t address = reinterpret_cast<std::uint64_t>(pointer) & kAddressMask;
		return ((tag << (64 - kTagBitCount)) & kTagMask) | address;
	}

	inline std::uint64_t GetTag(
		std::uint64_t taggedPointer) noexcept
	{
		return taggedPointer >> (64 - kTagBitCount);
	}

	template <typename T>
	inline T* GetPointer(
		std::uint64_t taggedPointer) noexcept
	{
		return reinterpret_cast<T*>(taggedPointer & kAddressMask);
	}

	inline std::uint64_t AtomicLoad64(
		volatile LONG64* target) noexcept
	{
		return static_cast<std::uint64_t>(InterlockedCompareExchange64(target, 0, 0));
	}

	inline std::uint64_t AtomicCompareExchange64(
		volatile LONG64* target,
		std::uint64_t exchangeValue,
		std::uint64_t comparand) noexcept
	{
		return static_cast<std::uint64_t>(
			InterlockedCompareExchange64(target, static_cast<LONG64>(exchangeValue), static_cast<LONG64>(comparand)));
	}

	inline std::uint64_t AtomicIncrement64(
		volatile LONG64* target) noexcept
	{
		return static_cast<std::uint64_t>(InterlockedIncrement64(target));
	}
}
