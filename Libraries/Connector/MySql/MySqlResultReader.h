#pragma once

namespace Connector::MySql
{
	template <typename TValue>
		requires std::is_integral_v<TValue> && std::is_unsigned_v<TValue>
	bool ParseUnsigned(
		const FMySqlCell& cell,
		TValue& outValue)
	{
		if (!cell.has_value())
		{
			return false;
		}
		const std::string& value = *cell;
		const auto parsed = std::from_chars(value.data(), value.data() + value.size(), outValue);
		return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
	}

	const SMySqlResultSet* FindFirstRows(const std::vector<SMySqlResultSet>& resultSets) noexcept;
}
