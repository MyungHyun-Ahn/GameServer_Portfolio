#include "ConnectorPch.h"

#include "Connector/Redis/FDisabledChatTicketStore.h"

namespace Connector
{
	bool FDisabledChatTicketStore::TryConsumeLoginTicket(
		std::string_view,
		SConsumedLoginTicket& outTicket,
		std::string& outError)
	{
		outTicket = {};
		outError = "login ticket store is disabled.";
		return false;
	}
}
