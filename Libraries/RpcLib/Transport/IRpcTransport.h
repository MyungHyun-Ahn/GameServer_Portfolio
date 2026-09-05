#pragma once

namespace RpcLib::Transport
{
	class IRpcTransport
	{
	public:
		virtual ~IRpcTransport() = default;

		virtual bool SendRequest(std::uint64_t networkSessionId, const Protocol::FRpcRequest& request) = 0;
		virtual bool SendResponse(std::uint64_t networkSessionId, const Protocol::FRpcResponse& response) = 0;
		virtual bool SendNotification(
			std::uint64_t networkSessionId,
			const Protocol::FRpcNotification& notification)
		{
			(void)networkSessionId;
			(void)notification;
			return false;
		}
	};
}
