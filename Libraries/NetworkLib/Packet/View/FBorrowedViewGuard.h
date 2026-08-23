#pragma once

namespace NetworkLib::Packet::View
{
	class FBorrowedViewScopeState
	{
	public:
		void Invalidate() noexcept
		{
			m_alive.store(false, std::memory_order_release);
		}

		bool IsAlive() const noexcept
		{
			return m_alive.load(std::memory_order_acquire);
		}

	private:
		std::atomic<bool> m_alive = true;
	};

	class FBorrowedViewScope
	{
	public:
		FBorrowedViewScope()
			: m_state(std::make_shared<FBorrowedViewScopeState>())
		{
		}

		~FBorrowedViewScope() noexcept
		{
			if (m_state != nullptr)
			{
				m_state->Invalidate();
			}
		}

		FBorrowedViewScope(const FBorrowedViewScope&) = delete;
		FBorrowedViewScope& operator=(const FBorrowedViewScope&) = delete;
		FBorrowedViewScope(FBorrowedViewScope&&) = delete;
		FBorrowedViewScope& operator=(FBorrowedViewScope&&) = delete;

	public:
		const std::shared_ptr<FBorrowedViewScopeState>& GetState() const noexcept
		{
			return m_state;
		}

	private:
		std::shared_ptr<FBorrowedViewScopeState> m_state;
	};

	inline void ValidateBorrowedViewAccess(
		const std::shared_ptr<FBorrowedViewScopeState>& state) noexcept
	{
		if (state == nullptr)
		{
			return;
		}

		assert(state->IsAlive() && "Borrowed view packet accessed after dispatch scope ended.");
	}
}
