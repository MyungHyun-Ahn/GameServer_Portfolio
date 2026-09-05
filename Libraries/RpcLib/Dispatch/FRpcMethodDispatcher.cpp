#include "RpcLibPch.h"

#include "Dispatch/FRpcMethodDispatcher.h"

namespace RpcLib::Dispatch
{
	Protocol::FRpcResponse FRpcMethodDispatcher::Dispatch(
		const FRpcCallContext& context,
		const Protocol::FRpcRequest& request) const
	{
		Protocol::FRpcResponse response;
		response.protocolVersion = Protocol::kRpcProtocolVersion;
		response.requestId = request.requestId;
		response.serviceId = request.serviceId;
		response.methodId = request.methodId;
		response.originContentInstanceId = request.originContentInstanceId;

		if (request.protocolVersion != Protocol::kRpcProtocolVersion)
		{
			response.resultCode = Protocol::ERpcResponseCode::ProtocolMismatch;
			return response;
		}

		std::shared_ptr<FRpcRawMethodHandler> handler;
		{
			std::shared_lock lock(m_lock);
			const auto it = m_handlers.find(MakeMethodKey(request.serviceId, request.methodId));
			if (it == m_handlers.end())
			{
				response.resultCode = Protocol::ERpcResponseCode::MethodNotFound;
				return response;
			}

			handler = it->second;
		}

		response.resultCode = (*handler)(context, request.payload, response.payload);
		return response;
	}

	Protocol::FRpcResponse FRpcMethodDispatcher::Dispatch(
		const Protocol::FRpcRequest& request) const
	{
		FRpcCallContext context;
		context.requestId = request.requestId;
		context.routingKey = request.routingKey;
		context.originContentInstanceId = request.originContentInstanceId;
		return Dispatch(context, request);
	}

	Protocol::ERpcNotificationDispatchResult FRpcMethodDispatcher::DispatchNotification(
		const FRpcCallContext& context,
		const Protocol::FRpcNotification& notification) const
	{
		if (notification.protocolVersion != Protocol::kRpcProtocolVersion)
		{
			return Protocol::ERpcNotificationDispatchResult::ProtocolMismatch;
		}

		std::shared_ptr<FRpcRawNotificationHandler> handler;
		{
			std::shared_lock lock(m_lock);
			const auto it = m_notificationHandlers.find(MakeMethodKey(notification.serviceId, notification.methodId));
			if (it == m_notificationHandlers.end())
			{
				return Protocol::ERpcNotificationDispatchResult::MethodNotFound;
			}

			handler = it->second;
		}

		return (*handler)(context, notification.payload);
	}

	Protocol::ERpcNotificationDispatchResult FRpcMethodDispatcher::DispatchNotification(
		const Protocol::FRpcNotification& notification) const
	{
		FRpcCallContext context;
		context.routingKey = notification.routingKey;
		context.originContentInstanceId = notification.originContentInstanceId;
		return DispatchNotification(context, notification);
	}

	std::size_t FRpcMethodDispatcher::GetRegisteredMethodCount() const
	{
		std::shared_lock lock(m_lock);
		return m_handlers.size();
	}

	std::size_t FRpcMethodDispatcher::GetRegisteredNotificationCount() const
	{
		std::shared_lock lock(m_lock);
		return m_notificationHandlers.size();
	}

	bool FRpcMethodDispatcher::RegisterRaw(
		const Protocol::FRpcServiceId serviceId,
		const Protocol::FRpcMethodId methodId,
		FRpcRawMethodHandler handler)
	{
		if (serviceId == 0 || methodId == 0 || !handler)
		{
			return false;
		}

		std::unique_lock lock(m_lock);
		return m_handlers.emplace(MakeMethodKey(serviceId, methodId), std::make_shared<FRpcRawMethodHandler>(std::move(handler))).second;
	}

	bool FRpcMethodDispatcher::RegisterRawNotification(
		const Protocol::FRpcServiceId serviceId,
		const Protocol::FRpcMethodId methodId,
		FRpcRawNotificationHandler handler)
	{
		if (serviceId == 0 || methodId == 0 || !handler)
		{
			return false;
		}

		std::unique_lock lock(m_lock);
		return m_notificationHandlers
			.emplace(MakeMethodKey(serviceId, methodId), std::make_shared<FRpcRawNotificationHandler>(std::move(handler)))
			.second;
	}

	std::uint64_t FRpcMethodDispatcher::MakeMethodKey(
		const Protocol::FRpcServiceId serviceId,
		const Protocol::FRpcMethodId methodId) noexcept
	{
		return (static_cast<std::uint64_t>(serviceId) << 32) | static_cast<std::uint64_t>(methodId);
	}
}
