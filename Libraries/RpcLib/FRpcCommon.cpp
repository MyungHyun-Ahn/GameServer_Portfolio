#include "RpcLibPch.h"

#include "FRpcCommon.h"

namespace RpcLib
{
	FRpcCommon::FRpcCommon(
		Session::FRpcSessionRegistry& sessionRegistry,
		Dispatch::FRpcMethodDispatcher& methodDispatcher,
		Call::FRpcRequestIdGenerator& requestIdGenerator,
		Transport::IRpcTransport& transport,
		const std::uint64_t originContentInstanceId,
		const std::size_t maxPendingCallCount) noexcept
		: m_sessionRegistry(sessionRegistry)
		, m_methodDispatcher(methodDispatcher)
		, m_requestIdGenerator(requestIdGenerator)
		, m_transport(transport)
		, m_originContentInstanceId(originContentInstanceId)
		, m_pendingCallManager(maxPendingCallCount)
	{
	}

	Protocol::FRpcResponse FRpcCommon::DispatchRequest(
		const std::uint64_t rpcSessionId,
		const Protocol::FRpcRequest& request) const
	{
		const std::shared_ptr<Session::FRpcSession> rpcSession = m_sessionRegistry.Find(rpcSessionId);
		if (rpcSession == nullptr || !rpcSession->IsReady())
		{
			Protocol::FRpcResponse response;
			response.requestId = request.requestId;
			response.serviceId = request.serviceId;
			response.methodId = request.methodId;
			response.originContentInstanceId = request.originContentInstanceId;
			response.resultCode = Protocol::ERpcResponseCode::SessionNotReady;
			return response;
		}

		Dispatch::FRpcCallContext context;
		context.rpcSessionId = rpcSessionId;
		context.peerServerType = rpcSession->GetRemoteServerType();
		context.peerServerInstanceId = rpcSession->GetRemoteServerInstanceId();
		context.requestId = request.requestId;
		context.routingKey = request.routingKey;
		context.originContentInstanceId = request.originContentInstanceId;
		return m_methodDispatcher.Dispatch(context, request);
	}

	Protocol::FRpcResponse FRpcCommon::DispatchRequest(
		const Protocol::FRpcRequest& request) const
	{
		return m_methodDispatcher.Dispatch(request);
	}

	Protocol::ERpcNotificationDispatchResult FRpcCommon::DispatchNotification(
		const std::uint64_t rpcSessionId,
		const Protocol::FRpcNotification& notification) const
	{
		const std::shared_ptr<Session::FRpcSession> rpcSession = m_sessionRegistry.Find(rpcSessionId);
		if (rpcSession == nullptr || !rpcSession->IsReady())
		{
			return Protocol::ERpcNotificationDispatchResult::SessionNotReady;
		}

		Dispatch::FRpcCallContext context;
		context.rpcSessionId = rpcSessionId;
		context.peerServerType = rpcSession->GetRemoteServerType();
		context.peerServerInstanceId = rpcSession->GetRemoteServerInstanceId();
		context.routingKey = notification.routingKey;
		context.originContentInstanceId = notification.originContentInstanceId;
		return m_methodDispatcher.DispatchNotification(context, notification);
	}

	Protocol::ERpcNotificationDispatchResult FRpcCommon::DispatchNotification(
		const Protocol::FRpcNotification& notification) const
	{
		return m_methodDispatcher.DispatchNotification(notification);
	}

	Protocol::ERpcCompletionResult FRpcCommon::ProcessResponse(
		const std::uint64_t rpcSessionId,
		const Protocol::FRpcResponse& response)
	{
		if (response.originContentInstanceId != m_originContentInstanceId)
		{
			return Protocol::ERpcCompletionResult::OriginMismatch;
		}

		return m_pendingCallManager.Complete(rpcSessionId, response);
	}

	std::size_t FRpcCommon::ProcessTimeouts(
		const std::chrono::steady_clock::time_point now)
	{
		return m_pendingCallManager.Expire(now);
	}

	std::size_t FRpcCommon::FailSession(
		const std::uint64_t rpcSessionId,
		const Protocol::ERpcCallError error)
	{
		return m_pendingCallManager.FailSession(rpcSessionId, error);
	}

	std::size_t FRpcCommon::GetPendingCallCount() const noexcept
	{
		return m_pendingCallManager.GetPendingCallCount();
	}

	std::uint64_t FRpcCommon::GetOriginContentInstanceId() const noexcept
	{
		return m_originContentInstanceId;
	}
}
