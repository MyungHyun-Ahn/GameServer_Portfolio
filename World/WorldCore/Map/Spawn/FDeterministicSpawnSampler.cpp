#include "WorldCorePch.h"

#include "WorldCore/Map/Spawn/FDeterministicSpawnSampler.h"

namespace WorldCore
{
	namespace
	{
		std::uint64_t Mix64(
			const std::uint64_t value) noexcept
		{
			std::uint64_t mixed = value + 0x9E3779B97F4A7C15ull;
			mixed = (mixed ^ (mixed >> 30u)) * 0xBF58476D1CE4E5B9ull;
			mixed = (mixed ^ (mixed >> 27u)) * 0x94D049BB133111EBull;
			return mixed ^ (mixed >> 31u);
		}

		float BuildCoordinate(
			const float minimum,
			const float maximumExclusive,
			const std::uint64_t randomValue) noexcept
		{
			constexpr double kInverseUnit = 1.0 / static_cast<double>(std::uint64_t{1} << 53u);
			const double unit = static_cast<double>(randomValue >> 11u) * kInverseUnit;
			float coordinate = static_cast<float>(
				static_cast<double>(minimum) + (static_cast<double>(maximumExclusive) - static_cast<double>(minimum)) * unit);
			if (coordinate >= maximumExclusive)
			{
				coordinate = std::nextafter(maximumExclusive, minimum);
			}
			return std::max(coordinate, minimum);
		}
	}

	SVector2 FDeterministicSpawnSampler::BuildPosition(
		const std::uint64_t seed,
		const FMapInstanceId mapInstanceId,
		const FEntityId entityId,
		const std::uint64_t revision,
		const std::uint32_t candidateIndex,
		const float minimumX,
		const float minimumY,
		const float maximumX,
		const float maximumY) noexcept
	{
		std::uint64_t key = Mix64(seed);
		key = Mix64(key ^ mapInstanceId);
		key = Mix64(key ^ entityId);
		key = Mix64(key ^ revision);
		key = Mix64(key ^ candidateIndex);
		return {BuildCoordinate(minimumX, maximumX, Mix64(key ^ 0xA0761D6478BD642Full)),
			BuildCoordinate(minimumY, maximumY, Mix64(key ^ 0xE7037ED1A0B428DBull))};
	}
}
