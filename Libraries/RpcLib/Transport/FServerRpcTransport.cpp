#include "RpcLibPch.h"

#include "Transport/FServerRpcTransport.h"

namespace RpcLib::Transport
{
	void FServerRpcTransport::Bind(
		NetworkLib::IServer& server) noexcept
	{
		m_server.store(&server, std::memory_order_release);
	}

	void FServerRpcTransport::Unbind() noexcept
	{
		m_server.store(nullptr, std::memory_order_release);
	}

	bool FServerRpcTransport::IsBound() const noexcept
	{
		return m_server.load(std::memory_order_acquire) != nullptr;
	}

	bool FServerRpcTransport::SendRequest(
		const std::uint64_t networkSessionId,
		const Protocol::FRpcRequest& request)
	{
		std::vector<char> payload;
		return Protocol::SerializeRpcRequest(request, payload) &&
			   SendPayload(networkSessionId, Protocol::ERpcWireOpcode::Request, std::move(payload));
	}

	bool FServerRpcTransport::SendResponse(
		const std::uint64_t networkSessionId,
		const Protocol::FRpcResponse& response)
	{
		std::vector<char> payload;
		return Protocol::SerializeRpcResponse(response, payload) &&
			   SendPayload(networkSessionId, Protocol::ERpcWireOpcode::Response, std::move(payload));
	}

	bool FServerRpcTransport::SendNotification(
		const std::uint64_t networkSessionId,
		const Protocol::FRpcNotification& notification)
	{
		std::vector<char> payload;
		return Protocol::SerializeRpcNotification(notification, payload) &&
			   SendPayload(networkSessionId, Protocol::ERpcWireOpcode::Notification, std::move(payload));
	}

	bool FServerRpcTransport::SendHelloRequest(
		const std::uint64_t networkSessionId,
		const Protocol::FRpcHelloRequest& request)
	{
		std::vector<char> payload;
		return Protocol::SerializeRpcHelloRequest(request, payload) &&
			   SendPayload(networkSessionId, Protocol::ERpcWireOpcode::HelloRequest, std::move(payload));
	}

	bool FServerRpcTransport::SendHelloResponse(
		const std::uint64_t networkSessionId,
		const Protocol::FRpcHelloResponse& response)
	{
		std::vector<char> payload;
		return Protocol::SerializeRpcHelloResponse(response, payload) &&
			   SendPayload(networkSessionId, Protocol::ERpcWireOpcode::HelloResponse, std::move(payload));
	}

	bool FServerRpcTransport::SendPayload(
		const std::uint64_t networkSessionId,
		const Protocol::ERpcWireOpcode opcode,
		std::vector<char>&& payload)
	{
		NetworkLib::IServer* server = m_server.load(std::memory_order_acquire);
		if (server == nullptr || networkSessionId == 0)
		{
			return false;
		}

		Protocol::FRpcWirePacket packet(opcode, std::move(payload));
		return server->SendPacket(networkSessionId, NetworkLib::Packet::Serialization::BuildOutgoingContentPacket(packet));
	}
}
