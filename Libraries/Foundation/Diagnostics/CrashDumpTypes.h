#pragma once

namespace Foundation
{
	enum class ECrashDumpType
	{
		Normal = 0,
		WithFullMemory = 1
	};

	struct SCrashDumpConfig
	{
		bool enabled = true;
		std::string outputDirectory = "dumps";
		ECrashDumpType dumpType = ECrashDumpType::WithFullMemory;
		bool allowOnlySingleDump = true;
		bool installInvalidParameterHandler = true;
		bool installPurecallHandler = true;
		bool installCrtReportHook = true;
		std::shared_ptr<ILogger> logger;
	};
}
