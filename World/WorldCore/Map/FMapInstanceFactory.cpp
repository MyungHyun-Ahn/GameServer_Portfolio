#include "WorldCorePch.h"

#include "WorldCore/Map/FMapInstance.h"
#include "WorldCore/Map/FMapInstanceFactory.h"
#include "WorldCore/Map/Sector/FSerialSectorExecutor.h"

namespace WorldCore
{
	namespace
	{
		std::atomic<FMapIncarnation> g_nextMapIncarnation = 1;

		bool ValidateCreateArguments(
			const FMapInstanceId mapInstanceId,
			const SMapDefinition& definition,
			EMapCreateResult& outResult,
			std::string& outError)
		{
			outResult = EMapCreateResult::InvalidDefinition;
			outError.clear();
			if (mapInstanceId == kInvalidMapInstanceId)
			{
				outError = "MapInstanceId must be non-zero.";
				return false;
			}
			return IsValidMapDefinition(definition, outError);
		}

		FMapIncarnation AcquireMapIncarnation() noexcept
		{
			FMapIncarnation incarnation = g_nextMapIncarnation.load(std::memory_order_relaxed);
			while (incarnation != kInvalidMapIncarnation && incarnation != std::numeric_limits<FMapIncarnation>::max())
			{
				if (g_nextMapIncarnation.compare_exchange_weak(
						incarnation, incarnation + 1, std::memory_order_relaxed, std::memory_order_relaxed))
				{
					return incarnation;
				}
			}
			return kInvalidMapIncarnation;
		}
	}

	std::unique_ptr<FMapInstance> FMapInstanceFactory::Create(
		const FMapInstanceId mapInstanceId,
		const SMapDefinition& definition,
		EMapCreateResult& outResult,
		std::string& outError) const
	{
		if (!ValidateCreateArguments(mapInstanceId, definition, outResult, outError))
		{
			return nullptr;
		}

		std::unique_ptr<ISectorExecutor> executor;
		switch (definition.sectorExecutionMode)
		{
			case ESectorExecutionMode::Serial:
				executor = std::make_unique<FSerialSectorExecutor>();
				break;
			case ESectorExecutionMode::TaskGraph:
				outResult = EMapCreateResult::UnsupportedExecutionMode;
				outError = "TaskGraph Sector execution is not implemented.";
				return nullptr;
			default:
				outError = "Unknown SectorExecutionMode.";
				return nullptr;
		}

		return CreateWithExecutor(mapInstanceId, definition, std::move(executor), outResult, outError);
	}

	std::unique_ptr<FMapInstance> FMapInstanceFactory::CreateWithExecutor(
		const FMapInstanceId mapInstanceId,
		const SMapDefinition& definition,
		std::unique_ptr<ISectorExecutor> executor,
		EMapCreateResult& outResult,
		std::string& outError) const
	{
		if (!ValidateCreateArguments(mapInstanceId, definition, outResult, outError))
		{
			return nullptr;
		}
		if (executor == nullptr)
		{
			outError = "MapInstance requires a Sector executor.";
			return nullptr;
		}

		const FMapIncarnation mapIncarnation = AcquireMapIncarnation();
		if (mapIncarnation == kInvalidMapIncarnation)
		{
			outError = "MapInstance incarnation space was exhausted.";
			return nullptr;
		}

		outResult = EMapCreateResult::Success;
		return std::unique_ptr<FMapInstance>(new FMapInstance(mapInstanceId, mapIncarnation, definition, std::move(executor)));
	}
}
