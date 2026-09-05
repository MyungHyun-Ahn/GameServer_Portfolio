#pragma once

namespace WorldCore
{
	struct SSectorTaskOutput;
	struct SSectorUpdateContext;

	class FSectorUpdatePipeline final
	{
	public:
		[[nodiscard]] bool Update(const SSectorUpdateContext& context, SSectorTaskOutput& outOutput, std::string& outError) const;
	};
}
