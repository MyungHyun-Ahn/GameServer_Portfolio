#include "NetLibPch.h"

#include "Servers/Core/BackendTypes.h"
#include "Servers/Core/FStubServer.h"

namespace NetworkLib::Core
{
	FStubServer::FStubServer(
		EBackendKind backendKind)
		: m_backendKind(backendKind)
	{
	}

	bool FStubServer::Start(
		const SServerConfig&,
		IApplicationHandler&)
	{
		std::cerr << "선택한 백엔드는 아직 구현하지 않았습니다.\n";
		return false;
	}

	void FStubServer::Stop() {}

	bool FStubServer::SendPacket(
		std::uint64_t,
		NetworkLib::Packet::Serialization::FOutgoingContentPacket&&)
	{
		return false;
	}

	bool FStubServer::Disconnect(
		std::uint64_t)
	{
		return false;
	}

	EBackendKind FStubServer::GetBackendKind() const
	{
		return m_backendKind;
	}

	SServerStats FStubServer::GetStatsSnapshot() const
	{
		return {};
	}
}
