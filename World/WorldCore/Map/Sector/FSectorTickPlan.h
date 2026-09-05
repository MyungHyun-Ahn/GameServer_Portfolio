#pragma once

namespace WorldCore
{
	class FSectorGrid;

	inline constexpr std::uint32_t kSectorTaskWaveCount = 4;

	struct SSectorTaskWave final
	{
		std::uint32_t waveIndex = 0;
		std::vector<SSectorTask> tasks;
	};

	class FSectorTickPlan final
	{
	public:
		FSectorTickPlan();
		~FSectorTickPlan();

		FSectorTickPlan(const FSectorTickPlan&) = delete;
		FSectorTickPlan& operator=(const FSectorTickPlan&) = delete;

		[[nodiscard]] bool Build(const FSectorGrid& sectorGrid, std::vector<SSectorTask> tasks, std::string& outError);
		void Clear() noexcept;

		[[nodiscard]] std::uint64_t GetTickIndex() const noexcept;
		[[nodiscard]] std::size_t GetTaskCount() const noexcept;
		[[nodiscard]] std::span<const SSectorTaskWave> GetWaves() const noexcept;

	private:
		struct SImpl;
		std::unique_ptr<SImpl> m_impl;
	};
}
