#pragma once

#include <Windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Foundation/Diagnostics/Tls/FTlsCollectorRuntime.h"
#include "Foundation/Diagnostics/Timing/TimingTypes.h"
#include "Foundation/Diagnostics/Timing/FTimingMetricsRuntime.h"
#include "Foundation/Diagnostics/Timing/FTimingThreadLocalCollector.h"
#include "Foundation/Diagnostics/Timing/FTimingScope.h"
#include "Foundation/Diagnostics/Timing/FTimingCsvLogger.h"
