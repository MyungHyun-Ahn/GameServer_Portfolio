#pragma once

namespace CacheServer::Contents
{
	inline constexpr ContentsRuntime::Core::FContentId kRpcRouterContentId = 1;
	inline constexpr ContentsRuntime::Core::FContentId kPlayerCacheContentId = 2;

	struct SPlayerCachePolicy final
	{
		std::chrono::milliseconds gameOwnerLeaseDuration{30000};
		std::chrono::milliseconds idleEvictionDuration{300000};
		std::chrono::milliseconds maintenanceInterval{1000};
		std::chrono::milliseconds revokeTimeout{2000};
	};

	inline constexpr std::size_t GetPlayerCacheShardIndex(
		const std::uint64_t routingKey,
		const std::size_t shardCount) noexcept
	{
		return shardCount == 0 ? 0 : static_cast<std::size_t>(routingKey % shardCount);
	}
}
