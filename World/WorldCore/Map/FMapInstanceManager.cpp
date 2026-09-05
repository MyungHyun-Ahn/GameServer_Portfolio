#include "WorldCorePch.h"

#include "WorldCore/Map/FMapInstance.h"
#include "WorldCore/Map/FMapInstanceFactory.h"
#include "WorldCore/Map/FMapInstanceManager.h"

namespace WorldCore
{
	struct FMapInstanceManager::SImpl final
	{
		FMapInstanceFactory factory;
		std::map<FMapInstanceId, std::unique_ptr<FMapInstance>> mapInstances;
	};

	FMapInstanceManager::FMapInstanceManager()
		: m_impl(std::make_unique<SImpl>())
	{
	}

	FMapInstanceManager::~FMapInstanceManager() = default;

	FMapInstance* FMapInstanceManager::CreateMap(
		const FMapInstanceId mapInstanceId,
		const SMapDefinition& definition,
		EMapCreateResult& outResult,
		std::string& outError)
	{
		if (m_impl->mapInstances.contains(mapInstanceId))
		{
			outResult = EMapCreateResult::DuplicateMapInstance;
			outError = "MapInstanceId is already registered in this Manager.";
			return nullptr;
		}

		std::unique_ptr<FMapInstance> mapInstance = m_impl->factory.Create(mapInstanceId, definition, outResult, outError);
		if (mapInstance == nullptr)
		{
			return nullptr;
		}
		FMapInstance* const result = mapInstance.get();
		m_impl->mapInstances.emplace(mapInstanceId, std::move(mapInstance));
		return result;
	}

	FMapInstance* FMapInstanceManager::CreateMapWithExecutor(
		const FMapInstanceId mapInstanceId,
		const SMapDefinition& definition,
		std::unique_ptr<ISectorExecutor> executor,
		EMapCreateResult& outResult,
		std::string& outError)
	{
		if (m_impl->mapInstances.contains(mapInstanceId))
		{
			outResult = EMapCreateResult::DuplicateMapInstance;
			outError = "MapInstanceId is already registered in this Manager.";
			return nullptr;
		}

		std::unique_ptr<FMapInstance> mapInstance =
			m_impl->factory.CreateWithExecutor(mapInstanceId, definition, std::move(executor), outResult, outError);
		if (mapInstance == nullptr)
		{
			return nullptr;
		}
		FMapInstance* const result = mapInstance.get();
		m_impl->mapInstances.emplace(mapInstanceId, std::move(mapInstance));
		return result;
	}

	bool FMapInstanceManager::RemoveMap(
		const FMapInstanceId mapInstanceId)
	{
		const auto found = m_impl->mapInstances.find(mapInstanceId);
		if (found == m_impl->mapInstances.end() || found->second->GetTickExecutionState() != EMapTickExecutionState::Idle)
		{
			return false;
		}
		m_impl->mapInstances.erase(found);
		return true;
	}

	FMapInstance* FMapInstanceManager::FindMap(
		const FMapInstanceId mapInstanceId) noexcept
	{
		const auto found = m_impl->mapInstances.find(mapInstanceId);
		return found == m_impl->mapInstances.end() ? nullptr : found->second.get();
	}

	const FMapInstance* FMapInstanceManager::FindMap(
		const FMapInstanceId mapInstanceId) const noexcept
	{
		const auto found = m_impl->mapInstances.find(mapInstanceId);
		return found == m_impl->mapInstances.end() ? nullptr : found->second.get();
	}

	std::size_t FMapInstanceManager::GetMapCount() const noexcept
	{
		return m_impl->mapInstances.size();
	}

	EMapTickCompletionResult FMapInstanceManager::CompleteTickExecution(
		SMapTickExecutionCompletion completion)
	{
		FMapInstance* const mapInstance = FindMap(completion.ticket.mapInstanceId);
		if (mapInstance == nullptr)
		{
			return EMapTickCompletionResult::NotExecuting;
		}
		return mapInstance->CompleteTickExecution(std::move(completion));
	}

	std::vector<SMapTickResult> FMapInstanceManager::TickAll()
	{
		std::vector<SMapTickResult> results;
		results.reserve(m_impl->mapInstances.size());
		for (auto& [mapInstanceId, mapInstance] : m_impl->mapInstances)
		{
			(void)mapInstanceId;
			results.push_back(mapInstance->Tick());
		}
		return results;
	}

	std::uint64_t FMapInstanceManager::GetStateHash() const
	{
		std::uint64_t hash = 0x84222325CBF29CE4ull;
		for (const auto& [mapInstanceId, mapInstance] : m_impl->mapInstances)
		{
			hash ^= mapInstanceId + 0x9E3779B97F4A7C15ull + (hash << 6u) + (hash >> 2u);
			hash ^= mapInstance->GetStateHash() + 0x9E3779B97F4A7C15ull + (hash << 6u) + (hash >> 2u);
		}
		return hash;
	}
}
