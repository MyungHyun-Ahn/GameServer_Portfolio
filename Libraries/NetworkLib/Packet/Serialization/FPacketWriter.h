#pragma once

namespace NetworkLib::Packet::Serialization
{
	template <typename TValue>
	concept CPacketWritableScalar = std::is_integral_v<TValue> || std::is_floating_point_v<TValue> || std::is_enum_v<TValue>;

	class FPacketWriter
	{
	public:
		FPacketWriter() noexcept
			: m_buffer(NetworkLib::Packet::Buffer::FPacketBuffer::Create())
		{
		}

		~FPacketWriter() noexcept
		{
			NetworkLib::Packet::Buffer::FPacketBuffer::Release(m_buffer);
		}

		FPacketWriter(const FPacketWriter&) = delete;
		FPacketWriter& operator=(const FPacketWriter&) = delete;

		FPacketWriter(
			FPacketWriter&& other) noexcept
			: m_buffer(std::exchange(other.m_buffer, nullptr))
			, m_frontSize(std::exchange(other.m_frontSize, 0))
		{
		}

		FPacketWriter& operator=(
			FPacketWriter&& other) noexcept
		{
			if (this != &other)
			{
				NetworkLib::Packet::Buffer::FPacketBuffer::Release(m_buffer);
				m_buffer = std::exchange(other.m_buffer, nullptr);
				m_frontSize = std::exchange(other.m_frontSize, 0);
			}

			return *this;
		}

	public:
		void ReserveFront(
			std::size_t size)
		{
			if (size == 0 || m_buffer == nullptr)
			{
				return;
			}

			std::vector<char>& buffer = m_buffer->GetBuffer();
			assert(buffer.empty());
			if (!buffer.empty())
			{
				return;
			}

			m_frontSize = size;
			buffer.resize(size);
		}

		void Reserve(
			std::size_t size)
		{
			m_buffer->GetBuffer().reserve(size);
		}

		void ReserveAdditional(
			std::size_t size)
		{
			if (size == 0)
			{
				return;
			}

			std::vector<char>& buffer = m_buffer->GetBuffer();
			buffer.reserve(buffer.size() + size);
		}

		void WriteBytes(
			const void* data,
			std::size_t size)
		{
			if (data == nullptr || size == 0)
			{
				return;
			}

			std::vector<char>& buffer = m_buffer->GetBuffer();
			const std::size_t oldSize = buffer.size();
			buffer.resize(oldSize + size);
			std::memcpy(buffer.data() + oldSize, data, size);
		}

		template <CPacketWritableScalar TValue>
		void Write(
			const TValue& value)
		{
			WriteBytes(&value, sizeof(TValue));
		}

		void Write(
			const bool value)
		{
			const std::uint8_t wireValue = value ? 1 : 0;
			WriteBytes(&wireValue, sizeof(wireValue));
		}

		void Write(
			const std::string& value)
		{
			const std::uint32_t length = static_cast<std::uint32_t>(value.size());
			Write(length);
			WriteBytes(value.data(), value.size());
		}

		void Write(
			const std::string_view value)
		{
			const std::uint32_t length = static_cast<std::uint32_t>(value.size());
			Write(length);
			WriteBytes(value.data(), value.size());
		}

		void Write(
			const std::span<const std::uint8_t> value)
		{
			const std::uint32_t length = static_cast<std::uint32_t>(value.size());
			Write(length);
			WriteBytes(value.data(), value.size_bytes());
		}

		template <typename TValue>
		void Write(
			const std::vector<TValue>& values)
		{
			const std::uint32_t count = static_cast<std::uint32_t>(values.size());
			Write(count);

			if constexpr (CPacketWritableScalar<TValue>)
			{
				WriteBytes(values.data(), sizeof(TValue) * values.size());
				return;
			}

			for (const TValue& value : values)
			{
				Write(value);
			}
		}

		template <typename TValue, std::size_t N>
		void Write(
			const std::array<TValue, N>& values)
		{
			if constexpr (CPacketWritableScalar<TValue>)
			{
				WriteBytes(values.data(), sizeof(TValue) * values.size());
				return;
			}

			for (const TValue& value : values)
			{
				Write(value);
			}
		}

		template <typename TKey, typename TValue, typename TCompare, typename TAllocator>
		void Write(
			const std::map<TKey, TValue, TCompare, TAllocator>& values)
		{
			const std::uint32_t count = static_cast<std::uint32_t>(values.size());
			Write(count);
			for (const auto& [key, value] : values)
			{
				Write(key);
				Write(value);
			}
		}

		template <typename TKey, typename TValue, typename THash, typename TKeyEqual, typename TAllocator>
		void Write(
			const std::unordered_map<TKey, TValue, THash, TKeyEqual, TAllocator>& values)
		{
			const std::uint32_t count = static_cast<std::uint32_t>(values.size());
			Write(count);
			for (const auto& [key, value] : values)
			{
				Write(key);
				Write(value);
			}
		}

		const std::vector<char>& GetBuffer() const noexcept
		{
			return m_buffer->GetBuffer();
		}

		std::size_t GetFrontSize() const noexcept
		{
			return m_frontSize;
		}

		const char* GetBodyData() const noexcept
		{
			if (m_buffer == nullptr)
			{
				return nullptr;
			}

			const std::vector<char>& buffer = m_buffer->GetBuffer();
			if (buffer.size() <= m_frontSize)
			{
				return nullptr;
			}

			return buffer.data() + m_frontSize;
		}

		std::size_t GetBodySize() const noexcept
		{
			if (m_buffer == nullptr)
			{
				return 0;
			}

			const std::vector<char>& buffer = m_buffer->GetBuffer();
			return buffer.size() > m_frontSize ? buffer.size() - m_frontSize : 0;
		}

		void OverwriteFrontBytes(
			std::size_t offset,
			const void* data,
			std::size_t size)
		{
			if (m_buffer == nullptr || data == nullptr || size == 0)
			{
				return;
			}

			assert(offset + size <= m_frontSize);
			if (offset + size > m_frontSize)
			{
				return;
			}

			std::vector<char>& buffer = m_buffer->GetBuffer();
			std::memcpy(buffer.data() + offset, data, size);
		}

		std::vector<char> MoveBuffer() noexcept
		{
			return std::move(m_buffer->GetBuffer());
		}

		NetworkLib::Packet::Buffer::FPacketBuffer* ReleaseBuffer() noexcept
		{
			m_frontSize = 0;
			return std::exchange(m_buffer, nullptr);
		}

	private:
		NetworkLib::Packet::Buffer::FPacketBuffer* m_buffer = nullptr;
		std::size_t m_frontSize = 0;
	};
}
