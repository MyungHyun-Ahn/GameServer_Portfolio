#pragma once

namespace Foundation
{
	class FConsoleLogger final : public ILogger
	{
	public:
		explicit FConsoleLogger(const SLogConfig& logConfig);
		static void WriteLine(std::string_view line);

	private:
		void WriteLog(ELogLevel logLevel, std::string_view category, std::string_view message) override;

		SLogConfig m_logConfig;
	};
}
