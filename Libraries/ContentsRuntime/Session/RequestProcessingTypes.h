#pragma once

namespace ContentsRuntime::Session
{
	enum class ERequestProcessingPolicy : std::uint8_t
	{
		Exclusive = 0,
		AllowedWhileBusy = 1
	};

	enum class EBeginRequestResult : std::uint8_t
	{
		Started = 0,
		AllowedWithoutTracking = 1,
		AlreadyProcessing = 2,
		Disconnected = 3
	};

	struct FRequestProcessingToken final
	{
		std::uint64_t sessionId = 0;
		std::uint64_t operationId = 0;

		bool IsValid() const noexcept
		{
			return sessionId != 0 && operationId != 0;
		}
	};

	struct FRequestProcessingState final
	{
		bool processing = false;
		std::uint64_t nextOperationId = 1;
		std::uint64_t activeOperationId = 0;
		std::uint64_t requestId = 0;
		std::uint16_t requestOpcode = 0;
	};
}
