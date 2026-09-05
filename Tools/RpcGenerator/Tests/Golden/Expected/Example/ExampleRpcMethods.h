#pragma once

// Generated from RPC YAML. Keep deterministic layout; do not format by hand.
// clang-format off
namespace TestProtocol::Example
{
	enum class EResult : std::uint8_t;
	struct FEnvelope;
	struct FPayload;

	using FUserId = std::uint64_t;
	using FCommandId = FUserId;
	using FPayloadAlias = FPayload;

	inline constexpr RpcLib::Protocol::FRpcServiceId kExampleServiceId = 42;

	enum class EResult : std::uint8_t
	{
		Success = 0,
		Failed = 1,
	};

	struct FPayload final
	{
		std::string text{};
		std::vector<std::uint32_t> values{};

		void Serialize(NetworkLib::Packet::Serialization::FPacketWriter& writer) const
		{
			if (!RpcLib::Protocol::WriteRpcValue(writer, text))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, values))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
		}

		bool Deserialize(NetworkLib::Packet::Serialization::FPacketReader& reader)
		{
			return RpcLib::Protocol::ReadRpcValue(reader, text) &&
				RpcLib::Protocol::ReadRpcValue(reader, values);
		}
	};

	struct FEnvelope final
	{
		FCommandId commandId{};
		FPayloadAlias payload{};

		void Serialize(NetworkLib::Packet::Serialization::FPacketWriter& writer) const
		{
			if (!RpcLib::Protocol::WriteRpcValue(writer, commandId))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
			if (!RpcLib::Protocol::WriteRpcValue(writer, payload))
			{
				throw std::runtime_error("RPC struct serialization failed.");
			}
		}

		bool Deserialize(NetworkLib::Packet::Serialization::FPacketReader& reader)
		{
			return RpcLib::Protocol::ReadRpcValue(reader, commandId) &&
				RpcLib::Protocol::ReadRpcValue(reader, payload);
		}
	};

	struct FQueryRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kExampleServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 1;
		static constexpr const char* kName = "Example.Query";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FRequestArguments = std::tuple<FUserId, std::string>;
		using FResponseArguments = std::tuple<EResult, FEnvelope>;
	};

	struct FChangedNoti final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kExampleServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 2;
		static constexpr const char* kName = "Example.Changed";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FArguments = std::tuple<FUserId, FPayload>;
	};

	struct FExecuteRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kExampleServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 3;
		static constexpr const char* kName = "Example.Execute";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FRequestArguments = std::tuple<FUserId, FCommandId>;
		using FResponseArguments = std::tuple<EResult>;
	};

	struct FExecuteNoti final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kExampleServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 3;
		static constexpr const char* kName = "Example.Execute";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FArguments = std::tuple<FUserId, EResult>;
	};
}
// clang-format on
