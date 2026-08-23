#pragma once

namespace Foundation
{
	class ILogger
	{
	public:
		virtual ~ILogger() = default;

		void Log(
			ELogLevel logLevel,
			std::string_view category,
			std::string_view message)
		{
			WriteLog(logLevel, category, message);
		}

		template <typename... TArgs>
			requires(sizeof...(TArgs) > 0)
		void Log(
			ELogLevel logLevel,
			std::string_view category,
			std::format_string<TArgs...> format,
			TArgs&&... args)
		{
			LogFormat(logLevel, category, format, std::forward<TArgs>(args)...);
		}

	private:
		virtual void WriteLog(ELogLevel logLevel, std::string_view category, std::string_view message) = 0;

		template <typename... TArgs>
		void LogFormat(
			ELogLevel logLevel,
			std::string_view category,
			std::format_string<TArgs...> format,
			TArgs&&... args)
		{
			WriteLog(logLevel, category, std::format(format, std::forward<TArgs>(args)...));
		}
	};
}
