#include "LockFreeTestsPch.h"

#include "Containers/FLockFreeQueue.h"
#include "Containers/FLockFreeStack.h"
#include "Crypto/FDefaultPacketCipher.h"
#include "Crypto/FNullPacketCipher.h"
#include "Generated/Packets/Cpp/Chat/ChatPackets.h"
#include "Generated/Packets/Cpp/Echo/EchoPackets.h"
#include "Crypto/IPacketCipher.h"
#include "Memory/FTlsMemoryPool.h"
#include "Packet/Buffer/FRecvBuffer.h"
#include "Packet/Framing/FDefaultPacketFramer.h"
#include "Packet/Serialization/FPacketSerialization.h"
#include "Packet/View/FPacketView.h"

#include <atomic>
#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace
{
	struct STestResult
	{
		bool passed = false;
		std::string name;
		std::string detail;
	};

	using TQueue = NetworkLib::Containers::FLockFreeQueue<int>;
	using TStack = NetworkLib::Containers::FLockFreeStack<int>;
	static_assert(std::endian::native == std::endian::little, "Packet wire fixtures require little-endian Windows.");
	static_assert(sizeof(bool) == 1, "Packet bool wire format requires a one-byte bool representation.");
	static_assert(std::numeric_limits<float>::is_iec559, "Packet float wire format requires IEEE 754 binary32.");
	static_assert(std::numeric_limits<double>::is_iec559, "Packet double wire format requires IEEE 754 binary64.");
	static_assert(sizeof(TQueue) == sizeof(std::int64_t) * 2, "FLockFreeQueue instance should only keep head and tail pointers.");
	static_assert(sizeof(TStack) == sizeof(std::int64_t), "FLockFreeStack instance should only keep top pointer.");

	struct STlsPoolPayload
	{
		std::int32_t ownerThread = -1;
		std::int32_t iteration = -1;
		char padding[48]{};
	};

	template <typename TContainer> STestResult RunLinearQueueTest()
	{
		TContainer queue;
		for (int value = 1; value <= 1000; ++value)
		{
			queue.Enqueue(value);
		}

		for (int expected = 1; expected <= 1000; ++expected)
		{
			int dequeuedValue = 0;
			if (!queue.Dequeue(dequeuedValue))
			{
				return {false, "Queue linear FIFO", "dequeue failed before queue was empty"};
			}

			if (dequeuedValue != expected)
			{
				return {false, "Queue linear FIFO", "FIFO order mismatch"};
			}
		}

		int tailValue = 0;
		if (queue.Dequeue(tailValue))
		{
			return {false, "Queue linear FIFO", "queue should be empty after 1000 dequeues"};
		}

		return {true, "Queue linear FIFO", "passed"};
	}

	template <typename TContainer>
	STestResult RunParallelSumTest(
		const char* testName)
	{
		constexpr int kProducerCount = 4;
		constexpr int kConsumerCount = 4;
		constexpr int kItemsPerProducer = 50000;
		const std::int64_t expectedCount = static_cast<std::int64_t>(kProducerCount) * kItemsPerProducer;

		TContainer container;
		std::atomic<std::int64_t> producedSum = 0;
		std::atomic<std::int64_t> consumedSum = 0;
		std::atomic<std::int64_t> consumedCount = 0;
		std::atomic<int> activeProducers = kProducerCount;

		std::vector<std::thread> producerThreads;
		producerThreads.reserve(kProducerCount);
		for (int producerIndex = 0; producerIndex < kProducerCount; ++producerIndex)
		{
			producerThreads.emplace_back(
				[&, producerIndex]()
				{
					const int baseValue = producerIndex * kItemsPerProducer;
					for (int offset = 1; offset <= kItemsPerProducer; ++offset)
					{
						const int value = baseValue + offset;
						container.Push(value);
						producedSum.fetch_add(value, std::memory_order_relaxed);
					}

					activeProducers.fetch_sub(1, std::memory_order_release);
				});
		}

		std::vector<std::thread> consumerThreads;
		consumerThreads.reserve(kConsumerCount);
		for (int consumerIndex = 0; consumerIndex < kConsumerCount; ++consumerIndex)
		{
			consumerThreads.emplace_back(
				[&]()
				{
					while (true)
					{
						int value = 0;
						if (container.Pop(value))
						{
							consumedSum.fetch_add(value, std::memory_order_relaxed);
							const std::int64_t newCount = consumedCount.fetch_add(1, std::memory_order_relaxed) + 1;
							if (newCount >= expectedCount)
							{
								return;
							}

							continue;
						}

						if (activeProducers.load(std::memory_order_acquire) == 0 &&
							consumedCount.load(std::memory_order_relaxed) >= expectedCount)
						{
							return;
						}

						std::this_thread::yield();
					}
				});
		}

		for (auto& producerThread : producerThreads)
		{
			producerThread.join();
		}

		for (auto& consumerThread : consumerThreads)
		{
			consumerThread.join();
		}

		if (consumedCount.load() != expectedCount)
		{
			return {false, testName, "consumed item count mismatch"};
		}

		if (consumedSum.load() != producedSum.load())
		{
			return {false, testName, "sum mismatch after parallel run"};
		}

		return {true, testName, "passed"};
	}

	STestResult RunTlsMemoryPoolParallelTest()
	{
		constexpr int kThreadCount = 8;
		constexpr int kBatchSize = 64;
		constexpr int kIterationCount = 10000;

		NetworkLib::Memory::FTlsMemoryPoolManager<STlsPoolPayload, kBatchSize, 2> memoryPool;
		std::atomic<bool> encounteredError = false;
		std::vector<std::thread> workerThreads;
		workerThreads.reserve(kThreadCount);

		for (int threadIndex = 0; threadIndex < kThreadCount; ++threadIndex)
		{
			workerThreads.emplace_back(
				[&, threadIndex]()
				{
					std::vector<STlsPoolPayload*> batch;
					batch.reserve(kBatchSize);

					for (int iteration = 0; iteration < kIterationCount; ++iteration)
					{
						for (int batchIndex = 0; batchIndex < kBatchSize; ++batchIndex)
						{
							STlsPoolPayload* payload = memoryPool.Alloc();
							if (payload == nullptr)
							{
								encounteredError.store(true, std::memory_order_relaxed);
								return;
							}

							payload->ownerThread = threadIndex;
							payload->iteration = iteration;
							batch.push_back(payload);
						}

						for (STlsPoolPayload* payload : batch)
						{
							if (payload->ownerThread != threadIndex || payload->iteration != iteration)
							{
								encounteredError.store(true, std::memory_order_relaxed);
								return;
							}

							memoryPool.Free(payload);
						}

						batch.clear();
					}
				});
		}

		for (auto& workerThread : workerThreads)
		{
			workerThread.join();
		}

		if (encounteredError.load(std::memory_order_relaxed))
		{
			return {false, "TLS memory pool parallel", "allocation or payload validation failed"};
		}

		if (memoryPool.GetUseCount() != 0)
		{
			return {false, "TLS memory pool parallel", "pool usage should return to zero after frees"};
		}

		STlsPoolPayload* finalPayload = memoryPool.Alloc();
		if (finalPayload == nullptr)
		{
			return {false, "TLS memory pool parallel", "post-run allocation failed"};
		}

		memoryPool.Free(finalPayload);
		return {true, "TLS memory pool parallel", "passed"};
	}

	STestResult RunPacketCipherRoundTripTest()
	{
		using namespace NetworkLib::Crypto;

		SDefaultPacketCipherConfig config{};
		config.enabled = true;
		config.packetKey = 0x37;

		std::shared_ptr<IPacketCipher> cipher = std::make_shared<FDefaultPacketCipher>(config);
		std::string originalMessage = "packet-cipher-roundtrip";
		std::string encodedMessage = originalMessage;
		const std::uint8_t randomKey = 0x5A;

		if (!cipher->GetConfig().enabled)
		{
			return {false, "Packet cipher round trip", "cipher config should be visible through interface"};
		}

		const std::uint8_t originalChecksum = cipher->CalculateChecksum(originalMessage.data(), static_cast<int>(originalMessage.size()));
		cipher->Encode(encodedMessage.data(), static_cast<int>(encodedMessage.size()), randomKey);
		if (encodedMessage == originalMessage)
		{
			return {false, "Packet cipher round trip", "encoded message should differ from original plaintext"};
		}

		cipher->Decode(encodedMessage.data(), static_cast<int>(encodedMessage.size()), randomKey);
		if (encodedMessage != originalMessage)
		{
			return {false, "Packet cipher round trip", "decoded message does not match original plaintext"};
		}

		const std::uint8_t decodedChecksum = cipher->CalculateChecksum(encodedMessage.data(), static_cast<int>(encodedMessage.size()));
		if (decodedChecksum != originalChecksum)
		{
			return {false, "Packet cipher round trip", "checksum mismatch after decode"};
		}

		return {true, "Packet cipher round trip", "passed"};
	}

	STestResult RunNullPacketCipherTest()
	{
		using namespace NetworkLib::Crypto;

		SPacketCipherConfig config{};
		config.enabled = false;

		std::shared_ptr<IPacketCipher> cipher = std::make_shared<FNullPacketCipher>(config);
		std::string originalMessage = "packet-cipher-null";
		std::string transformedMessage = originalMessage;

		const std::uint8_t originalChecksum = cipher->CalculateChecksum(originalMessage.data(), static_cast<int>(originalMessage.size()));
		cipher->Encode(transformedMessage.data(), static_cast<int>(transformedMessage.size()), 0x11);
		if (transformedMessage != originalMessage)
		{
			return {false, "Null packet cipher", "encode should not modify payload"};
		}

		cipher->Decode(transformedMessage.data(), static_cast<int>(transformedMessage.size()), 0x22);
		if (transformedMessage != originalMessage)
		{
			return {false, "Null packet cipher", "decode should not modify payload"};
		}

		const std::uint8_t transformedChecksum =
			cipher->CalculateChecksum(transformedMessage.data(), static_cast<int>(transformedMessage.size()));
		if (transformedChecksum != originalChecksum)
		{
			return {false, "Null packet cipher", "checksum should remain stable for no-op cipher"};
		}

		return {true, "Null packet cipher", "passed"};
	}

	STestResult RunPacketFramerRoundTripTest()
	{
		using namespace NetworkLib::Packet::Buffer;
		using namespace NetworkLib::Packet::Framing;
		using namespace NetworkLib::Packet::Serialization;
		using namespace NetworkLib::Packet::View;

		FDefaultPacketFramer framer;
		std::vector<char> packetBuffer;
		const std::string payload = "framed-echo-payload";
		std::vector<char> contentPayload = BuildContentPayload(3001, std::vector<char>(payload.begin(), payload.end()));
		SOutgoingPacket outgoingPacket{};
		outgoingPacket.randomKey = 0x44;
		outgoingPacket.checkSum = CalculatePacketChecksum(contentPayload.data(), static_cast<std::int32_t>(contentPayload.size()));
		outgoingPacket.payload = contentPayload.data();
		outgoingPacket.payloadLength = static_cast<std::int32_t>(contentPayload.size());

		if (!framer.BuildPacket(outgoingPacket, packetBuffer))
		{
			return {false, "Packet framer round trip", "BuildPacket failed"};
		}

		if (packetBuffer.size() != contentPayload.size() + sizeof(SPacketHeader))
		{
			return {false, "Packet framer round trip", "packet size mismatch"};
		}

		SFramedPacket framedPacket;
		std::vector<char> receiveBuffer = packetBuffer;
		if (!framer.TryExtractPacket(receiveBuffer, framedPacket))
		{
			return {false, "Packet framer round trip", "TryExtractPacket failed"};
		}

		if (!receiveBuffer.empty())
		{
			return {false, "Packet framer round trip", "receive buffer should be empty after extraction"};
		}

		if (framedPacket.randomKey != 0x44)
		{
			return {false, "Packet framer round trip", "randomKey mismatch after extraction"};
		}

		if (framedPacket.checkSum != outgoingPacket.checkSum)
		{
			return {false, "Packet framer round trip", "checksum mismatch after extraction"};
		}

		FPacketView transportPacketView{};
		transportPacketView.randomKey = framedPacket.randomKey;
		transportPacketView.checkSum = framedPacket.checkSum;
		transportPacketView.payload = framedPacket.payload.data();
		transportPacketView.payloadLength = static_cast<std::int32_t>(framedPacket.payload.size());

		FPacketView contentPacketView{};
		if (!TryParseContentPacketView(transportPacketView, contentPacketView))
		{
			return {false, "Packet framer round trip", "content packet view parse failed"};
		}

		if (contentPacketView.opcode != 3001)
		{
			return {false, "Packet framer round trip", "opcode mismatch after extraction"};
		}

		if (std::string(contentPacketView.payload, contentPacketView.payload + contentPacketView.payloadLength) != payload)
		{
			return {false, "Packet framer round trip", "payload mismatch after extraction"};
		}

		return {true, "Packet framer round trip", "passed"};
	}

	STestResult RunPacketFramerSizeLimitTest()
	{
		using namespace NetworkLib::Packet::Framing;

		FDefaultPacketFramer framer;
		SFramedPacketBufferParts packetParts{};
		std::vector<char> maximumPayload(kMaxTransportPayloadSizeBytes, 'x');

		SOutgoingPacket outgoingPacket{};
		outgoingPacket.payload = maximumPayload.data();
		outgoingPacket.payloadLength = static_cast<std::int32_t>(maximumPayload.size());
		if (!framer.BuildPacketParts(outgoingPacket, packetParts))
		{
			return {false, "Packet framer size limit", "maximum 8KB frame should be accepted"};
		}

		maximumPayload.push_back('x');
		outgoingPacket.payload = maximumPayload.data();
		outgoingPacket.payloadLength = static_cast<std::int32_t>(maximumPayload.size());
		if (framer.BuildPacketParts(outgoingPacket, packetParts))
		{
			return {false, "Packet framer size limit", "frame larger than 8KB should be rejected"};
		}

		SPacketHeader invalidHeader{};
		invalidHeader.payloadLength = static_cast<std::uint16_t>(kMaxTransportPayloadSizeBytes + 1);
		std::vector<char> invalidReceiveBuffer(sizeof(SPacketHeader));
		std::memcpy(invalidReceiveBuffer.data(), &invalidHeader, sizeof(invalidHeader));
		if (!framer.HasInvalidPacketHeader(invalidReceiveBuffer))
		{
			return {false, "Packet framer size limit", "oversized receive header should be marked invalid"};
		}

		SFramedPacket framedPacket{};
		if (framer.TryExtractPacket(invalidReceiveBuffer, framedPacket))
		{
			return {false, "Packet framer size limit", "oversized receive frame should not be extracted"};
		}

		return {true, "Packet framer size limit", "passed"};
	}

	STestResult RunPacketFramerPartialReceiveTest()
	{
		using namespace NetworkLib::Packet::Buffer;
		using namespace NetworkLib::Packet::Framing;
		using namespace NetworkLib::Packet::Serialization;
		using namespace NetworkLib::Packet::View;

		FDefaultPacketFramer framer;
		std::vector<char> packetBuffer;
		const std::string payload = "partial-frame";
		std::vector<char> contentPayload = BuildContentPayload(3002, std::vector<char>(payload.begin(), payload.end()));
		SOutgoingPacket outgoingPacket{};
		outgoingPacket.randomKey = 0x21;
		outgoingPacket.checkSum = CalculatePacketChecksum(contentPayload.data(), static_cast<std::int32_t>(contentPayload.size()));
		outgoingPacket.payload = contentPayload.data();
		outgoingPacket.payloadLength = static_cast<std::int32_t>(contentPayload.size());

		if (!framer.BuildPacket(outgoingPacket, packetBuffer))
		{
			return {false, "Packet framer partial receive", "BuildPacket failed"};
		}

		std::vector<char> receiveBuffer(packetBuffer.begin(), packetBuffer.begin() + 2);
		SFramedPacket framedPacket;
		if (framer.TryExtractPacket(receiveBuffer, framedPacket))
		{
			return {false, "Packet framer partial receive", "packet should not be extracted before header is complete"};
		}

		receiveBuffer.insert(receiveBuffer.end(), packetBuffer.begin() + 2, packetBuffer.end() - 3);
		if (framer.TryExtractPacket(receiveBuffer, framedPacket))
		{
			return {false, "Packet framer partial receive", "packet should not be extracted before payload is complete"};
		}

		receiveBuffer.insert(receiveBuffer.end(), packetBuffer.end() - 3, packetBuffer.end());
		if (!framer.TryExtractPacket(receiveBuffer, framedPacket))
		{
			return {false, "Packet framer partial receive", "packet should be extracted after remaining bytes arrive"};
		}

		if (framedPacket.checkSum != outgoingPacket.checkSum)
		{
			return {false, "Packet framer partial receive", "checksum mismatch after partial receive assembly"};
		}

		FPacketView transportPacketView{};
		transportPacketView.randomKey = framedPacket.randomKey;
		transportPacketView.checkSum = framedPacket.checkSum;
		transportPacketView.payload = framedPacket.payload.data();
		transportPacketView.payloadLength = static_cast<std::int32_t>(framedPacket.payload.size());

		FPacketView contentPacketView{};
		if (!TryParseContentPacketView(transportPacketView, contentPacketView))
		{
			return {false, "Packet framer partial receive", "content packet view parse failed"};
		}

		if (contentPacketView.opcode != 3002)
		{
			return {false, "Packet framer partial receive", "opcode mismatch after partial receive assembly"};
		}

		if (std::string(contentPacketView.payload, contentPacketView.payload + contentPacketView.payloadLength) != payload)
		{
			return {false, "Packet framer partial receive", "payload mismatch after partial receive assembly"};
		}

		return {true, "Packet framer partial receive", "passed"};
	}

	STestResult RunPacketFramerRecvBufferTest()
	{
		using namespace NetworkLib::Packet::Buffer;
		using namespace NetworkLib::Packet::Framing;
		using namespace NetworkLib::Packet::Serialization;
		using namespace NetworkLib::Packet::View;

		FDefaultPacketFramer framer;
		FRecvBuffer recvBuffer(64);
		std::vector<char> packetBuffer;
		const std::string payload = "recv-ring-buffer";
		std::vector<char> contentPayload = BuildContentPayload(3003, std::vector<char>(payload.begin(), payload.end()));

		SOutgoingPacket outgoingPacket{};
		outgoingPacket.randomKey = 0x31;
		outgoingPacket.checkSum = CalculatePacketChecksum(contentPayload.data(), static_cast<std::int32_t>(contentPayload.size()));
		outgoingPacket.payload = contentPayload.data();
		outgoingPacket.payloadLength = static_cast<std::int32_t>(contentPayload.size());

		if (!framer.BuildPacket(outgoingPacket, packetBuffer))
		{
			return {false, "Packet framer recv buffer", "BuildPacket failed"};
		}

		WSABUF recvWsabufs[2]{};
		DWORD recvBufferCount = 0;
		recvBuffer.BuildRecvWsabufs(recvWsabufs, recvBufferCount);
		if (recvBufferCount != 1)
		{
			return {false, "Packet framer recv buffer", "unexpected initial recv buffer count"};
		}

		std::memcpy(recvWsabufs[0].buf, packetBuffer.data(), 5);
		if (!recvBuffer.CommitWrite(5))
		{
			return {false, "Packet framer recv buffer", "CommitWrite failed for first fragment"};
		}

		SFramedPacket framedPacket;
		if (framer.TryExtractPacket(recvBuffer, framedPacket))
		{
			return {false, "Packet framer recv buffer", "packet should not be extracted before full payload arrives"};
		}

		recvBuffer.BuildRecvWsabufs(recvWsabufs, recvBufferCount);
		std::memcpy(recvWsabufs[0].buf, packetBuffer.data() + 5, packetBuffer.size() - 5);
		if (!recvBuffer.CommitWrite(packetBuffer.size() - 5))
		{
			return {false, "Packet framer recv buffer", "CommitWrite failed for second fragment"};
		}

		if (!framer.TryExtractPacket(recvBuffer, framedPacket))
		{
			return {false, "Packet framer recv buffer", "packet should be extracted from recv buffer"};
		}

		FPacketView transportPacketView{};
		transportPacketView.randomKey = framedPacket.randomKey;
		transportPacketView.checkSum = framedPacket.checkSum;
		transportPacketView.payload = framedPacket.payload.data();
		transportPacketView.payloadLength = static_cast<std::int32_t>(framedPacket.payload.size());

		FPacketView contentPacketView{};
		if (!TryParseContentPacketView(transportPacketView, contentPacketView))
		{
			return {false, "Packet framer recv buffer", "content packet view parse failed"};
		}

		if (contentPacketView.opcode != 3003)
		{
			return {false, "Packet framer recv buffer", "opcode mismatch after recv buffer extraction"};
		}

		if (std::string(contentPacketView.payload, contentPacketView.payload + contentPacketView.payloadLength) != payload)
		{
			return {false, "Packet framer recv buffer", "payload mismatch after recv buffer extraction"};
		}

		if (recvBuffer.GetUsedSize() != 0)
		{
			return {false, "Packet framer recv buffer", "recv buffer should be empty after extraction"};
		}

		return {true, "Packet framer recv buffer", "passed"};
	}

	STestResult RunPacketFramerPacketViewTest()
	{
		using namespace NetworkLib::Packet::Buffer;
		using namespace NetworkLib::Packet::Framing;
		using namespace NetworkLib::Packet::Serialization;
		using namespace NetworkLib::Packet::View;

		FDefaultPacketFramer framer;
		FRecvBuffer recvBuffer(64);
		std::vector<char> packetBuffer;
		const std::string payload = "packet-view";
		std::vector<char> contentPayload = BuildContentPayload(3004, std::vector<char>(payload.begin(), payload.end()));

		SOutgoingPacket outgoingPacket{};
		outgoingPacket.randomKey = 0x55;
		outgoingPacket.checkSum = CalculatePacketChecksum(contentPayload.data(), static_cast<std::int32_t>(contentPayload.size()));
		outgoingPacket.payload = contentPayload.data();
		outgoingPacket.payloadLength = static_cast<std::int32_t>(contentPayload.size());

		if (!framer.BuildPacket(outgoingPacket, packetBuffer))
		{
			return {false, "Packet framer packet view", "BuildPacket failed"};
		}

		WSABUF recvWsabufs[2]{};
		DWORD recvBufferCount = 0;
		recvBuffer.BuildRecvWsabufs(recvWsabufs, recvBufferCount);
		std::memcpy(recvWsabufs[0].buf, packetBuffer.data(), packetBuffer.size());
		if (!recvBuffer.CommitWrite(packetBuffer.size()))
		{
			return {false, "Packet framer packet view", "CommitWrite failed"};
		}

		FPacketView packetView;
		if (!framer.TryExtractPacketView(recvBuffer, packetView))
		{
			return {false, "Packet framer packet view", "TryExtractPacketView failed"};
		}

		if (packetView.opcode != 0)
		{
			return {false, "Packet framer packet view", "transport packet view opcode should not be set"};
		}

		FPacketView contentPacketView{};
		if (!TryParseContentPacketView(packetView, contentPacketView))
		{
			return {false, "Packet framer packet view", "content packet view parse failed"};
		}

		const char* expectedPayloadPtr = recvBuffer.GetReadPointer() + sizeof(SPacketHeader) + sizeof(SContentHeader);
		if (contentPacketView.payload != expectedPayloadPtr)
		{
			return {false, "Packet framer packet view", "payload pointer should point into recv buffer"};
		}

		if (contentPacketView.opcode != 3004)
		{
			return {false, "Packet framer packet view", "opcode mismatch"};
		}

		if (std::string(contentPacketView.payload, contentPacketView.payload + contentPacketView.payloadLength) != payload)
		{
			return {false, "Packet framer packet view", "payload mismatch"};
		}

		if (!recvBuffer.Discard(sizeof(SPacketHeader) + static_cast<std::size_t>(packetView.payloadLength)))
		{
			return {false, "Packet framer packet view", "Discard failed after view dispatch"};
		}

		return {true, "Packet framer packet view", "passed"};
	}

	STestResult RunGeneratedEchoPacketRoundTripTest()
	{
		Generated::Echo::FEchoRq requestPacket;
		requestPacket.SetMessageValue("generated-echo-message");

		std::vector<char> payload = NetworkLib::Packet::Serialization::SerializeContentBody(requestPacket);
		Generated::Echo::FEchoRq decodedPacket;
		if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(payload.data(), payload.size(), decodedPacket))
		{
			return {false, "Generated echo packet round trip", "DeserializeContentPacket failed"};
		}

		if (!decodedPacket.ContainsBorrowedViews())
		{
			return {false, "Generated echo packet round trip", "echo packet should report borrowed view payload"};
		}

		if (decodedPacket.GetOpcode() != requestPacket.GetOpcode())
		{
			return {false, "Generated echo packet round trip", "opcode mismatch"};
		}

		if (decodedPacket.GetMessageValue() != requestPacket.GetMessageValue())
		{
			return {false, "Generated echo packet round trip", "message mismatch"};
		}

		return {true, "Generated echo packet round trip", "passed"};
	}

	STestResult RunGeneratedChatContainerPacketRoundTripTest()
	{
		Generated::Chat::FRoomListRp responsePacket;
		responsePacket.roomIds = {77, 78, 79};
		responsePacket.roomNames = {"Room-1", "Room-2", "Room-3"};
		responsePacket.participantCounts = {1, 2, 0};
		responsePacket.capacities = {2, 2, 2};
		responsePacket.joinableFlags = {1, 0, 1};

		std::vector<char> payload = NetworkLib::Packet::Serialization::SerializeContentBody(responsePacket);
		Generated::Chat::FRoomListRp decodedPacket;
		if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(payload.data(), payload.size(), decodedPacket))
		{
			return {false, "Generated chat container packet round trip", "DeserializeContentPacket failed"};
		}

		if (decodedPacket.roomIds != responsePacket.roomIds)
		{
			return {false, "Generated chat container packet round trip", "roomIds mismatch"};
		}

		if (decodedPacket.roomNames != responsePacket.roomNames)
		{
			return {false, "Generated chat container packet round trip", "roomNames mismatch"};
		}

		if (decodedPacket.participantCounts != responsePacket.participantCounts)
		{
			return {false, "Generated chat container packet round trip", "participantCounts mismatch"};
		}

		if (decodedPacket.capacities != responsePacket.capacities)
		{
			return {false, "Generated chat container packet round trip", "capacities mismatch"};
		}

		if (decodedPacket.joinableFlags != responsePacket.joinableFlags)
		{
			return {false, "Generated chat container packet round trip", "joinableFlags mismatch"};
		}

		return {true, "Generated chat container packet round trip", "passed"};
	}

	STestResult RunPacketScalarContainerBulkRoundTripTest()
	{
		using namespace NetworkLib::Packet::Buffer;
		using namespace NetworkLib::Packet::Serialization;

		FPacketWriter writer;
		std::vector<std::uint32_t> originalVector = {10, 20, 30, 40, 50, 60, 70, 80};
		std::array<std::uint16_t, 4> originalArray = {3, 6, 9, 12};

		writer.Write(originalVector);
		writer.Write(originalArray);

		FPacketReader reader(writer.GetBuffer().data(), writer.GetBuffer().size());
		std::vector<std::uint32_t> decodedVector;
		std::array<std::uint16_t, 4> decodedArray{};
		if (!reader.Read(decodedVector) || !reader.Read(decodedArray) || !reader.IsAtEnd())
		{
			return {false, "Packet scalar container bulk round trip", "read failed"};
		}

		if (decodedVector != originalVector)
		{
			return {false, "Packet scalar container bulk round trip", "vector mismatch"};
		}

		if (decodedArray != originalArray)
		{
			return {false, "Packet scalar container bulk round trip", "array mismatch"};
		}

		return {true, "Packet scalar container bulk round trip", "passed"};
	}

	STestResult RunGeneratedPacketEstimatedSizeTest()
	{
		using namespace Generated;
		using namespace NetworkLib::Packet::Buffer;
		using namespace NetworkLib::Packet::Serialization;

		Echo::FEchoRq echoPacket;
		echoPacket.SetMessageValue("estimate-check");
		const std::vector<char> echoPayload = SerializeContentBody(echoPacket);
		if (echoPacket.GetEstimatedBodySize() != echoPayload.size())
		{
			return {false, "Generated packet estimated size", "echo packet estimated size mismatch"};
		}

		Chat::FRoomListRp chatPacket;
		chatPacket.roomIds = {77, 78};
		chatPacket.roomNames = {"Room-1", "Room-2"};
		chatPacket.participantCounts = {1, 2};
		chatPacket.capacities = {2, 2};
		chatPacket.joinableFlags = {1, 0};

		const std::vector<char> chatPayload = SerializeContentBody(chatPacket);
		if (chatPacket.GetEstimatedBodySize() != chatPayload.size())
		{
			return {false, "Generated packet estimated size", "chat packet estimated size mismatch"};
		}

		return {true, "Generated packet estimated size", "passed"};
	}

	STestResult RunPacketBytesViewRoundTripTest()
	{
		using namespace NetworkLib::Packet::Serialization;

		const std::array<std::uint8_t, 6> originalBytes = {1, 3, 5, 7, 9, 11};
		FPacketWriter writer;
		writer.Write(std::span<const std::uint8_t>(originalBytes.data(), originalBytes.size()));

		FPacketReader reader(writer.GetBuffer().data(), writer.GetBuffer().size());
		std::span<const std::uint8_t> decodedBytes;
		if (!reader.Read(decodedBytes) || !reader.IsAtEnd())
		{
			return {false, "Packet bytes_view round trip", "read failed"};
		}

		if (decodedBytes.size() != originalBytes.size())
		{
			return {false, "Packet bytes_view round trip", "size mismatch"};
		}

		for (std::size_t index = 0; index < originalBytes.size(); ++index)
		{
			if (decodedBytes[index] != originalBytes[index])
			{
				return {false, "Packet bytes_view round trip", "payload mismatch"};
			}
		}

		return {true, "Packet bytes_view round trip", "passed"};
	}

	STestResult RunGeneratedRoomFlowPacketRoundTripTest()
	{
		using namespace Generated::Chat;
		using namespace NetworkLib::Packet::Serialization;

		FRoomChangeRp packet;
		packet.previousRoomId = 77;
		packet.currentRoomId = 78;
		packet.success = true;
		packet.resultCode = 0;

		std::vector<char> payload = SerializeContentBody(packet);
		FRoomChangeRp decodedPacket;
		if (!DeserializeContentPacket(payload.data(), payload.size(), decodedPacket))
		{
			return {false, "Generated room flow packet round trip", "DeserializeContentPacket failed"};
		}

		if (decodedPacket.previousRoomId != packet.previousRoomId || decodedPacket.currentRoomId != packet.currentRoomId ||
			decodedPacket.success != packet.success || decodedPacket.resultCode != packet.resultCode)
		{
			return {false, "Generated room flow packet round trip", "packet mismatch"};
		}

		return {true, "Generated room flow packet round trip", "passed"};
	}

	STestResult RunCanonicalPacketWireFormatTest()
	{
		using namespace NetworkLib::Packet::Serialization;

		FPacketWriter writer;
		writer.Write(true);
		writer.Write(static_cast<std::int8_t>(-2));
		writer.Write(static_cast<std::uint8_t>(0xAB));
		writer.Write(static_cast<std::int16_t>(-2));
		writer.Write(static_cast<std::uint16_t>(0x1234));
		writer.Write(static_cast<std::int32_t>(-2));
		writer.Write(static_cast<std::uint32_t>(0x12345678));
		writer.Write(static_cast<std::int64_t>(-2));
		writer.Write(static_cast<std::uint64_t>(0x0102030405060708));
		writer.Write(1.0F);
		writer.Write(-2.0);
		writer.Write(std::string("가"));

		const std::array<std::uint8_t, 3> rawBytes = {0x00, 0x7F, 0xFF};
		writer.Write(std::span<const std::uint8_t>(rawBytes));
		writer.Write(std::vector<std::uint16_t>{0x0001, 0x0203});

		constexpr std::array<std::uint8_t, 65> expected = {0x01,
			0xFE,
			0xAB,
			0xFE,
			0xFF,
			0x34,
			0x12,
			0xFE,
			0xFF,
			0xFF,
			0xFF,
			0x78,
			0x56,
			0x34,
			0x12,
			0xFE,
			0xFF,
			0xFF,
			0xFF,
			0xFF,
			0xFF,
			0xFF,
			0xFF,
			0x08,
			0x07,
			0x06,
			0x05,
			0x04,
			0x03,
			0x02,
			0x01,
			0x00,
			0x00,
			0x80,
			0x3F,
			0x00,
			0x00,
			0x00,
			0x00,
			0x00,
			0x00,
			0x00,
			0xC0,
			0x03,
			0x00,
			0x00,
			0x00,
			0xEA,
			0xB0,
			0x80,
			0x03,
			0x00,
			0x00,
			0x00,
			0x00,
			0x7F,
			0xFF,
			0x02,
			0x00,
			0x00,
			0x00,
			0x01,
			0x00,
			0x03,
			0x02};

		const std::vector<char>& actual = writer.GetBuffer();
		if (actual.size() != expected.size())
		{
			return {false, "Canonical packet wire format", "serialized size mismatch"};
		}

		for (std::size_t index = 0; index < expected.size(); ++index)
		{
			if (static_cast<std::uint8_t>(actual[index]) != expected[index])
			{
				return {false, "Canonical packet wire format", "serialized byte mismatch"};
			}
		}

		return {true, "Canonical packet wire format", "passed"};
	}

	STestResult RunMalformedPacketLengthGuardTest()
	{
		using namespace NetworkLib::Packet::Serialization;

		const std::array<char, 1> invalidBool = {static_cast<char>(2)};
		FPacketReader boolReader(invalidBool.data(), invalidBool.size());
		bool boolValue = false;
		if (boolReader.Read(boolValue))
		{
			return {false, "Malformed packet length guard", "bool values other than 0 and 1 must be rejected"};
		}

		const std::array<char, 4> invalidVectorCount = {
			static_cast<char>(0xFF), static_cast<char>(0xFF), static_cast<char>(0xFF), static_cast<char>(0xFF)};
		FPacketReader vectorReader(invalidVectorCount.data(), invalidVectorCount.size());
		std::vector<std::uint32_t> values;
		if (vectorReader.Read(values))
		{
			return {false, "Malformed packet length guard", "oversized vector count must be rejected"};
		}

		return {true, "Malformed packet length guard", "passed"};
	}

	STestResult RunCanonicalEncryptedFrameTest()
	{
		using namespace NetworkLib::Crypto;
		using namespace NetworkLib::Packet::Framing;
		using namespace NetworkLib::Packet::Serialization;

		constexpr std::uint16_t kOpcode = 0x7E01;
		constexpr std::uint8_t kPacketKey = 0x37;
		constexpr std::uint8_t kRandomKey = 0x5A;

		FPacketWriter writer;
		writer.Write(true);
		writer.Write(static_cast<std::int8_t>(-2));
		writer.Write(static_cast<std::uint8_t>(0xA5));
		writer.Write(static_cast<std::int16_t>(-0x1234));
		writer.Write(static_cast<std::uint16_t>(0xBEEF));
		writer.Write(static_cast<std::int32_t>(-123456789));
		writer.Write(static_cast<std::uint32_t>(0x89ABCDEF));
		writer.Write(static_cast<std::int64_t>(-0x0102030405060708LL));
		writer.Write(static_cast<std::uint64_t>(0x0123456789ABCDEF));
		writer.Write(1.5F);
		writer.Write(-2.25);
		writer.Write(std::string("한글"));

		const std::array<std::uint8_t, 4> rawBytes = {0x00, 0x7F, 0x80, 0xFF};
		writer.Write(std::span<const std::uint8_t>(rawBytes));
		writer.Write(std::vector<std::uint16_t>{0x0001, 0x1234, 0xFFFF});

		std::vector<char> body(writer.GetBuffer().begin(), writer.GetBuffer().end());
		std::vector<char> contentPayload = BuildContentPayload(kOpcode, std::move(body));

		SDefaultPacketCipherConfig cipherConfig{};
		cipherConfig.enabled = true;
		cipherConfig.packetKey = kPacketKey;
		FDefaultPacketCipher cipher(cipherConfig);
		cipher.Encode(contentPayload.data(), static_cast<int>(contentPayload.size()), kRandomKey);

		SOutgoingPacket outgoingPacket{};
		outgoingPacket.randomKey = kRandomKey;
		outgoingPacket.checkSum = cipher.CalculateChecksum(contentPayload.data(), static_cast<int>(contentPayload.size()));
		outgoingPacket.payload = contentPayload.data();
		outgoingPacket.payloadLength = static_cast<std::int32_t>(contentPayload.size());

		FDefaultPacketFramer framer;
		std::vector<char> frame;
		if (!framer.BuildPacket(outgoingPacket, frame))
		{
			return {false, "Canonical encrypted frame", "frame build failed"};
		}

		constexpr std::array<std::uint8_t, 77> expected = {0x49,
			0x00,
			0x5A,
			0x3C,
			0x62,
			0x53,
			0xA9,
			0x98,
			0xAA,
			0xF5,
			0xAD,
			0x03,
			0xAF,
			0x4B,
			0x9F,
			0x3E,
			0x39,
			0xB2,
			0x00,
			0x8E,
			0x6B,
			0x65,
			0x69,
			0x79,
			0x03,
			0x9D,
			0x29,
			0xB1,
			0xC3,
			0xCD,
			0x9C,
			0xBD,
			0x51,
			0x79,
			0xD2,
			0x9D,
			0xDB,
			0x9E,
			0xDF,
			0x5F,
			0x60,
			0xE6,
			0x98,
			0xA9,
			0xE8,
			0x2C,
			0x64,
			0xB5,
			0x20,
			0x42,
			0xF8,
			0x85,
			0x88,
			0xED,
			0x6B,
			0x80,
			0xE3,
			0x77,
			0xDD,
			0x82,
			0x93,
			0xF1,
			0xE9,
			0x7C,
			0x38,
			0x4D,
			0x45,
			0x9F,
			0xA9,
			0x78,
			0x05,
			0x14,
			0xA3,
			0xC7,
			0xDF,
			0x9D,
			0x84};

		if (frame.size() != expected.size())
		{
			return {false, "Canonical encrypted frame", "frame size mismatch"};
		}

		for (std::size_t index = 0; index < expected.size(); ++index)
		{
			if (static_cast<std::uint8_t>(frame[index]) != expected[index])
			{
				return {false, "Canonical encrypted frame", "frame byte mismatch"};
			}
		}

		return {true, "Canonical encrypted frame", "passed"};
	}

	template <>
	STestResult RunParallelSumTest<TQueue>(
		const char* testName)
	{
		constexpr int kProducerCount = 4;
		constexpr int kConsumerCount = 4;
		constexpr int kItemsPerProducer = 50000;
		const std::int64_t expectedCount = static_cast<std::int64_t>(kProducerCount) * kItemsPerProducer;

		TQueue queue;
		std::atomic<std::int64_t> producedSum = 0;
		std::atomic<std::int64_t> consumedSum = 0;
		std::atomic<std::int64_t> consumedCount = 0;
		std::atomic<int> activeProducers = kProducerCount;

		std::vector<std::thread> producerThreads;
		for (int producerIndex = 0; producerIndex < kProducerCount; ++producerIndex)
		{
			producerThreads.emplace_back(
				[&, producerIndex]()
				{
					const int baseValue = producerIndex * kItemsPerProducer;
					for (int offset = 1; offset <= kItemsPerProducer; ++offset)
					{
						const int value = baseValue + offset;
						queue.Enqueue(value);
						producedSum.fetch_add(value, std::memory_order_relaxed);
					}

					activeProducers.fetch_sub(1, std::memory_order_release);
				});
		}

		std::vector<std::thread> consumerThreads;
		for (int consumerIndex = 0; consumerIndex < kConsumerCount; ++consumerIndex)
		{
			consumerThreads.emplace_back(
				[&]()
				{
					while (true)
					{
						int value = 0;
						if (queue.Dequeue(value))
						{
							consumedSum.fetch_add(value, std::memory_order_relaxed);
							const std::int64_t newCount = consumedCount.fetch_add(1, std::memory_order_relaxed) + 1;
							if (newCount >= expectedCount)
							{
								return;
							}

							continue;
						}

						if (activeProducers.load(std::memory_order_acquire) == 0 &&
							consumedCount.load(std::memory_order_relaxed) >= expectedCount)
						{
							return;
						}

						std::this_thread::yield();
					}
				});
		}

		for (auto& producerThread : producerThreads)
		{
			producerThread.join();
		}

		for (auto& consumerThread : consumerThreads)
		{
			consumerThread.join();
		}

		if (consumedCount.load() != expectedCount)
		{
			return {false, testName, "consumed item count mismatch"};
		}

		if (consumedSum.load() != producedSum.load())
		{
			return {false, testName, "sum mismatch after parallel run"};
		}

		return {true, testName, "passed"};
	}
}

