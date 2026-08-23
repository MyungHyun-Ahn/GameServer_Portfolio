#pragma once

namespace Foundation
{
	class FFileLoggerState;

	class FFileLogger final : public ILogger
	{
	public:
		explicit FFileLogger(const SLogConfig& logConfig);
		~FFileLogger() override;

	private:
		void WriteLog(ELogLevel logLevel, std::string_view category, std::string_view message) override;

		SLogConfig m_logConfig;
		std::unique_ptr<FFileLoggerState> m_state;
	};
}
