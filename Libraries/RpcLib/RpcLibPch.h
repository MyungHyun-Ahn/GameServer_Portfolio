#pragma once

// C++ standard library
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

// Network serialization contracts
#include <Windows.h>
#include "../NetworkLib/Crypto/PacketCipherTypes.h"
#include "../NetworkLib/Containers/LockFreeCommon.h"
#include "../NetworkLib/Memory/FLockFreeMemoryPool.h"
#include "../NetworkLib/Memory/FTlsMemoryPool.h"
#include "../NetworkLib/Packet/Buffer/FPacketBuffer.h"
#include "../NetworkLib/Packet/Serialization/FPacketReader.h"
#include "../NetworkLib/Packet/Serialization/FPacketWriter.h"
#include "../NetworkLib/Packet/View/FPacketView.h"
#include "../NetworkLib/Packet/View/FBorrowedViewGuard.h"
#include "../NetworkLib/Packet/Framing/ContentHeader.h"
#include "../NetworkLib/Packet/Serialization/IContentPacket.h"
#include "../NetworkLib/Servers/IServer.h"
#include "../NetworkLib/Packet/Serialization/FPacketSerialization.h"

// ClientNetworkLib contracts for reusable outbound RPC connections
#include "../ClientNetworkLib/FClientNetworkTypes.h"
#include "../ClientNetworkLib/FClientNetwork.h"

// RpcLib contracts
#include "Protocol/RpcTypes.h"
#include "Protocol/RpcMessages.h"
#include "Protocol/RpcArgumentSerialization.h"
#include "Protocol/RpcMessageSerialization.h"
#include "Protocol/RpcWirePacket.h"
#include "Session/FRpcSession.h"
#include "Session/FRpcSessionRegistry.h"
#include "Call/FRpcRequestIdGenerator.h"
#include "Call/FRpcPendingCallManager.h"
#include "Transport/IRpcTransport.h"
#include "Transport/FServerRpcTransport.h"
#include "Transport/FClientRpcTransport.h"
#include "Client/FOutboundRpcClient.h"
#include "Routing/IRpcContentRouter.h"
#include "Dispatch/FRpcCallContext.h"
#include "Dispatch/TRpcReply.h"
#include "Dispatch/FRpcMethodDispatcher.h"
#include "FRpcCommon.h"
