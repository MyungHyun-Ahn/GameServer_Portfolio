#include "RpcLibPch.h"

#include "Protocol/RpcMessageSerialization.h"

namespace RpcLib::Protocol
{
	namespace
	{
		bool WritePayload(
			NetworkLib::Packet::Serialization::FPacketWriter& writer,
			const std::vector<char>& payload)
		{
			if (payload.size() > kMaxRpcPayloadBytes || payload.size() > std::numeric_limits<std::uint32_t>::max())
			{
				return false;
			}

			writer.Write(static_cast<std::uint32_t>(payload.size()));
			writer.WriteBytes(payload.data(), payload.size());
			return true;
		}

		bool ReadPayload(
			NetworkLib::Packet::Serialization::FPacketReader& reader,
			std::vector<char>& outPayload)
		{
			std::uint32_t payloadSize = 0;
			if (!reader.Read(payloadSize) || payloadSize > kMaxRpcPayloadBytes || reader.GetRemainingSize() < payloadSize)
			{
				return false;
			}

			outPayload.resize(payloadSize);
			return reader.ReadBytes(outPayload.data(), payloadSize);
		}

		template <typename TWriter>
		bool SerializeMessage(
			TWriter&& write,
			std::vector<char>& outPayload)
		{
			try
			{
				NetworkLib::Packet::Serialization::FPacketWriter writer;
				if (!write(writer))
				{
					return false;
				}

				outPayload = writer.MoveBuffer();
				return true;
			}
			catch (...)
			{
				return false;
			}
		}
	}

	bool SerializeRpcHelloRequest(
		const FRpcHelloRequest& request,
		std::vector<char>& outPayload)
	{
		return SerializeMessage(
			[&request](NetworkLib::Packet::Serialization::FPacketWriter& writer)
			{
				writer.Write(request.protocolVersion);
				writer.Write(request.serverType);
				writer.Write(request.serverInstanceId);
				return true;
			},
			outPayload);
	}

	bool DeserializeRpcHelloRequest(
		const std::span<const char> payload,
		FRpcHelloRequest& outRequest)
	{
		NetworkLib::Packet::Serialization::FPacketReader reader(payload.data(), payload.size());
		return reader.Read(outRequest.protocolVersion) && reader.Read(outRequest.serverType) && reader.Read(outRequest.serverInstanceId) &&
			   reader.IsAtEnd();
	}

	bool SerializeRpcHelloResponse(
		const FRpcHelloResponse& response,
		std::vector<char>& outPayload)
	{
		return SerializeMessage(
			[&response](NetworkLib::Packet::Serialization::FPacketWriter& writer)
			{
				writer.Write(response.protocolVersion);
				writer.Write(response.result);
				writer.Write(response.serverType);
				writer.Write(response.serverInstanceId);
				return true;
			},
			outPayload);
	}

	bool DeserializeRpcHelloResponse(
		const std::span<const char> payload,
		FRpcHelloResponse& outResponse)
	{
		NetworkLib::Packet::Serialization::FPacketReader reader(payload.data(), payload.size());
		return reader.Read(outResponse.protocolVersion) && reader.Read(outResponse.result) && reader.Read(outResponse.serverType) &&
			   reader.Read(outResponse.serverInstanceId) && reader.IsAtEnd();
	}

	bool SerializeRpcRequest(
		const FRpcRequest& request,
		std::vector<char>& outPayload)
	{
		return SerializeMessage(
			[&request](NetworkLib::Packet::Serialization::FPacketWriter& writer)
			{
				writer.Write(request.protocolVersion);
				writer.Write(request.requestId);
				writer.Write(request.serviceId);
				writer.Write(request.methodId);
				writer.Write(request.routingKey);
				writer.Write(request.originContentInstanceId);
				return WritePayload(writer, request.payload);
			},
			outPayload);
	}

	bool DeserializeRpcRequest(
		const std::span<const char> payload,
		FRpcRequest& outRequest)
	{
		NetworkLib::Packet::Serialization::FPacketReader reader(payload.data(), payload.size());
		return reader.Read(outRequest.protocolVersion) && reader.Read(outRequest.requestId) && reader.Read(outRequest.serviceId) &&
			   reader.Read(outRequest.methodId) && reader.Read(outRequest.routingKey) && reader.Read(outRequest.originContentInstanceId) &&
			   ReadPayload(reader, outRequest.payload) && reader.IsAtEnd();
	}

	bool SerializeRpcResponse(
		const FRpcResponse& response,
		std::vector<char>& outPayload)
	{
		return SerializeMessage(
			[&response](NetworkLib::Packet::Serialization::FPacketWriter& writer)
			{
				writer.Write(response.protocolVersion);
				writer.Write(response.requestId);
				writer.Write(response.serviceId);
				writer.Write(response.methodId);
				writer.Write(response.originContentInstanceId);
				writer.Write(response.resultCode);
				return WritePayload(writer, response.payload);
			},
			outPayload);
	}

	bool DeserializeRpcResponse(
		const std::span<const char> payload,
		FRpcResponse& outResponse)
	{
		NetworkLib::Packet::Serialization::FPacketReader reader(payload.data(), payload.size());
		return reader.Read(outResponse.protocolVersion) && reader.Read(outResponse.requestId) && reader.Read(outResponse.serviceId) &&
			   reader.Read(outResponse.methodId) && reader.Read(outResponse.originContentInstanceId) &&
			   reader.Read(outResponse.resultCode) && ReadPayload(reader, outResponse.payload) && reader.IsAtEnd();
	}

	bool SerializeRpcNotification(
		const FRpcNotification& notification,
		std::vector<char>& outPayload)
	{
		return SerializeMessage(
			[&notification](NetworkLib::Packet::Serialization::FPacketWriter& writer)
			{
				writer.Write(notification.protocolVersion);
				writer.Write(notification.serviceId);
				writer.Write(notification.methodId);
				writer.Write(notification.routingKey);
				writer.Write(notification.originContentInstanceId);
				return WritePayload(writer, notification.payload);
			},
			outPayload);
	}

	bool DeserializeRpcNotification(
		const std::span<const char> payload,
		FRpcNotification& outNotification)
	{
		NetworkLib::Packet::Serialization::FPacketReader reader(payload.data(), payload.size());
		return reader.Read(outNotification.protocolVersion) && reader.Read(outNotification.serviceId) &&
			   reader.Read(outNotification.methodId) && reader.Read(outNotification.routingKey) &&
			   reader.Read(outNotification.originContentInstanceId) && ReadPayload(reader, outNotification.payload) && reader.IsAtEnd();
	}
}
