#pragma once

namespace RpcLib::Transport
{
	class FServerRpcTransport final : public IRpcTransport
	{
	public:
		void Bind(NetworkLib::IServer& server) noexcept;
		void Unbind() noexcept;
		bool IsBound() const noexcept;

		bool SendRequest(std::uint64_t networkSessionId, const Protocol::FRpcRequest& request) override;
		bool SendResponse(std::uint64_t networkSessionId, const Protocol::FRpcResponse& response) override;
		bool SendNotification(std::uint64_t networkSessionId, const Protocol::FRpcNotification& notification) override;
		bool SendHelloRequest(std::uint64_t networkSessionId, const Protocol::FRpcHelloRequest& request);
		bool SendHelloResponse(std::uint64_t networkSessionId, const Protocol::FRpcHelloResponse& response);

	private:
		bool SendPayload(std::uint64_t networkSessionId, Protocol::ERpcWireOpcode opcode, std::vector<char>&& payload);

	private:
		std::atomic<NetworkLib::IServer*> m_server = nullptr;
	};
}
