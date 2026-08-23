#pragma once

// Platform
#include <WinSock2.h>
#include <Windows.h>

// C++ standard library
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

// NetworkLib contracts under test
#include "Containers/LockFreeCommon.h"
#include "Memory/FLockFreeMemoryPool.h"
#include "Memory/FTlsMemoryPool.h"
#include "Crypto/PacketCipherTypes.h"
#include "Crypto/IPacketCipher.h"
#include "Packet/Framing/ContentHeader.h"
#include "Packet/Buffer/FPacketBuffer.h"
#include "Packet/Serialization/FPacketWriter.h"
#include "Packet/Serialization/FPacketReader.h"
#include "Packet/View/FPacketView.h"
#include "Packet/View/FBorrowedViewGuard.h"
#include "Servers/IServer.h"
#include "Packet/Serialization/FPacketSerialization.h"
#include "Packet/Serialization/IContentPacket.h"
#include "Packet/Buffer/FRecvBuffer.h"
#include "Packet/Framing/PacketTypes.h"
#include "Packet/Framing/IPacketFramer.h"
