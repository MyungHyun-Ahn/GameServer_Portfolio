#include "FoundationPch.h"

#include "Config/FConfigFileLoader.h"

#include <cctype>

namespace
{
	std::string Trim(
		const std::string_view text)
	{
		std::size_t beginIndex = 0;
		while (beginIndex < text.size() && std::isspace(static_cast<unsigned char>(text[beginIndex])) != 0)
		{
			++beginIndex;
		}

		std::size_t endIndex = text.size();
		while (endIndex > beginIndex && std::isspace(static_cast<unsigned char>(text[endIndex - 1])) != 0)
		{
			--endIndex;
		}

		return std::string(text.substr(beginIndex, endIndex - beginIndex));
	}

	std::size_t CountLeadingSpaces(
		const std::string_view text) noexcept
	{
		std::size_t count = 0;
		while (count < text.size() && text[count] == ' ')
		{
			++count;
		}

		return count;
	}

	bool IsCommentOrEmpty(
		const std::string_view text) noexcept
	{
		for (const char character : text)
		{
			if (std::isspace(static_cast<unsigned char>(character)) != 0)
			{
				continue;
			}

			return character == '#';
		}

		return true;
	}

	std::string StripInlineComment(
		const std::string_view text)
	{
		bool inSingleQuote = false;
		bool inDoubleQuote = false;
		for (std::size_t index = 0; index < text.size(); ++index)
		{
			const char character = text[index];
			if (character == '\'' && !inDoubleQuote)
			{
				inSingleQuote = !inSingleQuote;
				continue;
			}

			if (character == '"' && !inSingleQuote)
			{
				inDoubleQuote = !inDoubleQuote;
				continue;
			}

			if (character == '#' && !inSingleQuote && !inDoubleQuote)
			{
				return Trim(text.substr(0, index));
			}
		}

		return Trim(text);
	}

	bool TryUnquoteString(
		const std::string_view text,
		std::string& outValue)
	{
		if (text.size() < 2)
		{
			return false;
		}

		const char beginQuote = text.front();
		const char endQuote = text.back();
		if ((beginQuote != '"' || endQuote != '"') && (beginQuote != '\'' || endQuote != '\''))
		{
			return false;
		}

		outValue.clear();
		const std::string_view body = text.substr(1, text.size() - 2);
		if (beginQuote == '\'')
		{
			outValue.assign(body);
			return true;
		}

		for (std::size_t index = 0; index < body.size(); ++index)
		{
			const char character = body[index];
			if (character != '\\')
			{
				outValue.push_back(character);
				continue;
			}

			if (index + 1 >= body.size())
			{
				return false;
			}

			const char escapedCharacter = body[++index];
			switch (escapedCharacter)
			{
				case '\\':
					outValue.push_back('\\');
					break;
				case '"':
					outValue.push_back('"');
					break;
				case 'n':
					outValue.push_back('\n');
					break;
				case 'r':
					outValue.push_back('\r');
					break;
				case 't':
					outValue.push_back('\t');
					break;
				default:
					return false;
			}
		}

		return true;
	}
}

namespace Foundation::Config
{
	bool FConfigFileLoader::LoadYamlFile(
		const std::filesystem::path& filePath,
		SConfigDocument& outDocument,
		std::string& outError)
	{
		std::ifstream inputStream(filePath);
		if (!inputStream.is_open())
		{
			outError = "config file open failed: " + filePath.string();
			return false;
		}

		SConfigDocument document{};
		document.sourcePath = filePath;
		SConfigSection* currentSection = nullptr;

		std::string lineText;
		int lineNumber = 0;
		while (std::getline(inputStream, lineText))
		{
			++lineNumber;

			if (!lineText.empty() && lineText.back() == '\r')
			{
				lineText.pop_back();
			}

			if (lineNumber == 1 && lineText.size() >= 3 && static_cast<unsigned char>(lineText[0]) == 0xEF &&
				static_cast<unsigned char>(lineText[1]) == 0xBB && static_cast<unsigned char>(lineText[2]) == 0xBF)
			{
				lineText.erase(0, 3);
			}

			if (IsCommentOrEmpty(lineText))
			{
				continue;
			}

			if (lineText.find('\t') != std::string::npos)
			{
				outError = "tab indentation is not supported. line=" + std::to_string(lineNumber);
				return false;
			}

			const std::size_t indentSize = CountLeadingSpaces(lineText);
			const std::string_view trimmedLine = std::string_view(lineText).substr(indentSize);
			if (indentSize == 0)
			{
				const std::size_t colonIndex = trimmedLine.find(':');
				if (colonIndex == std::string_view::npos)
				{
					outError = "section line must contain ':'. line=" + std::to_string(lineNumber);
					return false;
				}

				const std::string sectionName = Trim(trimmedLine.substr(0, colonIndex));
				const std::string trailingText = StripInlineComment(trimmedLine.substr(colonIndex + 1));
				if (sectionName.empty())
				{
					outError = "section name is empty. line=" + std::to_string(lineNumber);
					return false;
				}

				if (!trailingText.empty())
				{
					outError = "root scalar is not supported. section='" + sectionName + "' line=" + std::to_string(lineNumber);
					return false;
				}

				if (document.sections.contains(sectionName))
				{
					outError = "duplicate section: " + sectionName;
					return false;
				}

				SConfigSection section{};
				section.name = sectionName;
				section.lineNumber = lineNumber;
				currentSection = &document.sections.emplace(sectionName, std::move(section)).first->second;
				continue;
			}

			if (indentSize != 2)
			{
				outError = "only section and one-level scalar indentation are supported. line=" + std::to_string(lineNumber);
				return false;
			}

			if (currentSection == nullptr)
			{
				outError = "scalar appears before section. line=" + std::to_string(lineNumber);
				return false;
			}

			const std::size_t colonIndex = trimmedLine.find(':');
			if (colonIndex == std::string_view::npos)
			{
				outError = "scalar line must contain ':'. line=" + std::to_string(lineNumber);
				return false;
			}

			const std::string keyName = Trim(trimmedLine.substr(0, colonIndex));
			if (keyName.empty())
			{
				outError = "scalar key is empty. line=" + std::to_string(lineNumber);
				return false;
			}

			if (currentSection->scalarValues.contains(keyName))
			{
				outError = "duplicate key '" + keyName + "' in section '" + currentSection->name + "'";
				return false;
			}

			std::string valueText = StripInlineComment(trimmedLine.substr(colonIndex + 1));
			std::string unquotedValue;
			if (TryUnquoteString(valueText, unquotedValue))
			{
				valueText = std::move(unquotedValue);
			}

			SConfigScalarValue scalarValue{};
			scalarValue.value = std::move(valueText);
			scalarValue.lineNumber = lineNumber;
			currentSection->scalarValues.emplace(keyName, std::move(scalarValue));
		}

		outDocument = std::move(document);
		return true;
	}
}
