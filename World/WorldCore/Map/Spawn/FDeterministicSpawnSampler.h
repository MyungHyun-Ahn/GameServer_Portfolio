#pragma once

namespace WorldCore
{
	class FDeterministicSpawnSampler final
	{
	public:
		[[nodiscard]] static SVector2 BuildPosition(std::uint64_t seed,
			FMapInstanceId mapInstanceId,
			FEntityId entityId,
			std::uint64_t revision,
			std::uint32_t candidateIndex,
			float minimumX,
			float minimumY,
			float maximumX,
			float maximumY) noexcept;
	};
}
