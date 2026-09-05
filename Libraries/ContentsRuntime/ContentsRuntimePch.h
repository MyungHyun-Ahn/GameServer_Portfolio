#pragma once

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

// Platform and formatting
#include <Windows.h>
#include <format>

// ContentsRuntime and shared library contracts
#include "Core/ContentRuntimeTypes.h"
#include "Session/RequestProcessingTypes.h"
#include "Session/FContentSession.h"
#include "../Foundation/Diagnostics/Timing/TimingTypes.h"
#include "../Foundation/Diagnostics/Tls/FTlsCollectorRuntime.h"
#include "../Foundation/Diagnostics/Timing/FTimingMetricsRuntime.h"
#include "../Foundation/Diagnostics/Timing/FTimingScope.h"
#include "../Foundation/Diagnostics/Timing/FTimingThreadLocalCollector.h"
#include "../Foundation/Ids/IIdAllocator.h"
#include "../NetworkLib/Packet/Framing/ContentHeader.h"
#include "../NetworkLib/Containers/LockFreeCommon.h"
#include "../NetworkLib/Memory/FLockFreeMemoryPool.h"
#include "../NetworkLib/Memory/FTlsMemoryPool.h"
#include "../NetworkLib/Packet/Buffer/FPacketBuffer.h"
#include "../NetworkLib/Packet/Serialization/FPacketWriter.h"
#include "../NetworkLib/Packet/Serialization/FPacketReader.h"
#include "../NetworkLib/Packet/View/FPacketView.h"
#include "../NetworkLib/Servers/IServer.h"
#include "../NetworkLib/Packet/Serialization/FPacketSerialization.h"
#include "../NetworkLib/Packet/View/FBorrowedViewGuard.h"
#include "../NetworkLib/Packet/Serialization/IContentPacket.h"
#include "Bridge/IContentBridge.h"
#include "../Foundation/Logging/LoggingTypes.h"
