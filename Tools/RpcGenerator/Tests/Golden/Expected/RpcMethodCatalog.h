#pragma once

// Generated from RPC YAML. Keep deterministic layout; do not format by hand.
// clang-format off
namespace Generated::Rpc
{
	struct FRpcMethodCatalogEntry final
	{
		std::uint32_t serviceId = 0;
		std::uint32_t methodId = 0;
		const char* name = nullptr;
		const char* routingKey = nullptr;
		bool hasRequestResponse = false;
		bool hasNotification = false;
	};

	inline constexpr FRpcMethodCatalogEntry kRpcMethodCatalog[] =
	{
		{42, 1, "Example.Query", "userId", true, false},
		{42, 2, "Example.Changed", "userId", false, true},
		{42, 3, "Example.Execute", "userId", true, true},
	};
}
// clang-format on
