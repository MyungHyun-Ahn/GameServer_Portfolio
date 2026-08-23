#pragma once

// Network platform
#include <WinSock2.h>
#include <Windows.h>
#include <WS2tcpip.h>
#include <MSWSock.h>

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
#include <stdexcept>
#include <format>

// Shared library contracts
#include "../../Libraries/Foundation/Logging/LoggingTypes.h"
#include "../../Libraries/Foundation/Logging/ILogger.h"
#include "../../Libraries/Foundation/Diagnostics/CrashDumpTypes.h"
#include "../../Libraries/ContentsRuntime/Core/ContentRuntimeTypes.h"
#include "../../Libraries/NetworkLib/Packet/Framing/ContentHeader.h"
#include "../../Libraries/NetworkLib/Containers/LockFreeCommon.h"
#include "../../Libraries/NetworkLib/Memory/FLockFreeMemoryPool.h"
#include "../../Libraries/NetworkLib/Memory/FTlsMemoryPool.h"
#include "../../Libraries/NetworkLib/Packet/Buffer/FPacketBuffer.h"
#include "../../Libraries/NetworkLib/Packet/Serialization/FPacketWriter.h"
#include "../../Libraries/NetworkLib/Packet/Serialization/FPacketReader.h"
#include "../../Libraries/NetworkLib/Packet/View/FPacketView.h"
#include "../../Libraries/NetworkLib/Servers/IServer.h"
#include "../../Libraries/NetworkLib/Packet/Serialization/FPacketSerialization.h"
#include "../../Libraries/NetworkLib/Packet/View/FBorrowedViewGuard.h"
#include "../../Libraries/ContentsRuntime/Bridge/IContentBridge.h"
#include "../../Libraries/ContentsRuntime/Threading/FContentThread.h"
#include "../../Libraries/NetworkLib/Crypto/PacketCipherTypes.h"
#include "../../Libraries/NetworkLib/Crypto/IPacketCipher.h"
#include "../../Libraries/ContentsRuntime/Core/IContent.h"
#include "../../Libraries/NetworkLib/Packet/Serialization/IContentPacket.h"
#include "../../Libraries/NetworkLib/Packet/Buffer/FRecvBuffer.h"
#include "../../Libraries/NetworkLib/Packet/Framing/PacketTypes.h"
#include "../../Libraries/NetworkLib/Packet/Framing/IPacketFramer.h"
#include "../../Libraries/NetworkLib/Servers/Core/BackendTypes.h"

// EchoServer contracts
#include "Contents/ContentTypes.h"
#include "Contents/Room/RoomFlowTypes.h"
