#pragma once

namespace Connector
{
	struct SConsumedLoginTicket
	{
		std::uint32_t userId = 0;
		std::uint64_t loginVersion = 0;
		std::string loginId;
		bool valid = false;
	};

	class ILoginTicketStore
	{
	public:
		virtual ~ILoginTicketStore() = default;

		virtual bool TryConsumeLoginTicket(std::string_view ticket, SConsumedLoginTicket& outTicket, std::string& outError) = 0;

		bool TryConsumeChatTicket(
			std::string_view ticket,
			SConsumedLoginTicket& outTicket,
			std::string& outError)
		{
			return TryConsumeLoginTicket(ticket, outTicket, outError);
		}
	};

	using SConsumedChatTicket = SConsumedLoginTicket;
	using IChatTicketStore = ILoginTicketStore;
}
