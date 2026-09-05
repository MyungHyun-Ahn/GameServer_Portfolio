#include "FoundationPch.h"

#include "FConsoleLogger.h"

#include "LogFormatting.h"

namespace
{
	std::mutex g_consoleMutex;

	void WriteConsoleLine(
		const std::string_view line,
		const bool shouldFlush)
	{
		const std::lock_guard<std::mutex> lock(g_consoleMutex);
		std::cout << line << '\n';
		if (shouldFlush)
		{
			std::cout.flush();
		}
	}
}

namespace Foundation
{
	FConsoleLogger::FConsoleLogger(
		const SLogConfig& logConfig)
		: m_logConfig(logConfig)
	{
	}

	void FConsoleLogger::WriteLog(
		ELogLevel logLevel,
		std::string_view category,
		std::string_view message)
	{
		if (!m_logConfig.consoleEnabled || !Logging::ShouldWrite(m_logConfig, logLevel))
		{
			return;
		}

		WriteConsoleLine(Logging::BuildLine(m_logConfig, logLevel, category, message), logLevel >= ELogLevel::Error);
	}

	void FConsoleLogger::FlushLog()
	{
		const std::lock_guard<std::mutex> lock(g_consoleMutex);
		std::cout.flush();
	}

	void FConsoleLogger::WriteLine(
		const std::string_view line)
	{
		WriteConsoleLine(line, false);
	}
}
