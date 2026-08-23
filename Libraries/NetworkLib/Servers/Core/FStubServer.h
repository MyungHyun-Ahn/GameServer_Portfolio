#pragma once

namespace NetworkLib::Core
{
	class FStubServer final : public IServer
	{
	public:
		explicit FStubServer(EBackendKind backendKind);

		bool Start(const SServerConfig& serverConfig, IApplicationHandler& applicationHandler) override;
		void Stop() override;
		bool SendPacket(std::uint64_t sessionId, NetworkLib::Packet::Serialization::FOutgoingContentPacket&& packet) override;
		bool Disconnect(std::uint64_t sessionId) override;
		EBackendKind GetBackendKind() const override;
		SServerStats GetStatsSnapshot() const override;

	private:
		EBackendKind m_backendKind;
	};
}
