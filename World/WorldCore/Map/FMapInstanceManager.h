#pragma once

namespace WorldCore
{
	class FMapInstance;
	class ISectorExecutor;

	class FMapInstanceManager final
	{
	public:
		FMapInstanceManager();
		~FMapInstanceManager();

		FMapInstanceManager(const FMapInstanceManager&) = delete;
		FMapInstanceManager& operator=(const FMapInstanceManager&) = delete;

		[[nodiscard]] FMapInstance* CreateMap(FMapInstanceId mapInstanceId,
			const SMapDefinition& definition,
			EMapCreateResult& outResult,
			std::string& outError);
		[[nodiscard]] FMapInstance* CreateMapWithExecutor(FMapInstanceId mapInstanceId,
			const SMapDefinition& definition,
			std::unique_ptr<ISectorExecutor> executor,
			EMapCreateResult& outResult,
			std::string& outError);
		[[nodiscard]] bool RemoveMap(FMapInstanceId mapInstanceId);
		[[nodiscard]] FMapInstance* FindMap(FMapInstanceId mapInstanceId) noexcept;
		[[nodiscard]] const FMapInstance* FindMap(FMapInstanceId mapInstanceId) const noexcept;
		[[nodiscard]] std::size_t GetMapCount() const noexcept;
		[[nodiscard]] EMapTickCompletionResult CompleteTickExecution(SMapTickExecutionCompletion completion);
		[[nodiscard]] std::vector<SMapTickResult> TickAll();
		[[nodiscard]] std::uint64_t GetStateHash() const;

	private:
		struct SImpl;
		std::unique_ptr<SImpl> m_impl;
	};
}
