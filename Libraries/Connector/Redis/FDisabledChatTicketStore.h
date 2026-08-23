#pragma once

namespace Connector
{
	class FDisabledChatTicketStore final : public IChatTicketStore
	{
	public:
		bool TryConsumeLoginTicket(std::string_view ticket, SConsumedLoginTicket& outTicket, std::string& outError) override;
	};
}
