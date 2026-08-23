#pragma once

namespace NetworkLib
{
	class IServer;
}

namespace NetworkLib::Core
{
	class FServerFactory
	{
	public:
		static std::unique_ptr<NetworkLib::IServer> Create(EBackendKind backendKind);
	};
}
