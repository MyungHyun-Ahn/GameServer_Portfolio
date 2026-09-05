#pragma once

// Network platform
#include <WinSock2.h>
#include <Windows.h>
#include <WS2tcpip.h>

// C++ standard library
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <shared_mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// Network and client contracts
#include "../../Libraries/NetworkLib/Containers/LockFreeCommon.h"
#include "../../Libraries/NetworkLib/Memory/FLockFreeMemoryPool.h"
#include "../../Libraries/NetworkLib/Memory/FTlsMemoryPool.h"
#include "../../Libraries/NetworkLib/Packet/Buffer/FPacketBuffer.h"
#include "../../Libraries/NetworkLib/Packet/Serialization/FPacketReader.h"
#include "../../Libraries/NetworkLib/Packet/Serialization/FPacketWriter.h"
#include "../../Libraries/NetworkLib/Packet/View/FPacketView.h"
#include "../../Libraries/NetworkLib/Packet/View/FBorrowedViewGuard.h"
#include "../../Libraries/NetworkLib/Packet/Framing/ContentHeader.h"
#include "../../Libraries/NetworkLib/Packet/Serialization/IContentPacket.h"
#include "../../Libraries/NetworkLib/Servers/IServer.h"
#include "../../Libraries/NetworkLib/Packet/Serialization/FPacketSerialization.h"
#include "../../Libraries/NetworkLib/Crypto/PacketCipherTypes.h"
#include "../../Libraries/ClientNetworkLib/FClientNetworkTypes.h"
#include "../../Libraries/ClientNetworkLib/FClientNetwork.h"

// RPC contracts
#include "../../Libraries/RpcLib/Protocol/RpcTypes.h"
#include "../../Libraries/RpcLib/Protocol/RpcMessages.h"
#include "../../Libraries/RpcLib/Protocol/RpcArgumentSerialization.h"
#include "../../Libraries/RpcLib/Protocol/RpcMessageSerialization.h"
#include "../../Libraries/RpcLib/Protocol/RpcWirePacket.h"
#include "../../Libraries/RpcLib/Session/FRpcSession.h"
#include "../../Libraries/RpcLib/Session/FRpcSessionRegistry.h"
#include "../../Libraries/RpcLib/Call/FRpcRequestIdGenerator.h"
#include "../../Libraries/RpcLib/Call/FRpcPendingCallManager.h"
#include "../../Libraries/RpcLib/Transport/IRpcTransport.h"
#include "../../Libraries/RpcLib/Dispatch/FRpcCallContext.h"
#include "../../Libraries/RpcLib/Dispatch/TRpcReply.h"
#include "../../Libraries/RpcLib/Dispatch/FRpcMethodDispatcher.h"
#include "../../Libraries/RpcLib/FRpcCommon.h"

// Cache contracts
#include "../../Generated/Rpc/Cache/CacheRpcMethods.h"

// Shared server contracts
#include "../../Generated/Rpc/ServerProtocol/UserPresenceRpcMethods.h"
