#pragma once

// Network platform
#include <WinSock2.h>
#include <Windows.h>

// C++ standard library
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <format>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// Shared runtime contracts
#include "Foundation/Logging/LoggingTypes.h"
#include "Foundation/Logging/ILogger.h"
#include "NetworkLib/Containers/LockFreeCommon.h"
#include "NetworkLib/Memory/FLockFreeMemoryPool.h"
#include "NetworkLib/Memory/FTlsMemoryPool.h"
#include "NetworkLib/Containers/FLockFreeQueue.h"
#include "NetworkLib/Packet/Framing/ContentHeader.h"
#include "NetworkLib/Packet/Buffer/FPacketBuffer.h"
#include "NetworkLib/Packet/Serialization/FPacketWriter.h"
#include "NetworkLib/Packet/Serialization/FPacketReader.h"
#include "NetworkLib/Packet/View/FPacketView.h"
#include "NetworkLib/Servers/Core/BackendTypes.h"
#include "NetworkLib/Servers/IServer.h"
#include "NetworkLib/Packet/Serialization/FPacketSerialization.h"
#include "NetworkLib/Packet/View/FBorrowedViewGuard.h"
#include "NetworkLib/Packet/Serialization/IContentPacket.h"
#include "NetworkLib/Servers/Core/FStubServer.h"
#include "ContentsRuntime/Core/ContentRuntimeTypes.h"
#include "ContentsRuntime/Bridge/IContentBridge.h"
#include "ContentsRuntime/Core/IContent.h"
#include "ContentsRuntime/Routing/FContentRuntime.h"
#include "ContentsRuntime/Threading/FContentThread.h"

// World contracts
#include "WorldCore/WorldTypes.h"
#include "WorldCore/Map/Sector/ISectorExecutor.h"
#include "WorldCore/Entity/FActorEntity.h"
#include "WorldCore/Entity/FEntityRegistry.h"
#include "WorldCore/Entity/FMonsterEntity.h"
#include "WorldCore/Entity/FPlayerEntity.h"
#include "WorldCore/Map/FMapInstance.h"
#include "WorldCore/Map/FMapInstanceFactory.h"
#include "WorldCore/Map/Sector/FSectorGrid.h"
