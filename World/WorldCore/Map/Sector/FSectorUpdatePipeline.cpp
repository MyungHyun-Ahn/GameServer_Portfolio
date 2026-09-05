#include "WorldCorePch.h"

#include "WorldCore/Map/Sector/FSectorGrid.h"
#include "WorldCore/Map/Sector/FSectorUpdateContext.h"
#include "WorldCore/Map/Sector/FSectorUpdatePipeline.h"
#include "WorldCore/Map/Sector/Systems/FMonsterAiSystem.h"
#include "WorldCore/Map/Sector/Systems/FPlayerAttackSystem.h"
#include "WorldCore/Map/Sector/Systems/FPlayerMovementSystem.h"

namespace WorldCore
{
	bool FSectorUpdatePipeline::Update(
		const SSectorUpdateContext& context,
		SSectorTaskOutput& outOutput,
		std::string& outError) const
	{
		outError.clear();
		outOutput = {};
		outOutput.sectorId = context.task.sectorId;
		outOutput.stableOrder = context.task.stableOrder;
		if (!context.sectorGrid.IsValidSectorId(context.task.sectorId))
		{
			outError = "Sector task contains an invalid SectorId.";
			return false;
		}

		const FPlayerMovementSystem playerMovementSystem;
		if (!playerMovementSystem.Update(context, outOutput, outError))
		{
			return false;
		}

		const FPlayerAttackSystem playerAttackSystem;
		if (!playerAttackSystem.Update(context, outOutput, outError))
		{
			return false;
		}

		const FMonsterAiSystem monsterAiSystem;
		return monsterAiSystem.Update(context, outOutput, outError);
	}
}
