#include "FoundationPch.h"

#include "LogFormatting.h"

namespace
{
	std::string BuildTimestamp()
	{
		const auto now = std::chrono::system_clock::now();
		const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);

		std::tm localTime{};
		localtime_s(&localTime, &nowTime);

		std::ostringstream oss;
		oss << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
		return oss.str();
	}

	const char* ToString(
		Foundation::ELogLevel logLevel)
	{
		using Foundation::ELogLevel;

		switch (logLevel)
		{
			case ELogLevel::Debug:
				return "Debug";
			case ELogLevel::Info:
				return "Info";
			case ELogLevel::Warn:
				return "Warn";
			case ELogLevel::Error:
				return "Error";
			default:
				return "Unknown";
		}
	}

	std::string BuildThreadPrefix(
		const Foundation::SLogConfig& logConfig)
	{
		if (!logConfig.includeThreadId)
		{
			return {};
		}

		std::ostringstream oss;
		oss << " [T" << GetCurrentThreadId() << "]";
		return oss.str();
	}
}

namespace Foundation::Logging
{
	std::string BuildDateStamp()
	{
		const auto now = std::chrono::system_clock::now();
		const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);

		std::tm localTime{};
		localtime_s(&localTime, &nowTime);

		std::ostringstream oss;
		oss << std::put_time(&localTime, "%Y%m%d");
		return oss.str();
	}

	std::string BuildLine(
		const SLogConfig& logConfig,
		ELogLevel logLevel,
		std::string_view category,
		std::string_view message)
	{
		std::ostringstream oss;
		oss << "[" << category << "] "
			<< "[" << BuildTimestamp() << "] "
			<< "[" << ToString(logLevel) << "]" << BuildThreadPrefix(logConfig) << " " << message;
		return oss.str();
	}

	bool ShouldWrite(
		const SLogConfig& logConfig,
		ELogLevel logLevel)
	{
		return static_cast<std::uint32_t>(logLevel) >= static_cast<std::uint32_t>(logConfig.minimumLevel);
	}
}
