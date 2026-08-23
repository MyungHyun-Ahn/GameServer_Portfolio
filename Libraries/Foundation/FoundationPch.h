#pragma once

// Platform
#include <Windows.h>

// C++ standard library
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <charconv>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <shared_mutex>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// Platform and standard extensions
#include <cctype>
#include <DbgHelp.h>
#include <crtdbg.h>
#include <format>

// Foundation contracts and stable runtime bases
#include "Config/ConfigTypes.h"
#include "Logging/LoggingTypes.h"
#include "Logging/ILogger.h"
#include "Diagnostics/CrashDumpTypes.h"
#include "Diagnostics/Rtt/RttTypes.h"
#include "Diagnostics/Timing/TimingTypes.h"
#include "Diagnostics/Tls/FTlsCollectorRuntime.h"
#include "Ids/IIdAllocator.h"
