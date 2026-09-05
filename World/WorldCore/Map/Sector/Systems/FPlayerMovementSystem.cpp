#include "WorldCorePch.h"

#include "WorldCore/Entity/FEntityRegistry.h"
#include "WorldCore/Entity/FPlayerEntity.h"
#include "WorldCore/Map/Sector/FSectorGrid.h"
#include "WorldCore/Map/Sector/FSectorUpdateContext.h"
#include "WorldCore/Map/Sector/Systems/FPlayerMovementSystem.h"

namespace WorldCore
{
	bool FPlayerMovementSystem::Update(
		const SSectorUpdateContext& context,
		SSectorTaskOutput& outOutput,
		std::string& outError) const
	{
		const SSectorTask& task = context.task;
		const FEntityRegistry& entityRegistry = context.entityRegistry;
		const FSectorGrid& sectorGrid = context.sectorGrid;
		const SMapDefinition& mapDefinition = context.mapDefinition;

		outOutput.moveResults.reserve(task.moveCommands.size());
		outOutput.sectorTransfers.reserve(task.moveCommands.size());
		for (const SMoveCommand& command : task.moveCommands)
		{
			const FPlayerEntity* const player = entityRegistry.FindPlayer(command.entityId);
			if (player == nullptr)
			{
				outError = "Sector task references an unknown EntityId.";
				return false;
			}
			if (player->GetSectorId() != task.sectorId || !sectorGrid.ContainsEntity(task.sectorId, command.entityId))
			{
				outError = "Entity ownership does not match the Sector task.";
				return false;
			}
			if (command.sequence <= player->GetLastMoveSequence())
			{
				continue;
			}

			SMoveResult moveResult{};
			moveResult.entityId = command.entityId;
			moveResult.sequence = command.sequence;
			moveResult.moveState = command.moveState;
			moveResult.previousSectorId = task.sectorId;

			const float maximumErrorSquared = mapDefinition.maxAcceptedPositionError * mapDefinition.maxAcceptedPositionError;
			if (!IsFinite(command.clientPosition) ||
				GetDistanceSquared(command.clientPosition, player->GetPosition()) > maximumErrorSquared)
			{
				moveResult.acceptedPosition = player->GetPosition();
				moveResult.isCorrected = true;
			}
			else
			{
				moveResult.acceptedPosition = sectorGrid.ClampInsideWorld(command.clientPosition);
				moveResult.isCorrected = moveResult.acceptedPosition != command.clientPosition;
			}

			if (!IsFinite(command.direction))
			{
				moveResult.direction = player->GetDirection();
				moveResult.isCorrected = true;
			}
			else
			{
				moveResult.direction = NormalizeOrZero(command.direction);
			}

			if (!sectorGrid.TryResolveSector(moveResult.acceptedPosition, moveResult.currentSectorId))
			{
				outError = "Accepted movement position cannot be resolved to a Sector.";
				return false;
			}

			if (moveResult.previousSectorId != moveResult.currentSectorId)
			{
				outOutput.sectorTransfers.push_back({command.entityId, moveResult.previousSectorId, moveResult.currentSectorId});
			}
			outOutput.moveResults.push_back(moveResult);
		}
		return true;
	}
}
