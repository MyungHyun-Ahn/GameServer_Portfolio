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
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <format>
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

// Shared library contracts
#include "../../Libraries/Foundation/Logging/LoggingTypes.h"
#include "../../Libraries/Foundation/Logging/ILogger.h"
#include "../../Libraries/ContentsRuntime/Core/ContentRuntimeTypes.h"
#include "../../Libraries/ContentsRuntime/Core/IContent.h"
#include "../../Libraries/ContentsRuntime/Core/ContentExecutionState.h"
#include "../../Libraries/ContentsRuntime/Threading/FContentThread.h"
#include "../../Libraries/NetworkLib/Containers/LockFreeCommon.h"
#include "../../Libraries/NetworkLib/Memory/FLockFreeMemoryPool.h"
#include "../../Libraries/NetworkLib/Memory/FTlsMemoryPool.h"
#include "../../Libraries/NetworkLib/Packet/Buffer/FPacketBuffer.h"
#include "../../Libraries/NetworkLib/Packet/Buffer/FRecvBuffer.h"
#include "../../Libraries/NetworkLib/Packet/Serialization/FPacketReader.h"
#include "../../Libraries/NetworkLib/Packet/Serialization/FPacketWriter.h"
#include "../../Libraries/NetworkLib/Packet/View/FPacketView.h"
#include "../../Libraries/NetworkLib/Packet/View/FBorrowedViewGuard.h"
#include "../../Libraries/NetworkLib/Packet/Framing/ContentHeader.h"
#include "../../Libraries/NetworkLib/Packet/Framing/PacketTypes.h"
#include "../../Libraries/NetworkLib/Packet/Framing/IPacketFramer.h"
#include "../../Libraries/NetworkLib/Crypto/PacketCipherTypes.h"
#include "../../Libraries/NetworkLib/Crypto/IPacketCipher.h"
#include "../../Libraries/NetworkLib/Packet/Serialization/IContentPacket.h"
#include "../../Libraries/NetworkLib/Servers/IServer.h"
#include "../../Libraries/NetworkLib/Packet/Serialization/FPacketSerialization.h"
#include "../../Libraries/ContentsRuntime/Bridge/IContentBridge.h"
#include "../../Libraries/Connector/MySql/MySqlTypes.h"
#include "../../Libraries/Connector/MySql/FMySqlConnection.h"
#include "../../Libraries/Connector/MySql/FThreadAffinedMySqlCluster.h"
#include "../../Libraries/Connector/MySql/MySqlResultReader.h"

// GameData runtime and generated contracts
#include "../../Libraries/GameData/Common/TGameDataRow.h"
#include "../../Generated/GameData/Cpp/Common/GameDataEnums.g.h"
#include "../../Generated/GameData/Cpp/Character/CharacterData.g.h"
#include "../../Generated/GameData/Cpp/CharacterLevel/CharacterLevelData.g.h"
#include "../../Generated/GameData/Cpp/Item/ItemData.g.h"
#include "../../Generated/GameData/Cpp/InventoryPolicy/InventoryPolicyData.g.h"
#include "../../Generated/GameData/Cpp/Currency/CurrencyData.g.h"
#include "../../Generated/GameData/Cpp/MailPolicy/MailPolicyData.g.h"
#include "../../Generated/GameData/Cpp/MailTemplate/MailTemplateData.g.h"
#include "../../Libraries/GameData/Item/FItemDataTable.h"
#include "../../Libraries/GameData/Character/FCharacterDataTable.h"
#include "../../Libraries/GameData/CharacterLevel/FCharacterLevelDataTable.h"
#include "../../Libraries/GameData/InventoryPolicy/FInventoryPolicyTable.h"
#include "../../Libraries/GameData/Currency/FCurrencyDataTable.h"
#include "../../Libraries/GameData/MailPolicy/FMailPolicyTable.h"
#include "../../Libraries/GameData/MailTemplate/FMailTemplateTable.h"

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
#include "../../Libraries/RpcLib/Transport/FServerRpcTransport.h"
#include "../../Libraries/RpcLib/Dispatch/FRpcCallContext.h"
#include "../../Libraries/RpcLib/Dispatch/TRpcReply.h"
#include "../../Libraries/RpcLib/Dispatch/FRpcMethodDispatcher.h"
#include "../../Libraries/RpcLib/FRpcCommon.h"
#include "../../Generated/Rpc/ServerProtocol/UserPresenceRpcMethods.h"

// Cache contracts
#include "../../Generated/Rpc/Cache/CacheRpcMethods.h"
#include "Contents/ContentTypes.h"
#include "Database/CacheDatabaseTypes.h"
#include "Domain/FCacheUser.h"
