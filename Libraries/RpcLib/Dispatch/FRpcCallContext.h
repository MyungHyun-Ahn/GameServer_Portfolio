#pragma once

namespace RpcLib::Dispatch
{
	// Server-local metadata for one inbound RPC request or notification. Peer identity is populated
	// from the authenticated FRpcSession and is never read from the RPC payload. Notifications use requestId 0.
	struct FRpcCallContext final
	{
		std::uint64_t rpcSessionId = 0;
		Protocol::ERpcServerType peerServerType = Protocol::ERpcServerType::Unknown;
		Protocol::FRpcServerInstanceId peerServerInstanceId = 0;
		Protocol::FRpcRequestId requestId = 0;
		std::uint64_t routingKey = 0;
		std::uint64_t originContentInstanceId = 0;
	};
}
