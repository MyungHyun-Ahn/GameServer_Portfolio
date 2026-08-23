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
#include <cmath>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cctype>
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
#include <random>
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

// Client and packet contracts
#include <barrier>
#include <cstdlib>
#include "../../Libraries/NetworkLib/Crypto/PacketCipherTypes.h"
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
#include "../../Libraries/ClientNetworkLib/FClientNetworkTypes.h"
#include "../../Libraries/NetworkLib/Packet/View/FBorrowedViewGuard.h"
#include "../../Libraries/NetworkLib/Packet/Serialization/IContentPacket.h"
#include "../../Libraries/ClientNetworkLib/FClientNetwork.h"

// Stable load-test types
#include "LoadTest/AuctionLoadTestTypes.h"
#include "LoadTest/FVirtualAuctionUser.h"
#include "LoadTest/FLoadTestMetrics.h"
