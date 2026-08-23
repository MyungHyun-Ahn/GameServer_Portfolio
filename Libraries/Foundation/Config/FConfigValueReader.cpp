#include "FoundationPch.h"

#include "Config/FConfigValueReader.h"

#include <charconv>
#include <limits>
#include <type_traits>

namespace
{
	std::string BuildMissingSectionError(
		const std::string_view sectionName)
	{
		return "required config section is missing: " + std::string(sectionName);
	}

	std::string BuildMissingKeyError(
		const std::string_view sectionName,
		const std::string_view keyName)
	{
		return "required config key is missing: " + std::string(sectionName) + "." + std::string(keyName);
	}

	std::string BuildInvalidValueError(
		const std::string_view sectionName,
		const std::string_view keyName,
		const std::string_view expectedType,
		const std::string_view actualValue)
	{
		return "invalid config value. key=" + std::string(sectionName) + "." + std::string(keyName) +
			   " expected=" + std::string(expectedType) + " actual='" + std::string(actualValue) + "'";
	}

	std::string ToLowerAscii(
		std::string value)
	{
		for (char& character : value)
		{
			if (character >= 'A' && character <= 'Z')
			{
				character = static_cast<char>(character - 'A' + 'a');
			}
		}

		return value;
	}

	bool TryParseBool(
		const std::string& text,
		bool& outValue)
	{
		const std::string lowerText = ToLowerAscii(text);
		if (lowerText == "true" || lowerText == "1" || lowerText == "yes" || lowerText == "on")
		{
			outValue = true;
			return true;
		}

		if (lowerText == "false" || lowerText == "0" || lowerText == "no" || lowerText == "off")
		{
			outValue = false;
			return true;
		}

		return false;
	}

	template <typename TValue>
	bool TryParseIntegral(
		const std::string& text,
		TValue& outValue)
	{
		static_assert(std::is_integral_v<TValue>);

		TValue parsedValue{};
		const char* begin = text.data();
		const char* end = text.data() + text.size();
		const auto [parseEnd, errorCode] = std::from_chars(begin, end, parsedValue);
		if (errorCode != std::errc{} || parseEnd != end)
		{
			return false;
		}

		outValue = parsedValue;
		return true;
	}

	bool TryParseFloat(
		const std::string& text,
		float& outValue)
	{
		std::istringstream iss(text);
		iss >> outValue;
		return !iss.fail() && iss.eof();
	}

	bool TryParseDouble(
		const std::string& text,
		double& outValue)
	{
		std::istringstream iss(text);
		iss >> outValue;
		return !iss.fail() && iss.eof();
	}

	template <typename TValue> bool TryConvertValue(const std::string& text, TValue& outValue);

	template <>
	bool TryConvertValue<std::string>(
		const std::string& text,
		std::string& outValue)
	{
		outValue = text;
		return true;
	}

	template <>
	bool TryConvertValue<bool>(
		const std::string& text,
		bool& outValue)
	{
		return TryParseBool(text, outValue);
	}

	template <>
	bool TryConvertValue<std::int32_t>(
		const std::string& text,
		std::int32_t& outValue)
	{
		return TryParseIntegral(text, outValue);
	}

	template <>
	bool TryConvertValue<std::uint16_t>(
		const std::string& text,
		std::uint16_t& outValue)
	{
		return TryParseIntegral(text, outValue);
	}

	template <>
	bool TryConvertValue<std::uint32_t>(
		const std::string& text,
		std::uint32_t& outValue)
	{
		return TryParseIntegral(text, outValue);
	}

	template <>
	bool TryConvertValue<std::int64_t>(
		const std::string& text,
		std::int64_t& outValue)
	{
		return TryParseIntegral(text, outValue);
	}

	template <>
	bool TryConvertValue<std::uint64_t>(
		const std::string& text,
		std::uint64_t& outValue)
	{
		return TryParseIntegral(text, outValue);
	}

	template <>
	bool TryConvertValue<float>(
		const std::string& text,
		float& outValue)
	{
		return TryParseFloat(text, outValue);
	}

	template <>
	bool TryConvertValue<double>(
		const std::string& text,
		double& outValue)
	{
		return TryParseDouble(text, outValue);
	}

	template <typename TValue> constexpr std::string_view GetExpectedTypeName()
	{
		if constexpr (std::is_same_v<TValue, std::string>)
		{
			return "string";
		}
		else if constexpr (std::is_same_v<TValue, bool>)
		{
			return "bool";
		}
		else if constexpr (std::is_same_v<TValue, std::int32_t>)
		{
			return "int32";
		}
		else if constexpr (std::is_same_v<TValue, std::uint16_t>)
		{
			return "uint16";
		}
		else if constexpr (std::is_same_v<TValue, std::uint32_t>)
		{
			return "uint32";
		}
		else if constexpr (std::is_same_v<TValue, std::int64_t>)
		{
			return "int64";
		}
		else if constexpr (std::is_same_v<TValue, std::uint64_t>)
		{
			return "uint64";
		}
		else if constexpr (std::is_same_v<TValue, float>)
		{
			return "float";
		}
		else if constexpr (std::is_same_v<TValue, double>)
		{
			return "double";
		}

		return "unknown";
	}
}

