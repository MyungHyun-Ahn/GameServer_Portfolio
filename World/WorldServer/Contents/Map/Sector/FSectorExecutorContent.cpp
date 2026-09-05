#include "WorldServerPch.h"

#include "WorldServer/Contents/Map/Sector/FSectorExecutorContent.h"

namespace WorldServer::Contents
{
	FSectorExecutorContent::FSectorExecutorContent(
		const ContentsRuntime::Core::FContentInstanceId contentInstanceId,
		const std::uint64_t maxPacketQueueDepth) noexcept
		: m_contentInstanceId(contentInstanceId)
		, m_maxPacketQueueDepth(maxPacketQueueDepth)
	{
	}

	ContentsRuntime::Core::FContentId FSectorExecutorContent::GetContentId() const noexcept
	{
		return kSectorExecutorContentId;
	}

	ContentsRuntime::Core::FContentInstanceId FSectorExecutorContent::GetContentInstanceId() const noexcept
	{
		return m_contentInstanceId;
	}

	std::uint32_t FSectorExecutorContent::GetTargetFps() const noexcept
	{
		return 1;
	}

	std::uint64_t FSectorExecutorContent::GetMaxPacketQueueDepth() const noexcept
	{
		return m_maxPacketQueueDepth;
	}

	void FSectorExecutorContent::OnEnter(
		std::uint64_t,
		std::uint64_t,
		ContentsRuntime::Bridge::IContentBridge&)
	{
	}

	void FSectorExecutorContent::OnLeave(
		std::uint64_t,
		std::uint64_t,
		ContentsRuntime::Bridge::IContentBridge&)
	{
	}

	void FSectorExecutorContent::OnPacket(
		std::uint64_t,
		std::uint64_t,
		std::uint16_t,
		std::span<const char>,
		ContentsRuntime::Bridge::IContentBridge&)
	{
	}

	void FSectorExecutorContent::OnFrame(
		int,
		ContentsRuntime::Bridge::IContentBridge&)
	{
	}
}
