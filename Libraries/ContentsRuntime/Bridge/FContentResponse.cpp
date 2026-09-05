#include "ContentsRuntimePch.h"

#include "Bridge/IContentBridge.h"
#include "Session/FContentSession.h"

namespace ContentsRuntime::Bridge
{
	bool SendContentPacket(
		IContentBridge& bridge,
		Session::FContentSession& session,
		const Session::FRequestProcessingToken& requestToken,
		const NetworkLib::Packet::Serialization::IResponsePacket& response)
	{
		if (!session.IsConnected() || !session.IsCurrentRequest(requestToken))
		{
			return false;
		}

		if (!bridge.SendPacket(session.GetSessionId(), NetworkLib::Packet::Serialization::BuildOutgoingContentPacket(response)))
		{
			session.MarkDisconnected();
			bridge.DisconnectSession(session.GetSessionId());
			return false;
		}

		return session.CompleteRequest(requestToken);
	}

	bool SendContentPacket(
		IContentBridge& bridge,
		Session::FContentRequestContext& requestContext,
		const NetworkLib::Packet::Serialization::IResponsePacket& response)
	{
		if (!requestContext.IsCurrent())
		{
			return false;
		}

		Session::FContentSession& session = requestContext.GetSession();
		if (!session.IsConnected())
		{
			return false;
		}

		if (!bridge.SendPacket(session.GetSessionId(), NetworkLib::Packet::Serialization::BuildOutgoingContentPacket(response)))
		{
			session.MarkDisconnected();
			bridge.DisconnectSession(session.GetSessionId());
			return false;
		}

		return requestContext.Complete();
	}
}
