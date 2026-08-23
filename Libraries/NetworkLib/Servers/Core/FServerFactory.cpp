#include "NetLibPch.h"

#include "Servers/Core/FServerFactory.h"

#include "Servers/Core/FIocpServer.h"
#include "Servers/Core/FRioServer.h"
#include "Servers/Core/FStubServer.h"

namespace NetworkLib::Core
{
	std::unique_ptr<NetworkLib::IServer> FServerFactory::Create(
		EBackendKind backendKind)
	{
		switch (backendKind)
		{
			case EBackendKind::Iocp:
				return std::make_unique<FIocpServer>();
			case EBackendKind::Rio:
				return std::make_unique<FRioServer>();
			case EBackendKind::BoostAsio:
				return std::make_unique<FStubServer>(backendKind);
			default:
				return nullptr;
		}
	}
}
