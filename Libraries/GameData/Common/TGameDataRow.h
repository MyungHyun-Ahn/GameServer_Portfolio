#pragma once

namespace GameData
{
	template <typename TDerived, typename TKey> class TGameDataRow
	{
	public:
		using KeyType = TKey;

		constexpr const KeyType& GetKey() const noexcept
		{
			return static_cast<const TDerived*>(this)->GetKeyValue();
		}
	};
}
