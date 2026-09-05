#include "ContentsRuntimePch.h"

#include "Session/FContentSession.h"

namespace ContentsRuntime::Session
{
	FContentSession::FContentSession(
		const std::uint64_t sessionId) noexcept
		: m_sessionId(sessionId)
	{
	}

	std::uint64_t FContentSession::GetSessionId() const noexcept
	{
		return m_sessionId;
	}

	bool FContentSession::IsConnected() const noexcept
	{
		return m_connected.load(std::memory_order_acquire);
	}

	bool FContentSession::IsRequestProcessing() const noexcept
	{
		return m_requestState.processing;
	}

	std::uint64_t FContentSession::GetActiveRequestId() const noexcept
	{
		return m_requestState.requestId;
	}

	std::uint16_t FContentSession::GetActiveRequestOpcode() const noexcept
	{
		return m_requestState.requestOpcode;
	}

	EBeginRequestResult FContentSession::TryBeginRequest(
		const std::uint64_t requestId,
		const std::uint16_t requestOpcode,
		const ERequestProcessingPolicy policy,
		FRequestProcessingToken& outToken) noexcept
	{
		outToken = {};
		if (!IsConnected())
		{
			return EBeginRequestResult::Disconnected;
		}

		if (policy == ERequestProcessingPolicy::AllowedWhileBusy)
		{
			return EBeginRequestResult::AllowedWithoutTracking;
		}

		if (m_requestState.processing)
		{
			return EBeginRequestResult::AlreadyProcessing;
		}

		std::uint64_t operationId = m_requestState.nextOperationId++;
		if (operationId == 0)
		{
			operationId = m_requestState.nextOperationId++;
		}

		m_requestState.processing = true;
		m_requestState.activeOperationId = operationId;
		m_requestState.requestId = requestId;
		m_requestState.requestOpcode = requestOpcode;
		outToken.sessionId = m_sessionId;
		outToken.operationId = operationId;
		return EBeginRequestResult::Started;
	}

	bool FContentSession::IsCurrentRequest(
		const FRequestProcessingToken& token) const noexcept
	{
		return token.IsValid() && token.sessionId == m_sessionId && m_requestState.processing &&
			   m_requestState.activeOperationId == token.operationId;
	}

	bool FContentSession::CompleteRequest(
		const FRequestProcessingToken& token) noexcept
	{
		if (!IsCurrentRequest(token))
		{
			return false;
		}

		ResetActiveRequest();
		return true;
	}

	bool FContentSession::CancelRequest(
		const FRequestProcessingToken& token) noexcept
	{
		if (!IsCurrentRequest(token))
		{
			return false;
		}

		ResetActiveRequest();
		return true;
	}

	void FContentSession::MarkDisconnected() noexcept
	{
		m_connected.store(false, std::memory_order_release);
	}

	void FContentSession::ResetActiveRequest() noexcept
	{
		m_requestState.processing = false;
		m_requestState.activeOperationId = 0;
		m_requestState.requestId = 0;
		m_requestState.requestOpcode = 0;
	}

	FContentRequestContext::FContentRequestContext(
		FContentSession& session,
		const FRequestProcessingToken token) noexcept
		: m_session(&session)
		, m_token(token)
	{
	}

	FContentRequestContext::~FContentRequestContext() noexcept
	{
		CancelIfOwned();
	}

	FContentRequestContext::FContentRequestContext(
		FContentRequestContext&& other) noexcept
		: m_session(std::exchange(other.m_session, nullptr))
		, m_token(std::exchange(other.m_token, {}))
		, m_deferred(std::exchange(other.m_deferred, false))
	{
	}

	FContentRequestContext& FContentRequestContext::operator=(
		FContentRequestContext&& other) noexcept
	{
		if (this != &other)
		{
			CancelIfOwned();
			m_session = std::exchange(other.m_session, nullptr);
			m_token = std::exchange(other.m_token, {});
			m_deferred = std::exchange(other.m_deferred, false);
		}

		return *this;
	}

	FContentSession& FContentRequestContext::GetSession() const noexcept
	{
		assert(m_session != nullptr);
		return *m_session;
	}

	const FRequestProcessingToken& FContentRequestContext::GetToken() const noexcept
	{
		return m_token;
	}

	bool FContentRequestContext::IsCurrent() const noexcept
	{
		return m_session != nullptr && m_session->IsCurrentRequest(m_token);
	}

	bool FContentRequestContext::Complete() noexcept
	{
		if (m_session == nullptr)
		{
			return false;
		}

		const bool completed = m_session->CompleteRequest(m_token);
		if (completed)
		{
			m_session = nullptr;
			m_token = {};
			m_deferred = false;
		}

		return completed;
	}

	FRequestProcessingToken FContentRequestContext::Defer() noexcept
	{
		m_deferred = true;
		return m_token;
	}

	void FContentRequestContext::CancelIfOwned() noexcept
	{
		if (m_session != nullptr && !m_deferred)
		{
			m_session->CancelRequest(m_token);
		}
	}
}
