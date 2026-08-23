#pragma once

namespace Foundation
{
	class FCrashDump final
	{
	public:
		static bool Initialize(const SCrashDumpConfig& config);
		static void Shutdown();
		static bool IsInitialized();
		static bool WriteManualDumpForDiagnostics();

	private:
		FCrashDump() = delete;
		~FCrashDump() = delete;
		FCrashDump(const FCrashDump&) = delete;
		FCrashDump& operator=(const FCrashDump&) = delete;
	};
}
