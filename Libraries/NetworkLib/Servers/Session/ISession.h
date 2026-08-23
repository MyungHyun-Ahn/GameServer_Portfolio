#pragma once

namespace NetworkLib::Session
{
	class ISession
	{
	public:
		virtual ~ISession() = default;

		virtual std::uint64_t GetSessionId() const noexcept = 0;
		virtual std::uint32_t GetSlotIndex() const noexcept = 0;
		virtual std::uint32_t GetGeneration() const noexcept = 0;

		virtual bool IsClosing() const noexcept = 0;
		virtual bool TryMarkClosing() noexcept = 0;

		virtual long AcquireRef() noexcept = 0;
		virtual long ReleaseRef() noexcept = 0;

		virtual std::uint32_t GetQueuedSendBufferCount() const noexcept = 0;
		virtual std::uint32_t GetMaxObservedQueuedSendBufferCount() const noexcept = 0;
	};
}
