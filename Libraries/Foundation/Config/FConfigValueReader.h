#pragma once

namespace Foundation::Config
{
	template <typename TEnum> struct SConfigEnumValue
	{
		std::string_view name;
		TEnum value;
	};

	class FConfigValueReader
	{
	public:
		explicit FConfigValueReader(const SConfigDocument& document) noexcept;

		bool ValidateKnownSections(std::span<const std::string_view> knownSectionNames, std::string& outError) const;
		bool ValidateKnownKeys(std::string_view sectionName, std::span<const std::string_view> knownKeyNames, std::string& outError) const;

		bool ReadRequiredString(std::string_view sectionName, std::string_view keyName, std::string& outValue, std::string& outError) const;
		bool ReadOptionalString(std::string_view sectionName, std::string_view keyName, std::string& outValue, std::string& outError) const;

		bool ReadRequiredBool(std::string_view sectionName, std::string_view keyName, bool& outValue, std::string& outError) const;
		bool ReadOptionalBool(std::string_view sectionName, std::string_view keyName, bool& outValue, std::string& outError) const;

		bool ReadRequiredInt32(std::string_view sectionName, std::string_view keyName, std::int32_t& outValue, std::string& outError) const;
		bool ReadOptionalInt32(std::string_view sectionName, std::string_view keyName, std::int32_t& outValue, std::string& outError) const;

		bool ReadRequiredUInt16(std::string_view sectionName,
			std::string_view keyName,
			std::uint16_t& outValue,
			std::string& outError) const;
		bool ReadOptionalUInt16(std::string_view sectionName,
			std::string_view keyName,
			std::uint16_t& outValue,
			std::string& outError) const;

		bool ReadRequiredUInt32(std::string_view sectionName,
			std::string_view keyName,
			std::uint32_t& outValue,
			std::string& outError) const;
		bool ReadOptionalUInt32(std::string_view sectionName,
			std::string_view keyName,
			std::uint32_t& outValue,
			std::string& outError) const;

		bool ReadRequiredInt64(std::string_view sectionName, std::string_view keyName, std::int64_t& outValue, std::string& outError) const;
		bool ReadOptionalInt64(std::string_view sectionName, std::string_view keyName, std::int64_t& outValue, std::string& outError) const;

		bool ReadRequiredUInt64(std::string_view sectionName,
			std::string_view keyName,
			std::uint64_t& outValue,
			std::string& outError) const;
		bool ReadOptionalUInt64(std::string_view sectionName,
			std::string_view keyName,
			std::uint64_t& outValue,
			std::string& outError) const;

		bool ReadRequiredFloat(std::string_view sectionName, std::string_view keyName, float& outValue, std::string& outError) const;
		bool ReadOptionalFloat(std::string_view sectionName, std::string_view keyName, float& outValue, std::string& outError) const;

		bool ReadRequiredDouble(std::string_view sectionName, std::string_view keyName, double& outValue, std::string& outError) const;
		bool ReadOptionalDouble(std::string_view sectionName, std::string_view keyName, double& outValue, std::string& outError) const;

		template <typename TEnum, std::size_t TCount>
		bool ReadRequiredEnum(
			std::string_view sectionName,
			std::string_view keyName,
			const std::array<SConfigEnumValue<TEnum>, TCount>& enumValues,
			TEnum& outValue,
			std::string& outError) const
		{
			return ReadEnumValue(sectionName, keyName, true, std::span<const SConfigEnumValue<TEnum>>(enumValues), outValue, outError);
		}

		template <typename TEnum, std::size_t TCount>
		bool ReadOptionalEnum(
			std::string_view sectionName,
			std::string_view keyName,
			const std::array<SConfigEnumValue<TEnum>, TCount>& enumValues,
			TEnum& outValue,
			std::string& outError) const
		{
			return ReadEnumValue(sectionName, keyName, false, std::span<const SConfigEnumValue<TEnum>>(enumValues), outValue, outError);
		}

	private:
		const SConfigSection* FindSection(std::string_view sectionName) const noexcept;
		const SConfigScalarValue* FindScalar(std::string_view sectionName, std::string_view keyName, std::string& outError) const;

		template <typename TValue>
		bool ReadValue(std::string_view sectionName, std::string_view keyName, bool required, TValue& outValue, std::string& outError)
			const;

		template <typename TEnum>
		bool ReadEnumValue(
			std::string_view sectionName,
			std::string_view keyName,
			bool required,
			std::span<const SConfigEnumValue<TEnum>> enumValues,
			TEnum& outValue,
			std::string& outError) const
		{
			const SConfigSection* section = FindSection(sectionName);
			if (section == nullptr)
			{
				outError = "required config section is missing: " + std::string(sectionName);
				return false;
			}

			const auto scalarIt = section->scalarValues.find(std::string(keyName));
			if (scalarIt == section->scalarValues.end())
			{
				if (required)
				{
					outError = "required config key is missing: " + std::string(sectionName) + "." + std::string(keyName);
					return false;
				}

				outError.clear();
				return true;
			}

			for (const SConfigEnumValue<TEnum>& enumValue : enumValues)
			{
				if (scalarIt->second.value == enumValue.name)
				{
					outValue = enumValue.value;
					return true;
				}
			}

			outError = "invalid config enum value. key=" + std::string(sectionName) + "." + std::string(keyName) + " actual='" +
					   scalarIt->second.value + "'";
			return false;
		}

	private:
		const SConfigDocument& m_document;
	};
}
