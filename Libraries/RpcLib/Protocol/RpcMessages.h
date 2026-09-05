#pragma once

namespace RpcLib::Protocol
{
	struct FRpcHelloRequest final
	{
		std::uint32_t protocolVersion = kRpcProtocolVersion;
		ERpcServerType serverType = ERpcServerType::Unknown;
		FRpcServerInstanceId serverInstanceId = 0;
	};

	struct FRpcHelloResponse final
	{
		std::uint32_t protocolVersion = kRpcProtocolVersion;
		ERpcHelloResult result = ERpcHelloResult::Success;
		ERpcServerType serverType = ERpcServerType::Unknown;
		FRpcServerInstanceId serverInstanceId = 0;
	};

	struct FRpcRequest final
	{
		std::uint32_t protocolVersion = kRpcProtocolVersion;
		FRpcRequestId requestId = 0;
		FRpcServiceId serviceId = 0;
		FRpcMethodId methodId = 0;
		std::uint64_t routingKey = 0;
		std::uint64_t originContentInstanceId = 0;
		std::vector<char> payload;
	};

	struct FRpcResponse final
	{
		std::uint32_t protocolVersion = kRpcProtocolVersion;
		FRpcRequestId requestId = 0;
		FRpcServiceId serviceId = 0;
		FRpcMethodId methodId = 0;
		std::uint64_t originContentInstanceId = 0;
		ERpcResponseCode resultCode = ERpcResponseCode::Success;
		std::vector<char> payload;
	};

	struct FRpcNotification final
	{
		std::uint32_t protocolVersion = kRpcProtocolVersion;
		FRpcServiceId serviceId = 0;
		FRpcMethodId methodId = 0;
		std::uint64_t routingKey = 0;
		std::uint64_t originContentInstanceId = 0;
		std::vector<char> payload;
	};
}
