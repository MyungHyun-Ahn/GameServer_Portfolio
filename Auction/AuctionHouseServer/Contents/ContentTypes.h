#pragma once

namespace AuctionHouseServer::Contents
{
	inline constexpr ContentsRuntime::Core::FContentId kRouterContentId = 1;
	inline constexpr ContentsRuntime::Core::FContentId kCommandContentId = 2;
	inline constexpr ContentsRuntime::Core::FContentId kAuthContentId = 3;
	inline constexpr ContentsRuntime::Core::FContentId kExpirationContentId = 4;
	inline constexpr std::uint32_t kCommandShardCount = 4;

	inline constexpr std::size_t GetCommandShardIndex(
		const std::uint64_t routingKey,
		const std::size_t shardCount) noexcept
	{
		return shardCount == 0 ? 0 : static_cast<std::size_t>(routingKey % shardCount);
	}
}
