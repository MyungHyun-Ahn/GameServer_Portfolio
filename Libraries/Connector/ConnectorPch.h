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
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// Connector public contracts
#include "../Foundation/Diagnostics/Timing/TimingTypes.h"
#include "../Foundation/Diagnostics/Tls/FTlsCollectorRuntime.h"
#include "../Foundation/Diagnostics/Timing/FTimingMetricsRuntime.h"
#include "../Foundation/Diagnostics/Timing/FTimingScope.h"
#include "../Foundation/Diagnostics/Timing/FTimingThreadLocalCollector.h"
#include "Interfaces/IChatTicketStore.h"
#include "Config/RedisChatTicketStoreTypes.h"
#include "MySql/MySqlTypes.h"
#include "MySql/FMySqlConnection.h"
#include "MySql/FMySqlTransaction.h"
#include "MySql/FThreadAffinedMySqlCluster.h"
#include "MySql/MySqlResultReader.h"
