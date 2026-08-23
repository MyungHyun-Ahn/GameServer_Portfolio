#pragma once

namespace Foundation
{
	class FCompositeLogger final : public ILogger
	{
	public:
		void AddSink(std::shared_ptr<ILogger> logger);

	private:
		void WriteLog(ELogLevel logLevel, std::string_view category, std::string_view message) override;

		std::vector<std::shared_ptr<ILogger>> m_sinks;
	};
}
