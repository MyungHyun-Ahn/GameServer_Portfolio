#pragma once

namespace Foundation::Ids
{
	using FAllocatedId = std::uint64_t;
	inline constexpr FAllocatedId kInvalidAllocatedId = 0;

	class IIdAllocator
	{
	public:
		virtual ~IIdAllocator() = default;

		virtual FAllocatedId Allocate() = 0;
		virtual void Release(FAllocatedId allocatedId) = 0;
	};
}
