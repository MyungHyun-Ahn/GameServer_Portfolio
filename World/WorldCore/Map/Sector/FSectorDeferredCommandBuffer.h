#pragma once

namespace WorldCore
{
	class FSectorGrid;
	struct SSectorTaskOutput;

	class FSectorDeferredCommandBuffer final
	{
	public:
		FSectorDeferredCommandBuffer();
		~FSectorDeferredCommandBuffer();

		FSectorDeferredCommandBuffer(const FSectorDeferredCommandBuffer&) = delete;
		FSectorDeferredCommandBuffer& operator=(const FSectorDeferredCommandBuffer&) = delete;

		void Build(std::span<const SSectorTaskOutput> taskOutputs);
		[[nodiscard]] bool Validate(const FSectorGrid& sectorGrid, std::string& outError) const;
		[[nodiscard]] bool Commit(FSectorGrid& sectorGrid, std::string& outError);
		void Clear();
		[[nodiscard]] std::size_t GetTransferCount() const noexcept;

	private:
		struct SImpl;
		std::unique_ptr<SImpl> m_impl;
	};
}
