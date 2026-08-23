#pragma once

namespace Foundation::Config
{
	struct SConfigScalarValue
	{
		std::string value;
		int lineNumber = 0;
	};

	struct SConfigSection
	{
		std::string name;
		int lineNumber = 0;
		std::unordered_map<std::string, SConfigScalarValue> scalarValues;
	};

	struct SConfigDocument
	{
		std::filesystem::path sourcePath;
		std::unordered_map<std::string, SConfigSection> sections;
	};
}