namespace Foundation::Config
{
	FConfigValueReader::FConfigValueReader(
		const SConfigDocument& document) noexcept
		: m_document(document)
	{
	}

	bool FConfigValueReader::ValidateKnownSections(
		std::span<const std::string_view> knownSectionNames,
		std::string& outError) const
	{
		for (const auto& [sectionName, section] : m_document.sections)
		{
			(void)section;

			bool found = false;
			for (const std::string_view knownSectionName : knownSectionNames)
			{
				if (sectionName == knownSectionName)
				{
					found = true;
					break;
				}
			}

			if (!found)
			{
				outError = "unknown config section: " + sectionName;
				return false;
			}
		}

		return true;
	}

	bool FConfigValueReader::ValidateKnownKeys(
		std::string_view sectionName,
		std::span<const std::string_view> knownKeyNames,
		std::string& outError) const
	{
		const SConfigSection* section = FindSection(sectionName);
		if (section == nullptr)
		{
			outError = BuildMissingSectionError(sectionName);
			return false;
		}

		for (const auto& [keyName, scalarValue] : section->scalarValues)
		{
			(void)scalarValue;

			bool found = false;
			for (const std::string_view knownKeyName : knownKeyNames)
			{
				if (keyName == knownKeyName)
				{
					found = true;
					break;
				}
			}

			if (!found)
			{
				outError = "unknown config key: " + std::string(sectionName) + "." + keyName;
				return false;
			}
		}

		return true;
	}

	bool FConfigValueReader::ReadRequiredString(
		std::string_view sectionName,
		std::string_view keyName,
		std::string& outValue,
		std::string& outError) const
	{
		return ReadValue(sectionName, keyName, true, outValue, outError);
	}

	bool FConfigValueReader::ReadOptionalString(
		std::string_view sectionName,
		std::string_view keyName,
		std::string& outValue,
		std::string& outError) const
	{
		return ReadValue(sectionName, keyName, false, outValue, outError);
	}

	bool FConfigValueReader::ReadRequiredBool(
		std::string_view sectionName,
		std::string_view keyName,
		bool& outValue,
		std::string& outError) const
	{
		return ReadValue(sectionName, keyName, true, outValue, outError);
	}

	bool FConfigValueReader::ReadOptionalBool(
		std::string_view sectionName,
		std::string_view keyName,
		bool& outValue,
		std::string& outError) const
	{
		return ReadValue(sectionName, keyName, false, outValue, outError);
	}

	bool FConfigValueReader::ReadRequiredInt32(
		std::string_view sectionName,
		std::string_view keyName,
		std::int32_t& outValue,
		std::string& outError) const
	{
		return ReadValue(sectionName, keyName, true, outValue, outError);
	}

	bool FConfigValueReader::ReadOptionalInt32(
		std::string_view sectionName,
		std::string_view keyName,
		std::int32_t& outValue,
		std::string& outError) const
	{
		return ReadValue(sectionName, keyName, false, outValue, outError);
	}

	bool FConfigValueReader::ReadRequiredUInt16(
		std::string_view sectionName,
		std::string_view keyName,
		std::uint16_t& outValue,
		std::string& outError) const
	{
		return ReadValue(sectionName, keyName, true, outValue, outError);
	}

	bool FConfigValueReader::ReadOptionalUInt16(
		std::string_view sectionName,
		std::string_view keyName,
		std::uint16_t& outValue,
		std::string& outError) const
	{
		return ReadValue(sectionName, keyName, false, outValue, outError);
	}

	bool FConfigValueReader::ReadRequiredUInt32(
		std::string_view sectionName,
		std::string_view keyName,
		std::uint32_t& outValue,
		std::string& outError) const
	{
		return ReadValue(sectionName, keyName, true, outValue, outError);
	}

	bool FConfigValueReader::ReadOptionalUInt32(
		std::string_view sectionName,
		std::string_view keyName,
		std::uint32_t& outValue,
		std::string& outError) const
	{
		return ReadValue(sectionName, keyName, false, outValue, outError);
	}

	bool FConfigValueReader::ReadRequiredInt64(
		std::string_view sectionName,
		std::string_view keyName,
		std::int64_t& outValue,
		std::string& outError) const
	{
		return ReadValue(sectionName, keyName, true, outValue, outError);
	}

	bool FConfigValueReader::ReadOptionalInt64(
		std::string_view sectionName,
		std::string_view keyName,
		std::int64_t& outValue,
		std::string& outError) const
	{
		return ReadValue(sectionName, keyName, false, outValue, outError);
	}

