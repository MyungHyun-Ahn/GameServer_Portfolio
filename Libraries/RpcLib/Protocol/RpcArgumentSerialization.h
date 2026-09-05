#pragma once

namespace RpcLib::Protocol
{
	template <typename TValue> struct TIsRpcVector : std::false_type
	{
	};

	template <typename TValue, typename TAllocator> struct TIsRpcVector<std::vector<TValue, TAllocator>> : std::true_type
	{
		using FValueType = TValue;
	};

	template <typename TValue> inline constexpr bool TIsRpcVectorV = TIsRpcVector<TValue>::value;

	template <typename TValue>
	concept CRpcScalar = std::is_integral_v<TValue> || std::is_floating_point_v<TValue> || std::is_enum_v<TValue>;

	template <typename TValue>
	concept CRpcMemberWritable = requires(const TValue& value, NetworkLib::Packet::Serialization::FPacketWriter& writer) {
		{ value.Serialize(writer) } -> std::same_as<void>;
	};

	template <typename TValue>
	concept CRpcMemberReadable = requires(TValue& value, NetworkLib::Packet::Serialization::FPacketReader& reader) {
		{ value.Deserialize(reader) } -> std::same_as<bool>;
	};

	template <typename TValue>
	bool WriteRpcValue(
		NetworkLib::Packet::Serialization::FPacketWriter& writer,
		const TValue& value)
	{
		using FValue = std::remove_cvref_t<TValue>;
		if constexpr (CRpcScalar<FValue> || std::same_as<FValue, std::string> || std::same_as<FValue, std::string_view>)
		{
			writer.Write(value);
			return true;
		}
		else if constexpr (TIsRpcVectorV<FValue>)
		{
			if (value.size() > kMaxRpcCollectionElementCount || value.size() > std::numeric_limits<std::uint32_t>::max())
			{
				return false;
			}

			writer.Write(static_cast<std::uint32_t>(value.size()));
			for (const auto& element : value)
			{
				if (!WriteRpcValue(writer, element))
				{
					return false;
				}
			}

			return true;
		}
		else if constexpr (CRpcMemberWritable<FValue>)
		{
			value.Serialize(writer);
			return true;
		}
		else
		{
			static_assert(std::is_same_v<FValue, void>, "RPC argument type is not serializable.");
			return false;
		}
	}

	template <typename TValue>
	bool ReadRpcValue(
		NetworkLib::Packet::Serialization::FPacketReader& reader,
		TValue& outValue)
	{
		using FValue = std::remove_cvref_t<TValue>;
		if constexpr (CRpcScalar<FValue> || std::same_as<FValue, std::string>)
		{
			return reader.Read(outValue);
		}
		else if constexpr (TIsRpcVectorV<FValue>)
		{
			std::uint32_t count = 0;
			if (!reader.Read(count) || count > kMaxRpcCollectionElementCount)
			{
				return false;
			}

			outValue.clear();
			outValue.reserve(count);
			for (std::uint32_t index = 0; index < count; ++index)
			{
				typename TIsRpcVector<FValue>::FValueType element{};
				if (!ReadRpcValue(reader, element))
				{
					return false;
				}

				outValue.push_back(std::move(element));
			}

			return true;
		}
		else if constexpr (CRpcMemberReadable<FValue>)
		{
			return outValue.Deserialize(reader);
		}
		else
		{
			static_assert(std::is_same_v<FValue, void>, "RPC argument type is not deserializable.");
			return false;
		}
	}

	template <typename... TValues>
	bool WriteRpcArguments(
		NetworkLib::Packet::Serialization::FPacketWriter& writer,
		const std::tuple<TValues...>& arguments)
	{
		bool success = true;
		std::apply(
			[&writer, &success](const auto&... values)
			{
				((success = success && WriteRpcValue(writer, values)), ...);
			},
			arguments);
		return success;
	}

	template <typename... TValues>
	bool ReadRpcArguments(
		NetworkLib::Packet::Serialization::FPacketReader& reader,
		std::tuple<TValues...>& outArguments)
	{
		bool success = true;
		std::apply(
			[&reader, &success](auto&... values)
			{
				((success = success && ReadRpcValue(reader, values)), ...);
			},
			outArguments);
		return success;
	}
}
