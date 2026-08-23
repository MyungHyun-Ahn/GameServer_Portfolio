#pragma once

namespace GameData::Auction
{
	class FAuctionPolicyTable final
	{
	public:
		bool Load(const std::filesystem::path& filePath, std::string& outError);
		const SAuctionPolicy& Get() const noexcept;

	private:
		SAuctionPolicy m_policy;
	};
}
