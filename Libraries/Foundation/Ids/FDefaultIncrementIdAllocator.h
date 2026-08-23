#pragma once

namespace Foundation::Ids
{
	class FDefaultIncrementIdAllocator final : public IIdAllocator
	{
	public:
		explicit FDefaultIncrementIdAllocator(FAllocatedId initialValue = kInvalidAllocatedId) noexcept;

		FAllocatedId Allocate() override;
		void Release(FAllocatedId allocatedId) override;

	private:
		std::atomic<FAllocatedId> m_nextId;
	};
}
