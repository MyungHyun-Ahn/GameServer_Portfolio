#pragma once

namespace ContentsRuntime::Session
{
	class FContentSession
	{
	public:
		explicit FContentSession(std::uint64_t sessionId) noexcept;
		virtual ~FContentSession() = default;

		FContentSession(const FContentSession&) = delete;
		FContentSession& operator=(const FContentSession&) = delete;
		FContentSession(FContentSession&&) = delete;
		FContentSession& operator=(FContentSession&&) = delete;

		std::uint64_t GetSessionId() const noexcept;
		bool IsConnected() const noexcept;
		bool IsRequestProcessing() const noexcept;
		std::uint64_t GetActiveRequestId() const noexcept;
		std::uint16_t GetActiveRequestOpcode() const noexcept;

		EBeginRequestResult TryBeginRequest(std::uint64_t requestId,
			std::uint16_t requestOpcode,
			ERequestProcessingPolicy policy,
			FRequestProcessingToken& outToken) noexcept;
		bool IsCurrentRequest(const FRequestProcessingToken& token) const noexcept;
		bool CompleteRequest(const FRequestProcessingToken& token) noexcept;
		bool CancelRequest(const FRequestProcessingToken& token) noexcept;
		void MarkDisconnected() noexcept;

	private:
		void ResetActiveRequest() noexcept;

	private:
		std::uint64_t m_sessionId = 0;
		std::atomic<bool> m_connected = true;
		FRequestProcessingState m_requestState{};
	};

	class FContentRequestContext final
	{
	public:
		FContentRequestContext(FContentSession& session, FRequestProcessingToken token) noexcept;
		~FContentRequestContext() noexcept;

		FContentRequestContext(const FContentRequestContext&) = delete;
		FContentRequestContext& operator=(const FContentRequestContext&) = delete;
		FContentRequestContext(FContentRequestContext&& other) noexcept;
		FContentRequestContext& operator=(FContentRequestContext&& other) noexcept;

		FContentSession& GetSession() const noexcept;
		const FRequestProcessingToken& GetToken() const noexcept;
		bool IsCurrent() const noexcept;
		bool Complete() noexcept;
		FRequestProcessingToken Defer() noexcept;

	private:
		void CancelIfOwned() noexcept;

	private:
		FContentSession* m_session = nullptr;
		FRequestProcessingToken m_token{};
		bool m_deferred = false;
	};
}
