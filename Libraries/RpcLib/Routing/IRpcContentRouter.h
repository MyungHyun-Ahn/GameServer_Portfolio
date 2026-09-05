#pragma once

namespace RpcLib::Routing
{
	class IRpcContentRouter
	{
	public:
		virtual ~IRpcContentRouter() = default;

		virtual bool RouteRequest(std::uint64_t networkSessionId, Protocol::FRpcRequest request) = 0;
		virtual bool RouteResponse(std::uint64_t networkSessionId, Protocol::FRpcResponse response) = 0;
		virtual bool RouteNotification(
			std::uint64_t networkSessionId,
			Protocol::FRpcNotification notification)
		{
			(void)networkSessionId;
			(void)notification;
			return false;
		}
	};
}
