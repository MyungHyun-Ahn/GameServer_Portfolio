#pragma once

namespace ClientNetworkLib
{
	class FClientNetwork final
	{
	public:
		explicit FClientNetwork(const FClientNetworkConfig& config = {});
		~FClientNetwork();

		FClientNetwork(const FClientNetwork&) = delete;
		FClientNetwork& operator=(const FClientNetwork&) = delete;
		FClientNetwork(FClientNetwork&&) = delete;
		FClientNetwork& operator=(FClientNetwork&&) = delete;

	public:
		bool Start(std::string& outErrorMessage);
		void Stop();

		bool IsRunning() const noexcept;
		const FClientNetworkConfig& GetConfig() const noexcept;

		bool ConnectSession(FClientSessionId& outSessionId, std::string& outErrorMessage);
		bool DisconnectSession(FClientSessionId sessionId, const std::string& reasonMessage = {});

		bool SendPacket(FClientSessionId sessionId,
			const NetworkLib::Packet::Serialization::IContentPacket& packet,
			std::uint8_t randomKey,
			std::string& outErrorMessage);
		bool SendPacketBuffer(FClientSessionId sessionId, std::vector<char>&& packetBuffer, std::string& outErrorMessage);

		std::size_t PollEvents(std::vector<FClientEvent>& outEvents, std::size_t maxEventCount = std::numeric_limits<std::size_t>::max());
		bool TryPopEvent(FClientEvent& outEvent);

		std::size_t GetActiveSessionCount() const;

	private:
		struct FImpl;
		std::unique_ptr<FImpl> m_impl;
	};
}
