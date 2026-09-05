#pragma once

namespace WorldCore
{
	struct SSectorTaskOutput;
	struct SSectorUpdateContext;

	class FPlayerAttackSystem final
	{
	public:
		[[nodiscard]] bool Update(const SSectorUpdateContext& context, SSectorTaskOutput& outOutput, std::string& outError) const;
	};
}
