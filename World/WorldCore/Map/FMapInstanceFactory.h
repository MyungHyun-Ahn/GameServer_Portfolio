#pragma once

namespace WorldCore
{
	class FMapInstance;
	class ISectorExecutor;

	class FMapInstanceFactory final
	{
	public:
		[[nodiscard]] std::unique_ptr<FMapInstance> Create(FMapInstanceId mapInstanceId,
			const SMapDefinition& definition,
			EMapCreateResult& outResult,
			std::string& outError) const;
		[[nodiscard]] std::unique_ptr<FMapInstance> CreateWithExecutor(FMapInstanceId mapInstanceId,
			const SMapDefinition& definition,
			std::unique_ptr<ISectorExecutor> executor,
			EMapCreateResult& outResult,
			std::string& outError) const;
	};
}