	bool FConfigValueReader::ReadRequiredUInt64(
		std::string_view sectionName,
		std::string_view keyName,
		std::uint64_t& outValue,
		std::string& outError) const
	{
		return ReadValue(sectionName, keyName, true, outValue, outError);
	}

	bool FConfigValueReader::ReadOptionalUInt64(
		std::string_view sectionName,
		std::string_view keyName,
		std::uint64_t& outValue,
		std::string& outError) const
	{
		return ReadValue(sectionName, keyName, false, outValue, outError);
	}

	bool FConfigValueReader::ReadRequiredFloat(
		std::string_view sectionName,
		std::string_view keyName,
		float& outValue,
		std::string& outError) const
	{
		return ReadValue(sectionName, keyName, true, outValue, outError);
	}

	bool FConfigValueReader::ReadOptionalFloat(
		std::string_view sectionName,
		std::string_view keyName,
		float& outValue,
		std::string& outError) const
	{
		return ReadValue(sectionName, keyName, false, outValue, outError);
	}

	bool FConfigValueReader::ReadRequiredDouble(
		std::string_view sectionName,
		std::string_view keyName,
		double& outValue,
		std::string& outError) const
	{
		return ReadValue(sectionName, keyName, true, outValue, outError);
	}

	bool FConfigValueReader::ReadOptionalDouble(
		std::string_view sectionName,
		std::string_view keyName,
		double& outValue,
		std::string& outError) const
	{
		return ReadValue(sectionName, keyName, false, outValue, outError);
	}

	const SConfigSection* FConfigValueReader::FindSection(
		std::string_view sectionName) const noexcept
	{
		const auto sectionIt = m_document.sections.find(std::string(sectionName));
		if (sectionIt == m_document.sections.end())
		{
			return nullptr;
		}

		return &sectionIt->second;
	}

	const SConfigScalarValue* FConfigValueReader::FindScalar(
		std::string_view sectionName,
		std::string_view keyName,
		std::string& outError) const
	{
		const SConfigSection* section = FindSection(sectionName);
		if (section == nullptr)
		{
			outError = BuildMissingSectionError(sectionName);
			return nullptr;
		}

		const auto scalarIt = section->scalarValues.find(std::string(keyName));
		if (scalarIt == section->scalarValues.end())
		{
			return nullptr;
		}

		return &scalarIt->second;
	}

	template <typename TValue>
	bool FConfigValueReader::ReadValue(
		std::string_view sectionName,
		std::string_view keyName,
		const bool required,
		TValue& outValue,
		std::string& outError) const
	{
		const SConfigScalarValue* scalarValue = FindScalar(sectionName, keyName, outError);
		if (scalarValue == nullptr)
		{
			if (required)
			{
				if (outError.empty())
				{
					outError = BuildMissingKeyError(sectionName, keyName);
				}

				return false;
			}

			if (!outError.empty() && outError == BuildMissingSectionError(sectionName))
			{
				return false;
			}

			outError.clear();
			return true;
		}

		if (!TryConvertValue<TValue>(scalarValue->value, outValue))
		{
			outError = BuildInvalidValueError(sectionName, keyName, GetExpectedTypeName<TValue>(), scalarValue->value);
			return false;
		}

		return true;
	}

	template bool FConfigValueReader::ReadValue<std::string>(std::string_view sectionName,
		std::string_view keyName,
		bool required,
		std::string& outValue,
		std::string& outError) const;
	template bool FConfigValueReader::ReadValue<bool>(std::string_view sectionName,
		std::string_view keyName,
		bool required,
		bool& outValue,
		std::string& outError) const;
	template bool FConfigValueReader::ReadValue<std::int32_t>(std::string_view sectionName,
		std::string_view keyName,
		bool required,
		std::int32_t& outValue,
		std::string& outError) const;
	template bool FConfigValueReader::ReadValue<std::uint16_t>(std::string_view sectionName,
		std::string_view keyName,
		bool required,
		std::uint16_t& outValue,
		std::string& outError) const;
	template bool FConfigValueReader::ReadValue<std::uint32_t>(std::string_view sectionName,
		std::string_view keyName,
		bool required,
		std::uint32_t& outValue,
		std::string& outError) const;
	template bool FConfigValueReader::ReadValue<std::int64_t>(std::string_view sectionName,
		std::string_view keyName,
		bool required,
		std::int64_t& outValue,
		std::string& outError) const;
	template bool FConfigValueReader::ReadValue<std::uint64_t>(std::string_view sectionName,
		std::string_view keyName,
		bool required,
		std::uint64_t& outValue,
		std::string& outError) const;
	template bool FConfigValueReader::ReadValue<float>(std::string_view sectionName,
		std::string_view keyName,
		bool required,
		float& outValue,
		std::string& outError) const;
	template bool FConfigValueReader::ReadValue<double>(std::string_view sectionName,
		std::string_view keyName,
		bool required,
		double& outValue,
		std::string& outError) const;
}
