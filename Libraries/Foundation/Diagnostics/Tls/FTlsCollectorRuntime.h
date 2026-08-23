#pragma once

namespace Foundation::Diagnostics
{
	class FTlsCollectorRuntime
	{
	public:
		class FRegisteredTlsShard
		{
		public:
			explicit FRegisteredTlsShard(
				FTlsCollectorRuntime& ownerRuntime) noexcept
				: m_ownerRuntime(&ownerRuntime)
			{
				m_ownerRuntime->RegisterTlsShard(*this);
			}

			FRegisteredTlsShard(const FRegisteredTlsShard&) = delete;
			FRegisteredTlsShard& operator=(const FRegisteredTlsShard&) = delete;

			virtual ~FRegisteredTlsShard() noexcept
			{
				DetachFromOwnerRuntime();
			}

			FTlsCollectorRuntime* GetOwnerRuntime() const noexcept
			{
				return m_ownerRuntime;
			}

		private:
			friend class FTlsCollectorRuntime;

			void DetachFromOwnerRuntime() noexcept
			{
				FTlsCollectorRuntime* const ownerRuntime = m_ownerRuntime;
				if (ownerRuntime == nullptr)
				{
					return;
				}

				ownerRuntime->UnregisterTlsShard(*this);
			}

		private:
			FTlsCollectorRuntime* m_ownerRuntime = nullptr;
			FRegisteredTlsShard* m_previous = nullptr;
			FRegisteredTlsShard* m_next = nullptr;
		};

	public:
		FTlsCollectorRuntime() = default;
		FTlsCollectorRuntime(const FTlsCollectorRuntime&) = delete;
		FTlsCollectorRuntime& operator=(const FTlsCollectorRuntime&) = delete;

		virtual ~FTlsCollectorRuntime() noexcept
		{
			DetachAllTlsShards();
		}

	protected:
		template <typename TCallback>
		void ForEachRegisteredTlsShard(
			TCallback&& callback) const
		{
			const std::lock_guard<std::mutex> lock(m_tlsShardMutex);
			for (FRegisteredTlsShard* shard = m_tlsShardHead; shard != nullptr; shard = shard->m_next)
			{
				callback(*shard);
			}
		}

	private:
		void RegisterTlsShard(
			FRegisteredTlsShard& shard) noexcept
		{
			const std::lock_guard<std::mutex> lock(m_tlsShardMutex);
			shard.m_previous = nullptr;
			shard.m_next = m_tlsShardHead;
			if (m_tlsShardHead != nullptr)
			{
				m_tlsShardHead->m_previous = &shard;
			}

			m_tlsShardHead = &shard;
		}

		void UnregisterTlsShard(
			FRegisteredTlsShard& shard) noexcept
		{
			const std::lock_guard<std::mutex> lock(m_tlsShardMutex);
			if (shard.m_ownerRuntime != this)
			{
				return;
			}

			if (shard.m_previous != nullptr)
			{
				shard.m_previous->m_next = shard.m_next;
			}
			else
			{
				m_tlsShardHead = shard.m_next;
			}

			if (shard.m_next != nullptr)
			{
				shard.m_next->m_previous = shard.m_previous;
			}

			shard.m_ownerRuntime = nullptr;
			shard.m_previous = nullptr;
			shard.m_next = nullptr;
		}

		void DetachAllTlsShards() noexcept
		{
			const std::lock_guard<std::mutex> lock(m_tlsShardMutex);
			FRegisteredTlsShard* shard = m_tlsShardHead;
			while (shard != nullptr)
			{
				FRegisteredTlsShard* const nextShard = shard->m_next;
				shard->m_ownerRuntime = nullptr;
				shard->m_previous = nullptr;
				shard->m_next = nullptr;
				shard = nextShard;
			}

			m_tlsShardHead = nullptr;
		}

	private:
		mutable std::mutex m_tlsShardMutex;
		FRegisteredTlsShard* m_tlsShardHead = nullptr;
	};
}
