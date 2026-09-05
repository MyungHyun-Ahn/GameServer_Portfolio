#pragma once

namespace RpcLib::Protocol
{
	bool SerializeRpcHelloRequest(const FRpcHelloRequest& request, std::vector<char>& outPayload);
	bool DeserializeRpcHelloRequest(std::span<const char> payload, FRpcHelloRequest& outRequest);
	bool SerializeRpcHelloResponse(const FRpcHelloResponse& response, std::vector<char>& outPayload);
	bool DeserializeRpcHelloResponse(std::span<const char> payload, FRpcHelloResponse& outResponse);
	bool SerializeRpcRequest(const FRpcRequest& request, std::vector<char>& outPayload);
	bool DeserializeRpcRequest(std::span<const char> payload, FRpcRequest& outRequest);
	bool SerializeRpcResponse(const FRpcResponse& response, std::vector<char>& outPayload);
	bool DeserializeRpcResponse(std::span<const char> payload, FRpcResponse& outResponse);
	bool SerializeRpcNotification(const FRpcNotification& notification, std::vector<char>& outPayload);
	bool DeserializeRpcNotification(std::span<const char> payload, FRpcNotification& outNotification);
}
