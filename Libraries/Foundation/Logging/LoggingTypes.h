#pragma once

namespace Foundation
{
	enum class ELogLevel : std::uint32_t
	{
		Debug = 0,
		Info = 1,
		Warn = 2,
		Error = 3
	};

	struct SLogConfig
	{
		ELogLevel minimumLevel = ELogLevel::Info;
		std::string outputDirectory = "logs";
		bool consoleEnabled = true;
		bool fileEnabled = true;
		bool includeThreadId = true;
	};
}
