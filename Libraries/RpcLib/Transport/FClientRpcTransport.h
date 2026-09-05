#pragma once

namespace ClientNetworkLib
{
	class FClientNetwork;
}

namespace RpcLib::Transport
{
	class FClientRpcTransport final : public IRpcTransport
	{
	public:
		// FClientNetwork serializes its per-session send queue internally, so one
		// transport can be used concurrently by multiple Content threads.
		FClientRpcTransport(ClientNetworkLib::FClientNetwork& network, std::uint8_t randomKey) noexcept;

		bool SendRequest(std::uint64_t networkSessionId, const Protocol::FRpcRequest& request) override;
		bool SendResponse(std::uint64_t networkSessionId, const Protocol::FRpcResponse& response) override;
		bool SendNotification(std::uint64_t networkSessionId, const Protocol::FRpcNotification& notification) override;
		bool SendHelloRequest(std::uint64_t networkSessionId, const Protocol::FRpcHelloRequest& request);
		bool SendHelloResponse(std::uint64_t networkSessionId, const Protocol::FRpcHelloResponse& response);

	private:
		bool SendPayload(std::uint64_t networkSessionId, Protocol::ERpcWireOpcode opcode, std::vector<char>&& payload);

	private:
		ClientNetworkLib::FClientNetwork& m_network;
		std::uint8_t m_randomKey = 0;
	};
}
