#include "FoundationPch.h"

#include "FFileLogger.h"

#include "LogFormatting.h"

namespace Foundation
{
	namespace
	{
		struct SCategoryFile
		{
			std::string dateStamp;
			std::ofstream outputFile;
		};
	}

	class FFileLoggerState
	{
	public:
		std::mutex mutex;
		std::unordered_map<std::string, SCategoryFile> categoryFiles;
	};

	FFileLogger::FFileLogger(
		const SLogConfig& logConfig)
		: m_logConfig(logConfig)
		, m_state(std::make_unique<FFileLoggerState>())
	{
	}

	FFileLogger::~FFileLogger()
	{
		const std::lock_guard<std::mutex> lock(m_state->mutex);
		for (auto& [category, categoryFile] : m_state->categoryFiles)
		{
			(void)category;
			if (categoryFile.outputFile.is_open())
			{
				categoryFile.outputFile.flush();
				categoryFile.outputFile.close();
			}
		}
	}

	void FFileLogger::WriteLog(
		ELogLevel logLevel,
		std::string_view category,
		std::string_view message)
	{
		if (!m_logConfig.fileEnabled || !Logging::ShouldWrite(m_logConfig, logLevel))
		{
			return;
		}

		const std::string line = Logging::BuildLine(m_logConfig, logLevel, category, message);
		const std::string categoryName(category);
		const std::string dateStamp = Logging::BuildDateStamp();
		const std::lock_guard<std::mutex> lock(m_state->mutex);

		SCategoryFile& categoryFile = m_state->categoryFiles[categoryName];
		if (!categoryFile.outputFile.is_open() || categoryFile.dateStamp != dateStamp)
		{
			if (categoryFile.outputFile.is_open())
			{
				categoryFile.outputFile.flush();
				categoryFile.outputFile.close();
			}

			const std::filesystem::path directoryPath = std::filesystem::path(m_logConfig.outputDirectory) / categoryName;
			std::error_code directoryError;
			std::filesystem::create_directories(directoryPath, directoryError);
			if (directoryError)
			{
				return;
			}

			const std::filesystem::path filePath = directoryPath / (dateStamp + "_" + categoryName + ".log");
			categoryFile.outputFile.open(filePath, std::ios::out | std::ios::app);
			if (!categoryFile.outputFile.is_open())
			{
				return;
			}
			categoryFile.dateStamp = dateStamp;
		}

		categoryFile.outputFile << line << '\n';
		if (logLevel >= ELogLevel::Error)
		{
			categoryFile.outputFile.flush();
		}
	}

	void FFileLogger::FlushLog()
	{
		const std::lock_guard<std::mutex> lock(m_state->mutex);
		for (auto& [category, categoryFile] : m_state->categoryFiles)
		{
			(void)category;
			if (categoryFile.outputFile.is_open())
			{
				categoryFile.outputFile.flush();
			}
		}
	}
}
