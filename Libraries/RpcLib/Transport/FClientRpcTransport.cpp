#include "RpcLibPch.h"

#include "Transport/FClientRpcTransport.h"

namespace RpcLib::Transport
{
	FClientRpcTransport::FClientRpcTransport(
		ClientNetworkLib::FClientNetwork& network,
		const std::uint8_t randomKey) noexcept
		: m_network(network)
		, m_randomKey(randomKey)
	{
	}

	bool FClientRpcTransport::SendRequest(
		const std::uint64_t networkSessionId,
		const Protocol::FRpcRequest& request)
	{
		std::vector<char> payload;
		return Protocol::SerializeRpcRequest(request, payload) &&
			   SendPayload(networkSessionId, Protocol::ERpcWireOpcode::Request, std::move(payload));
	}

	bool FClientRpcTransport::SendResponse(
		const std::uint64_t networkSessionId,
		const Protocol::FRpcResponse& response)
	{
		std::vector<char> payload;
		return Protocol::SerializeRpcResponse(response, payload) &&
			   SendPayload(networkSessionId, Protocol::ERpcWireOpcode::Response, std::move(payload));
	}

	bool FClientRpcTransport::SendNotification(
		const std::uint64_t networkSessionId,
		const Protocol::FRpcNotification& notification)
	{
		std::vector<char> payload;
		return Protocol::SerializeRpcNotification(notification, payload) &&
			   SendPayload(networkSessionId, Protocol::ERpcWireOpcode::Notification, std::move(payload));
	}

	bool FClientRpcTransport::SendHelloRequest(
		const std::uint64_t networkSessionId,
		const Protocol::FRpcHelloRequest& request)
	{
		std::vector<char> payload;
		return Protocol::SerializeRpcHelloRequest(request, payload) &&
			   SendPayload(networkSessionId, Protocol::ERpcWireOpcode::HelloRequest, std::move(payload));
	}

	bool FClientRpcTransport::SendHelloResponse(
		const std::uint64_t networkSessionId,
		const Protocol::FRpcHelloResponse& response)
	{
		std::vector<char> payload;
		return Protocol::SerializeRpcHelloResponse(response, payload) &&
			   SendPayload(networkSessionId, Protocol::ERpcWireOpcode::HelloResponse, std::move(payload));
	}

	bool FClientRpcTransport::SendPayload(
		const std::uint64_t networkSessionId,
		const Protocol::ERpcWireOpcode opcode,
		std::vector<char>&& payload)
	{
		if (networkSessionId == 0)
		{
			return false;
		}

		Protocol::FRpcWirePacket packet(opcode, std::move(payload));
		std::string error;
		return m_network.SendPacket(networkSessionId, packet, m_randomKey, error);
	}
}
