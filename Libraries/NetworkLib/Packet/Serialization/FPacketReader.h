#pragma once

namespace NetworkLib::Packet::Serialization
{
	template <typename TValue>
	concept CPacketReadableScalar = std::is_integral_v<TValue> || std::is_floating_point_v<TValue> || std::is_enum_v<TValue>;

	class FPacketReader
	{
	public:
		FPacketReader(
			const char* data,
			std::size_t size) noexcept
			: m_data(data)
			, m_size(size)
		{
		}

	public:
		bool ReadBytes(
			void* outData,
			std::size_t size) noexcept
		{
			if (!CanRead(size))
			{
				return false;
			}

			if (outData != nullptr && size > 0)
			{
				std::memcpy(outData, m_data + m_offset, size);
			}

			m_offset += size;
			return true;
		}

		template <CPacketReadableScalar TValue>
		bool Read(
			TValue& outValue) noexcept
		{
			return ReadBytes(&outValue, sizeof(TValue));
		}

		bool Read(
			bool& outValue) noexcept
		{
			std::uint8_t wireValue = 0;
			if (!Read(wireValue) || wireValue > 1)
			{
				return false;
			}

			outValue = wireValue != 0;
			return true;
		}

		bool Read(
			std::string& outValue) noexcept
		{
			std::uint32_t length = 0;
			if (!Read(length) || !CanRead(length))
			{
				return false;
			}

			outValue.resize(length);
			return ReadBytes(outValue.data(), length);
		}

		bool Read(
			std::string_view& outValue) noexcept
		{
			std::uint32_t length = 0;
			if (!Read(length) || !CanRead(length))
			{
				return false;
			}

			outValue = std::string_view(m_data + m_offset, length);
			m_offset += length;
			return true;
		}

		bool Read(
			std::span<const std::uint8_t>& outValue) noexcept
		{
			std::uint32_t length = 0;
			if (!Read(length) || !CanRead(length))
			{
				return false;
			}

			outValue = std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(m_data + m_offset), length);
			m_offset += length;
			return true;
		}

		template <typename TValue>
		bool Read(
			std::vector<TValue>& outValues) noexcept
		{
			std::uint32_t count = 0;
			if (!Read(count))
			{
				return false;
			}

			if (count > GetRemainingSize())
			{
				return false;
			}

			outValues.clear();
			if constexpr (CPacketReadableScalar<TValue>)
			{
				if (count > GetRemainingSize() / sizeof(TValue))
				{
					return false;
				}

				outValues.resize(count);
				return ReadBytes(outValues.data(), sizeof(TValue) * outValues.size());
			}

			outValues.reserve(count);

			for (std::uint32_t index = 0; index < count; ++index)
			{
				TValue value{};
				if (!Read(value))
				{
					return false;
				}

				outValues.push_back(std::move(value));
			}

			return true;
		}

		template <typename TValue, std::size_t N>
		bool Read(
			std::array<TValue, N>& outValues) noexcept
		{
			if constexpr (CPacketReadableScalar<TValue>)
			{
				return ReadBytes(outValues.data(), sizeof(TValue) * outValues.size());
			}

			for (TValue& value : outValues)
			{
				if (!Read(value))
				{
					return false;
				}
			}

			return true;
		}

		template <typename TKey, typename TValue, typename TCompare, typename TAllocator>
		bool Read(
			std::map<TKey, TValue, TCompare, TAllocator>& outValues) noexcept
		{
			std::uint32_t count = 0;
			if (!Read(count))
			{
				return false;
			}

			if (count > GetRemainingSize())
			{
				return false;
			}

			outValues.clear();
			for (std::uint32_t index = 0; index < count; ++index)
			{
				TKey key{};
				TValue value{};
				if (!Read(key) || !Read(value))
				{
					return false;
				}

				outValues.emplace(std::move(key), std::move(value));
			}

			return true;
		}

		template <typename TKey, typename TValue, typename THash, typename TKeyEqual, typename TAllocator>
		bool Read(
			std::unordered_map<TKey, TValue, THash, TKeyEqual, TAllocator>& outValues) noexcept
		{
			std::uint32_t count = 0;
			if (!Read(count))
			{
				return false;
			}

			if (count > GetRemainingSize())
			{
				return false;
			}

			outValues.clear();
			outValues.reserve(count);
			for (std::uint32_t index = 0; index < count; ++index)
			{
				TKey key{};
				TValue value{};
				if (!Read(key) || !Read(value))
				{
					return false;
				}

				outValues.emplace(std::move(key), std::move(value));
			}

			return true;
		}

		bool Skip(
			std::size_t size) noexcept
		{
			if (!CanRead(size))
			{
				return false;
			}

			m_offset += size;
			return true;
		}

		bool CanRead(
			std::size_t size) const noexcept
		{
			return m_data != nullptr && m_offset + size <= m_size;
		}

		bool IsAtEnd() const noexcept
		{
			return m_offset == m_size;
		}

		std::size_t GetRemainingSize() const noexcept
		{
			return m_size - m_offset;
		}

	private:
		const char* m_data = nullptr;
		std::size_t m_size = 0;
		std::size_t m_offset = 0;
	};
}