int main()
{
	std::vector<STestResult> results;
	results.push_back(RunLinearQueueTest<TQueue>());
	results.push_back(RunParallelSumTest<TQueue>("Queue parallel sum"));
	results.push_back(RunParallelSumTest<TStack>("Stack parallel sum"));
	results.push_back(RunTlsMemoryPoolParallelTest());
	results.push_back(RunPacketCipherRoundTripTest());
	results.push_back(RunNullPacketCipherTest());
	results.push_back(RunPacketFramerRoundTripTest());
	results.push_back(RunPacketFramerSizeLimitTest());
	results.push_back(RunPacketFramerPartialReceiveTest());
	results.push_back(RunPacketFramerRecvBufferTest());
	results.push_back(RunPacketFramerPacketViewTest());
	results.push_back(RunGeneratedEchoPacketRoundTripTest());
	results.push_back(RunGeneratedChatContainerPacketRoundTripTest());
	results.push_back(RunPacketScalarContainerBulkRoundTripTest());
	results.push_back(RunPacketBytesViewRoundTripTest());
	results.push_back(RunGeneratedRoomFlowPacketRoundTripTest());
	results.push_back(RunGeneratedPacketEstimatedSizeTest());
	results.push_back(RunCanonicalPacketWireFormatTest());
	results.push_back(RunMalformedPacketLengthGuardTest());
	results.push_back(RunCanonicalEncryptedFrameTest());

	bool allPassed = true;
	for (const STestResult& result : results)
	{
		std::cout << "[" << (result.passed ? "PASS" : "FAIL") << "] " << result.name << " : " << result.detail << "\n";
		allPassed = allPassed && result.passed;
	}

	return allPassed ? 0 : 1;
}
