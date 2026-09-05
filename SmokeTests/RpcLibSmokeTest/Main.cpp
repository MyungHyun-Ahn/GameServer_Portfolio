#include "RpcLibSmokeTestPch.h"

namespace
{
	template <typename TMessage>
	concept CHasRequestIdMember = requires(TMessage message) { message.requestId; };

	template <typename TMessage>
	concept CHasResultCodeMember = requires(TMessage message) { message.resultCode; };

	struct FAddRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = 1;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 1001;
		static constexpr bool kHasRoutingKey = false;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;
		using FRequestArguments = std::tuple<std::int32_t, std::int32_t>;
		using FResponseArguments = std::tuple<std::int32_t, std::string>;
	};

	struct FNoReplyRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = 1;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 1002;
		static constexpr bool kHasRoutingKey = false;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;
		using FRequestArguments = std::tuple<std::uint64_t>;
		using FResponseArguments = std::tuple<std::uint64_t>;
	};

	struct FContextRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = 1;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 1003;
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;
		using FRequestArguments = std::tuple<std::uint64_t>;
		using FResponseArguments = std::tuple<std::uint64_t>;
	};

	struct FStateChangedNoti final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = 1;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 1001;
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;
		using FArguments = std::tuple<std::uint64_t, std::string>;
	};

	struct FThrowingNoti final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = 1;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 1004;
		static constexpr bool kHasRoutingKey = false;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;
		using FArguments = std::tuple<std::uint64_t>;
	};

	class FMockRpcTransport final : public RpcLib::Transport::IRpcTransport
	{
	public:
		struct SSentRequest final
		{
			std::uint64_t networkSessionId = 0;
			RpcLib::Protocol::FRpcRequest request;
		};

		struct SSentNotification final
		{
			std::uint64_t networkSessionId = 0;
			RpcLib::Protocol::FRpcNotification notification;
		};

		bool SendRequest(
			const std::uint64_t networkSessionId,
			const RpcLib::Protocol::FRpcRequest& request) override
		{
			if (!allowSend)
			{
				return false;
			}

			sentRequests.push_back({networkSessionId, request});
			return true;
		}

		bool SendResponse(
			const std::uint64_t,
			const RpcLib::Protocol::FRpcResponse&) override
		{
			return allowSend;
		}

		bool SendNotification(
			const std::uint64_t networkSessionId,
			const RpcLib::Protocol::FRpcNotification& notification) override
		{
			if (!allowSend)
			{
				return false;
			}

			sentNotifications.push_back({networkSessionId, notification});
			return true;
		}

		bool allowSend = true;
		std::vector<SSentRequest> sentRequests;
		std::vector<SSentNotification> sentNotifications;
	};

	bool Check(
		const bool condition,
		const std::string_view message,
		int& failureCount)
	{
		if (condition)
		{
			return true;
		}

		std::cerr << "[FAIL] " << message << '\n';
		++failureCount;
		return false;
	}
}

int main()
{
	using namespace RpcLib;
	using namespace RpcLib::Protocol;

	int failureCount = 0;
	Check(kRpcProtocolVersion == 2, "notification capability is guarded by RPC protocol version 2", failureCount);
	Check(IsRequestDefinitelyNotDispatched({ERpcCallError::RemoteError, ERpcResponseCode::MethodNotFound}),
		"method-not-found is classified as pre-dispatch failure",
		failureCount);
	Check(IsRequestDefinitelyNotDispatched({ERpcCallError::RemoteError, ERpcResponseCode::ProtocolMismatch}) &&
			  IsRequestDefinitelyNotDispatched({ERpcCallError::RemoteError, ERpcResponseCode::SessionNotReady}),
		"protocol and session rejection are classified as pre-dispatch failures",
		failureCount);
	Check(!IsRequestDefinitelyNotDispatched({ERpcCallError::Timeout, ERpcResponseCode::Success}) &&
			  !IsRequestDefinitelyNotDispatched({ERpcCallError::RemoteError, ERpcResponseCode::HandlerException}) &&
			  !IsRequestDefinitelyNotDispatched({ERpcCallError::RemoteError, ERpcResponseCode::InvalidPayload}),
		"ambiguous transport and post-dispatch failures remain outcome-unknown",
		failureCount);
	static_assert(!CHasRequestIdMember<FRpcNotification>, "one-way notifications must not contain a request id");
	static_assert(!CHasResultCodeMember<FRpcNotification>, "one-way notifications must not contain a response result");

	Client::FOutboundRpcClientConfig invalidOutboundConfig;
	invalidOutboundConfig.localServerType = ERpcServerType::Auction;
	invalidOutboundConfig.localServerInstanceId = 1;
	invalidOutboundConfig.expectedRemoteServerType = ERpcServerType::Cache;
	invalidOutboundConfig.expectedRemoteServerInstanceId = 1;
	Client::FOutboundRpcClient invalidOutboundClient(std::move(invalidOutboundConfig));
	std::string outboundError;
	Check(!invalidOutboundClient.Start(outboundError) && !outboundError.empty(),
		"outbound RPC client rejects an invalid endpoint before starting its background worker",
		failureCount);
	Check(!invalidOutboundClient.IsRunning() && !invalidOutboundClient.IsReady() && invalidOutboundClient.GetReadySessionId() == 0,
		"rejected outbound RPC client remains stopped",
		failureCount);
	invalidOutboundClient.Stop();

	FRpcHelloRequest helloRequest;
	helloRequest.serverType = ERpcServerType::Auction;
	helloRequest.serverInstanceId = 7;
	std::vector<char> serializedHello;
	FRpcHelloRequest deserializedHello;
	Check(SerializeRpcHelloRequest(helloRequest, serializedHello), "hello request serialization", failureCount);
	Check(DeserializeRpcHelloRequest(serializedHello, deserializedHello), "hello request deserialization", failureCount);
	Check(deserializedHello.protocolVersion == 2 && deserializedHello.serverType == ERpcServerType::Auction &&
			  deserializedHello.serverInstanceId == 7,
		"hello request round trip",
		failureCount);

	FRpcHelloResponse helloResponse;
	helloResponse.serverType = ERpcServerType::Cache;
	helloResponse.serverInstanceId = 3;
	std::vector<char> serializedHelloResponse;
	FRpcHelloResponse deserializedHelloResponse;
	Check(SerializeRpcHelloResponse(helloResponse, serializedHelloResponse), "hello response serialization", failureCount);
	Check(DeserializeRpcHelloResponse(serializedHelloResponse, deserializedHelloResponse), "hello response deserialization", failureCount);
	Check(deserializedHelloResponse.protocolVersion == 2 && deserializedHelloResponse.serverType == ERpcServerType::Cache &&
			  deserializedHelloResponse.serverInstanceId == 3,
		"hello response round trip",
		failureCount);

	FRpcRequest wireRequest;
	wireRequest.requestId = 41;
	wireRequest.serviceId = FAddRpc::kServiceId;
	wireRequest.methodId = FAddRpc::kMethodId;
	wireRequest.routingKey = 9;
	wireRequest.originContentInstanceId = 501;
	wireRequest.payload = {'r', 'p', 'c'};
	std::vector<char> serializedRequest;
	FRpcRequest deserializedRequest;
	Check(SerializeRpcRequest(wireRequest, serializedRequest), "request serialization", failureCount);
	Check(DeserializeRpcRequest(serializedRequest, deserializedRequest), "request deserialization", failureCount);
	Check(deserializedRequest.requestId == wireRequest.requestId && deserializedRequest.payload == wireRequest.payload,
		"request round trip",
		failureCount);
	serializedRequest.push_back('x');
	Check(!DeserializeRpcRequest(serializedRequest, deserializedRequest), "trailing request bytes rejected", failureCount);
	FRpcRequest maximumPayloadRequest = wireRequest;
	maximumPayloadRequest.payload.assign(kMaxRpcPayloadBytes, 'x');
	Check(SerializeRpcRequest(maximumPayloadRequest, serializedRequest), "maximum RPC payload accepted", failureCount);
	maximumPayloadRequest.payload.push_back('x');
	Check(!SerializeRpcRequest(maximumPayloadRequest, serializedRequest), "oversized RPC payload rejected", failureCount);

	FRpcNotification wireNotification;
	wireNotification.serviceId = FStateChangedNoti::kServiceId;
	wireNotification.methodId = FStateChangedNoti::kMethodId;
	wireNotification.routingKey = 73;
	wireNotification.originContentInstanceId = 502;
	wireNotification.payload = {'n', 'o', 't', 'i'};
	std::vector<char> serializedNotification;
	FRpcNotification deserializedNotification;
	FRpcNotification emptyWireNotification = wireNotification;
	emptyWireNotification.payload.clear();
	std::vector<char> serializedEmptyNotification;
	constexpr std::size_t kNotificationEnvelopeByteCount = sizeof(std::uint32_t) + sizeof(FRpcServiceId) + sizeof(FRpcMethodId) +
														   sizeof(std::uint64_t) + sizeof(std::uint64_t) + sizeof(std::uint32_t);
	Check(SerializeRpcNotification(emptyWireNotification, serializedEmptyNotification),
		"empty notification envelope serialization",
		failureCount);
	Check(serializedEmptyNotification.size() == kNotificationEnvelopeByteCount && kNotificationEnvelopeByteCount == 32,
		"notification envelope has no request id or response fields",
		failureCount);
	Check(SerializeRpcNotification(wireNotification, serializedNotification), "notification serialization", failureCount);
	Check(DeserializeRpcNotification(serializedNotification, deserializedNotification), "notification deserialization", failureCount);
	Check(deserializedNotification.serviceId == wireNotification.serviceId &&
			  deserializedNotification.methodId == wireNotification.methodId &&
			  deserializedNotification.routingKey == wireNotification.routingKey &&
			  deserializedNotification.originContentInstanceId == wireNotification.originContentInstanceId &&
			  deserializedNotification.payload == wireNotification.payload,
		"notification round trip",
		failureCount);
	serializedNotification.push_back('x');
	Check(!DeserializeRpcNotification(serializedNotification, deserializedNotification),
		"trailing notification bytes rejected",
		failureCount);
	FRpcNotification maximumPayloadNotification = wireNotification;
	maximumPayloadNotification.payload.assign(kMaxRpcPayloadBytes, 'x');
	Check(SerializeRpcNotification(maximumPayloadNotification, serializedNotification),
		"maximum notification payload accepted",
		failureCount);
	maximumPayloadNotification.payload.push_back('x');
	Check(!SerializeRpcNotification(maximumPayloadNotification, serializedNotification),
		"oversized notification payload rejected",
		failureCount);
	Check(IsRpcWireOpcode(static_cast<std::uint16_t>(ERpcWireOpcode::Notification)), "notification wire opcode recognized", failureCount);
	Check(
		!IsRpcWireOpcode(static_cast<std::uint16_t>(ERpcWireOpcode::Notification) + 1), "opcode after notification rejected", failureCount);

	Session::FRpcSessionRegistry sessionRegistry;
	Check(sessionRegistry.Add(101), "first rpc session added", failureCount);
	Check(sessionRegistry.Add(102), "second rpc session added", failureCount);
	Check(sessionRegistry.Add(103), "duplicate-instance test session added", failureCount);
	Check(sessionRegistry.Add(104), "old-protocol test session added", failureCount);
	Check(!sessionRegistry.Add(101), "duplicate rpc session rejected", failureCount);
	Check(sessionRegistry.Find(101)->BeginHandshake(), "rpc handshake started", failureCount);
	Check(sessionRegistry.Find(104)->BeginHandshake(), "old-protocol rpc handshake started", failureCount);
	Check(!sessionRegistry.MarkReady(104, ERpcServerType::Login, 1, 1),
		"version 1 peer rejected before notification-capable ready state",
		failureCount);
	Check(!sessionRegistry.Find(104)->IsReady(), "rejected version 1 peer remains non-ready", failureCount);
	Check(sessionRegistry.MarkReady(101, ERpcServerType::Cache, 1, kRpcProtocolVersion), "first rpc session ready", failureCount);
	Check(sessionRegistry.MarkReady(102, ERpcServerType::Cache, 2, kRpcProtocolVersion), "second rpc session ready", failureCount);
	Check(!sessionRegistry.MarkReady(103, ERpcServerType::Cache, 2, kRpcProtocolVersion),
		"duplicate remote server instance rejected",
		failureCount);
	Check(sessionRegistry.SelectReady({ERpcServerType::Cache, 0, 0})->GetNetworkSessionId() == 101,
		"routing key zero selects first session",
		failureCount);
	Check(sessionRegistry.SelectReady({ERpcServerType::Cache, 1, 0})->GetNetworkSessionId() == 102,
		"routing key one selects second session",
		failureCount);
	Check(sessionRegistry.SelectReady({ERpcServerType::Cache, 0, 2})->GetNetworkSessionId() == 102,
		"explicit server instance selected",
		failureCount);
	FRpcTarget exactSessionTarget{ERpcServerType::Cache, 999, 0, 101};
	Check(sessionRegistry.SelectReady(exactSessionTarget)->GetNetworkSessionId() == 101,
		"exact rpc session selected before routing",
		failureCount);
	exactSessionTarget.serverInstanceId = 2;
	Check(sessionRegistry.SelectReady(exactSessionTarget) == nullptr, "exact rpc session rejects mismatched instance", failureCount);
	exactSessionTarget.serverInstanceId = 0;
	exactSessionTarget.rpcSessionId = 999;
	Check(sessionRegistry.SelectReady(exactSessionTarget) == nullptr,
		"missing exact rpc session is not replaced by another peer",
		failureCount);

	Dispatch::FRpcMethodDispatcher dispatcher;
	Call::FRpcRequestIdGenerator requestIdGenerator;
	FMockRpcTransport transport;
	FRpcCommon rpcCommon(sessionRegistry, dispatcher, requestIdGenerator, transport, 9001, 2);

	Check(rpcCommon.Register<FAddRpc>(
			  [](Dispatch::TRpcReply<FAddRpc>& reply, const std::int32_t left, const std::int32_t right)
			  {
				  reply.Send(left + right, std::string("ok"));
			  }),
		"typed rpc method registered",
		failureCount);
	Check(!rpcCommon.Register<FAddRpc>(
			  [](Dispatch::TRpcReply<FAddRpc>& reply, const std::int32_t left, const std::int32_t right)
			  {
				  reply.Send(left + right, std::string("duplicate"));
			  }),
		"duplicate rpc method rejected",
		failureCount);
	Check(rpcCommon.Register<FNoReplyRpc>([](Dispatch::TRpcReply<FNoReplyRpc>&, const std::uint64_t) {}),
		"no-reply test method registered",
		failureCount);

	std::optional<Dispatch::FRpcCallContext> observedContext;
	std::size_t contextHandlerCallCount = 0;
	Check(rpcCommon.Register<FContextRpc>(
			  [&observedContext, &contextHandlerCallCount](
				  const Dispatch::FRpcCallContext& context, Dispatch::TRpcReply<FContextRpc>& reply, const std::uint64_t userId)
			  {
				  observedContext = context;
				  ++contextHandlerCallCount;
				  reply.Send(userId);
			  }),
		"context-aware rpc method registered",
		failureCount);

	FRpcRequest contextRequest;
	contextRequest.requestId = 7001;
	contextRequest.serviceId = FContextRpc::kServiceId;
	contextRequest.methodId = FContextRpc::kMethodId;
	contextRequest.routingKey = 3001;
	contextRequest.originContentInstanceId = 8001;
	{
		NetworkLib::Packet::Serialization::FPacketWriter writer;
		Protocol::WriteRpcArguments(writer, FContextRpc::FRequestArguments{3001});
		contextRequest.payload = writer.MoveBuffer();
	}

	const FRpcResponse contextResponse = rpcCommon.DispatchRequest(101, contextRequest);
	Check(contextResponse.resultCode == ERpcResponseCode::Success, "context-aware rpc dispatched", failureCount);
	Check(observedContext.has_value() && observedContext->rpcSessionId == 101 && observedContext->peerServerType == ERpcServerType::Cache &&
			  observedContext->peerServerInstanceId == 1 && observedContext->requestId == contextRequest.requestId &&
			  observedContext->routingKey == contextRequest.routingKey &&
			  observedContext->originContentInstanceId == contextRequest.originContentInstanceId,
		"rpc call context combines authenticated session and request envelope",
		failureCount);
	Check(contextHandlerCallCount == 1, "context-aware handler invoked once", failureCount);
	FRpcRequest mismatchedContextRequest = contextRequest;
	mismatchedContextRequest.routingKey = contextRequest.routingKey + 1;
	Check(rpcCommon.DispatchRequest(101, mismatchedContextRequest).resultCode == ERpcResponseCode::InvalidPayload,
		"request routing-key mismatch rejected before handler",
		failureCount);
	Check(contextHandlerCallCount == 1, "request routing-key mismatch did not invoke handler", failureCount);

	std::optional<Dispatch::FRpcCallContext> observedNotificationContext;
	std::uint64_t observedNotificationUserId = 0;
	std::string observedNotificationState;
	std::size_t notificationHandlerCallCount = 0;
	Check(rpcCommon.RegisterNotification<FStateChangedNoti>(
			  [&observedNotificationContext, &observedNotificationUserId, &observedNotificationState, &notificationHandlerCallCount](
				  const Dispatch::FRpcCallContext& context, const std::uint64_t userId, std::string state)
			  {
				  observedNotificationContext = context;
				  observedNotificationUserId = userId;
				  observedNotificationState = std::move(state);
				  ++notificationHandlerCallCount;
			  }),
		"typed notification registered with same service and method id as request",
		failureCount);
	Check(!rpcCommon.RegisterNotification<FStateChangedNoti>([](const std::uint64_t, std::string) {}),
		"duplicate notification rejected",
		failureCount);
	Check(dispatcher.GetRegisteredMethodCount() == 3 && dispatcher.GetRegisteredNotificationCount() == 1,
		"request and notification handlers stored separately",
		failureCount);
	Check(rpcCommon.RegisterNotification<FThrowingNoti>(
			  [](const std::uint64_t)
			  {
				  throw std::runtime_error("notification handler test");
			  }),
		"throwing notification registered",
		failureCount);

	const FRpcResponse nonReadyContextResponse = rpcCommon.DispatchRequest(103, contextRequest);
	Check(nonReadyContextResponse.resultCode == ERpcResponseCode::SessionNotReady,
		"non-ready session rejected before context-aware dispatch",
		failureCount);
	Check(contextHandlerCallCount == 1, "non-ready session did not invoke handler", failureCount);
	const std::size_t sentRequestCountBeforeRoutingMismatch = transport.sentRequests.size();
	const std::size_t pendingCountBeforeRoutingMismatch = rpcCommon.GetPendingCallCount();
	const FRpcCallStartResult mismatchedCall = rpcCommon.Call<FContextRpc>(
		{ERpcServerType::Cache, 73, 0},
		std::chrono::seconds(1),
		[](const std::uint64_t) {},
		[](const FRpcCallFailure&) {},
		std::uint64_t{3001});
	Check(!mismatchedCall.accepted && mismatchedCall.error == ERpcCallError::InvalidArgument &&
			  transport.sentRequests.size() == sentRequestCountBeforeRoutingMismatch &&
			  rpcCommon.GetPendingCallCount() == pendingCountBeforeRoutingMismatch,
		"request routing-key mismatch rejected before pending registration and send",
		failureCount);

	const std::size_t pendingCountBeforeNotification = rpcCommon.GetPendingCallCount();
	const FRpcNotificationSendResult mismatchedNotificationSend =
		rpcCommon.Notify<FStateChangedNoti>({ERpcServerType::Cache, 73, 0}, std::uint64_t{3001}, std::string("outbid"));
	Check(!mismatchedNotificationSend.accepted && mismatchedNotificationSend.error == ERpcCallError::InvalidArgument &&
			  transport.sentNotifications.empty(),
		"notification routing-key mismatch rejected before send",
		failureCount);
	const FRpcNotificationSendResult notificationSend =
		rpcCommon.Notify<FStateChangedNoti>({ERpcServerType::Cache, 3001, 0}, std::uint64_t{3001}, std::string("outbid"));
	Check(notificationSend.accepted && notificationSend.error == ERpcCallError::None && notificationSend.rpcSessionId == 102,
		"typed notification sent to selected session",
		failureCount);
	Check(rpcCommon.GetPendingCallCount() == pendingCountBeforeNotification, "notification creates no pending call", failureCount);
	Check(transport.sentNotifications.size() == 1 && transport.sentNotifications.back().networkSessionId == 102,
		"notification transport receives selected session",
		failureCount);
	Check(
		rpcCommon.DispatchNotification(102, transport.sentNotifications.back().notification) == ERpcNotificationDispatchResult::Dispatched,
		"notification dispatched",
		failureCount);
	Check(notificationHandlerCallCount == 1 && observedNotificationUserId == 3001 && observedNotificationState == "outbid",
		"notification handler arguments decoded",
		failureCount);
	Check(observedNotificationContext.has_value() && observedNotificationContext->rpcSessionId == 102 &&
			  observedNotificationContext->peerServerType == ERpcServerType::Cache &&
			  observedNotificationContext->peerServerInstanceId == 2 && observedNotificationContext->requestId == 0 &&
			  observedNotificationContext->routingKey == 3001 && observedNotificationContext->originContentInstanceId == 9001,
		"notification context combines authenticated session and envelope",
		failureCount);
	FRpcNotification mismatchedRoutingNotification = transport.sentNotifications.back().notification;
	mismatchedRoutingNotification.routingKey += 1;
	Check(rpcCommon.DispatchNotification(102, mismatchedRoutingNotification) == ERpcNotificationDispatchResult::InvalidPayload,
		"notification routing-key mismatch rejected before handler",
		failureCount);
	Check(notificationHandlerCallCount == 1, "notification routing-key mismatch did not invoke handler", failureCount);
	Check(rpcCommon.DispatchNotification(103, transport.sentNotifications.back().notification) ==
			  ERpcNotificationDispatchResult::SessionNotReady,
		"non-ready notification session rejected before dispatch",
		failureCount);
	Check(notificationHandlerCallCount == 1, "non-ready notification did not invoke handler", failureCount);

	FRpcNotification invalidNotification = transport.sentNotifications.back().notification;
	invalidNotification.payload.clear();
	Check(rpcCommon.DispatchNotification(102, invalidNotification) == ERpcNotificationDispatchResult::InvalidPayload,
		"invalid notification payload rejected",
		failureCount);
	invalidNotification = transport.sentNotifications.back().notification;
	invalidNotification.methodId = 9999;
	Check(rpcCommon.DispatchNotification(102, invalidNotification) == ERpcNotificationDispatchResult::MethodNotFound,
		"unknown notification method rejected",
		failureCount);
	invalidNotification = transport.sentNotifications.back().notification;
	invalidNotification.protocolVersion = kRpcProtocolVersion + 1;
	Check(rpcCommon.DispatchNotification(102, invalidNotification) == ERpcNotificationDispatchResult::ProtocolMismatch,
		"notification protocol mismatch rejected",
		failureCount);

	FRpcNotification throwingNotification;
	throwingNotification.serviceId = FThrowingNoti::kServiceId;
	throwingNotification.methodId = FThrowingNoti::kMethodId;
	{
		NetworkLib::Packet::Serialization::FPacketWriter writer;
		Protocol::WriteRpcArguments(writer, FThrowingNoti::FArguments{3001});
		throwingNotification.payload = writer.MoveBuffer();
	}
	bool notificationExceptionEscaped = false;
	ERpcNotificationDispatchResult throwingNotificationResult = ERpcNotificationDispatchResult::MethodNotFound;
	try
	{
		throwingNotificationResult = rpcCommon.DispatchNotification(102, throwingNotification);
	}
	catch (...)
	{
		notificationExceptionEscaped = true;
	}
	Check(throwingNotificationResult == ERpcNotificationDispatchResult::HandlerException && !notificationExceptionEscaped,
		"notification handler exception isolated",
		failureCount);

	const std::size_t sentNotificationCountBeforeFailureCases = transport.sentNotifications.size();
	const FRpcNotificationSendResult invalidNotificationTarget =
		rpcCommon.Notify<FStateChangedNoti>({ERpcServerType::Unknown, 0, 0}, std::uint64_t{1}, std::string("state"));
	Check(!invalidNotificationTarget.accepted && invalidNotificationTarget.error == ERpcCallError::InvalidArgument,
		"invalid notification target rejected",
		failureCount);
	const FRpcNotificationSendResult unavailableNotificationTarget =
		rpcCommon.Notify<FStateChangedNoti>({ERpcServerType::Login, 1, 0}, std::uint64_t{1}, std::string("state"));
	Check(!unavailableNotificationTarget.accepted && unavailableNotificationTarget.error == ERpcCallError::SessionUnavailable,
		"unavailable notification target rejected",
		failureCount);
	const FRpcNotificationSendResult oversizedNotification =
		rpcCommon.Notify<FStateChangedNoti>({ERpcServerType::Cache, 1, 0}, std::uint64_t{1}, std::string(kMaxRpcPayloadBytes, 'x'));
	Check(!oversizedNotification.accepted && oversizedNotification.error == ERpcCallError::SerializationFailed &&
			  oversizedNotification.rpcSessionId == 102,
		"oversized typed notification rejected before send",
		failureCount);
	Check(
		transport.sentNotifications.size() == sentNotificationCountBeforeFailureCases, "rejected notifications are not sent", failureCount);

	bool successCalled = false;
	bool failureCalled = false;
	const FRpcCallStartResult callResult = rpcCommon.Call<FAddRpc>(
		{ERpcServerType::Cache, 0, 0},
		std::chrono::milliseconds(100),
		[&successCalled, &failureCount](const std::int32_t sum, std::string message)
		{
			successCalled = sum == 42 && message == "ok";
			if (!successCalled)
			{
				++failureCount;
			}
		},
		[&failureCalled](const FRpcCallFailure&)
		{
			failureCalled = true;
		},
		std::int32_t{20},
		std::int32_t{22});
	Check(callResult.accepted, "typed rpc call accepted", failureCount);
	Check(!successCalled && !failureCalled, "rpc callback is not invoked inline", failureCount);
	Check(rpcCommon.GetPendingCallCount() == 1, "pending call registered", failureCount);
	Check(transport.sentRequests.back().networkSessionId == 101, "selected session used for send", failureCount);

	FRpcResponse addResponse = rpcCommon.DispatchRequest(transport.sentRequests.back().request);
	std::vector<char> serializedResponse;
	FRpcResponse receivedResponse;
	Check(SerializeRpcResponse(addResponse, serializedResponse), "response serialization", failureCount);
	Check(DeserializeRpcResponse(serializedResponse, receivedResponse), "response deserialization", failureCount);
	FRpcResponse wrongOriginResponse = receivedResponse;
	wrongOriginResponse.originContentInstanceId = 9002;
	Check(rpcCommon.ProcessResponse(101, wrongOriginResponse) == ERpcCompletionResult::OriginMismatch,
		"response routed to another content rejected",
		failureCount);
	Check(rpcCommon.GetPendingCallCount() == 1, "origin mismatch keeps pending call", failureCount);
	Check(rpcCommon.ProcessResponse(999, receivedResponse) == ERpcCompletionResult::SessionMismatch,
		"response from another rpc session rejected",
		failureCount);
	Check(rpcCommon.GetPendingCallCount() == 1, "session mismatch keeps pending call", failureCount);
	Check(rpcCommon.ProcessResponse(101, receivedResponse) == ERpcCompletionResult::Completed, "response completed", failureCount);
	Check(successCalled && !failureCalled, "success callback invoked", failureCount);
	Check(rpcCommon.GetPendingCallCount() == 0, "completed pending call removed", failureCount);
	Check(rpcCommon.ProcessResponse(101, receivedResponse) == ERpcCompletionResult::NotFound, "duplicate response ignored", failureCount);

	ERpcCallError throwingResponseError = ERpcCallError::None;
	const FRpcCallStartResult throwingResponseCall = rpcCommon.Call<FAddRpc>(
		{ERpcServerType::Cache, 0, 0},
		std::chrono::seconds(1),
		[](const std::int32_t, std::string)
		{
			throw std::runtime_error("response callback test");
		},
		[&throwingResponseError](const FRpcCallFailure& failure)
		{
			throwingResponseError = failure.error;
		},
		std::int32_t{4},
		std::int32_t{5});
	Check(throwingResponseCall.accepted, "throwing response callback call accepted", failureCount);
	FRpcResponse throwingResponse = rpcCommon.DispatchRequest(transport.sentRequests.back().request);
	bool responseCallbackEscaped = false;
	ERpcCompletionResult throwingResponseResult = ERpcCompletionResult::NotFound;
	try
	{
		throwingResponseResult = rpcCommon.ProcessResponse(101, throwingResponse);
	}
	catch (...)
	{
		responseCallbackEscaped = true;
	}
	Check(throwingResponseResult == ERpcCompletionResult::ProtocolError && !responseCallbackEscaped,
		"response callback exception isolated",
		failureCount);
	Check(throwingResponseError == ERpcCallError::ProtocolError, "response callback failure reported", failureCount);

	FRpcRequest invalidPayloadRequest = transport.sentRequests.back().request;
	invalidPayloadRequest.requestId = 500;
	invalidPayloadRequest.payload.clear();
	Check(rpcCommon.DispatchRequest(invalidPayloadRequest).resultCode == ERpcResponseCode::InvalidPayload,
		"invalid method payload rejected",
		failureCount);

	FRpcRequest noReplyRequest;
	noReplyRequest.requestId = 501;
	noReplyRequest.serviceId = FNoReplyRpc::kServiceId;
	noReplyRequest.methodId = FNoReplyRpc::kMethodId;
	noReplyRequest.originContentInstanceId = 9001;
	{
		NetworkLib::Packet::Serialization::FPacketWriter writer;
		Protocol::WriteRpcArguments(writer, FNoReplyRpc::FRequestArguments{77});
		noReplyRequest.payload = writer.MoveBuffer();
	}
	Check(rpcCommon.DispatchRequest(noReplyRequest).resultCode == ERpcResponseCode::HandlerDidNotReply,
		"missing handler reply detected",
		failureCount);

	ERpcCallError timeoutError = ERpcCallError::None;
	const FRpcCallStartResult timeoutCall = rpcCommon.Call<FAddRpc>(
		{ERpcServerType::Cache, 0, 0},
		std::chrono::milliseconds(10),
		[](const std::int32_t, std::string) {},
		[&timeoutError](const FRpcCallFailure& failure)
		{
			timeoutError = failure.error;
		},
		std::int32_t{1},
		std::int32_t{2});
	Check(timeoutCall.accepted, "timeout test call accepted", failureCount);
	Check(
		rpcCommon.ProcessTimeouts(std::chrono::steady_clock::now() + std::chrono::seconds(1)) == 1, "expired call processed", failureCount);
	Check(timeoutError == ERpcCallError::Timeout, "timeout callback invoked", failureCount);

	const FRpcCallStartResult throwingFailureCall = rpcCommon.Call<FAddRpc>(
		{ERpcServerType::Cache, 0, 0},
		std::chrono::milliseconds(10),
		[](const std::int32_t, std::string) {},
		[](const FRpcCallFailure&)
		{
			throw std::runtime_error("failure callback test");
		},
		std::int32_t{1},
		std::int32_t{2});
	Check(throwingFailureCall.accepted, "throwing failure callback call accepted", failureCount);
	bool failureCallbackEscaped = false;
	std::size_t throwingFailureExpiredCount = 0;
	try
	{
		throwingFailureExpiredCount = rpcCommon.ProcessTimeouts(std::chrono::steady_clock::now() + std::chrono::seconds(1));
	}
	catch (...)
	{
		failureCallbackEscaped = true;
	}
	Check(throwingFailureExpiredCount == 1 && !failureCallbackEscaped, "failure callback exception isolated", failureCount);

	ERpcCallError remoteCallError = ERpcCallError::None;
	ERpcResponseCode remoteResponseCode = ERpcResponseCode::Success;
	const FRpcCallStartResult remoteErrorCall = rpcCommon.Call<FAddRpc>(
		{ERpcServerType::Cache, 0, 0},
		std::chrono::seconds(1),
		[](const std::int32_t, std::string) {},
		[&remoteCallError, &remoteResponseCode](const FRpcCallFailure& failure)
		{
			remoteCallError = failure.error;
			remoteResponseCode = failure.remoteResponseCode;
		},
		std::int32_t{2},
		std::int32_t{3});
	Check(remoteErrorCall.accepted, "remote-error test call accepted", failureCount);
	FRpcResponse remoteErrorResponse = rpcCommon.DispatchRequest(transport.sentRequests.back().request);
	remoteErrorResponse.resultCode = ERpcResponseCode::MethodNotFound;
	remoteErrorResponse.payload.clear();
	Check(rpcCommon.ProcessResponse(101, remoteErrorResponse) == ERpcCompletionResult::RemoteError,
		"remote rpc error processed",
		failureCount);
	Check(remoteCallError == ERpcCallError::RemoteError && remoteResponseCode == ERpcResponseCode::MethodNotFound,
		"remote rpc error callback invoked",
		failureCount);

	ERpcCallError disconnectError = ERpcCallError::None;
	const FRpcCallStartResult disconnectCall = rpcCommon.Call<FAddRpc>(
		{ERpcServerType::Cache, 1, 0},
		std::chrono::seconds(1),
		[](const std::int32_t, std::string) {},
		[&disconnectError](const FRpcCallFailure& failure)
		{
			disconnectError = failure.error;
		},
		std::int32_t{3},
		std::int32_t{4});
	Check(disconnectCall.accepted, "disconnect test call accepted", failureCount);
	Check(rpcCommon.FailSession(102, ERpcCallError::Disconnected) == 1, "session pending calls failed", failureCount);
	Check(disconnectError == ERpcCallError::Disconnected, "disconnect callback invoked", failureCount);

	FRpcCommon limitedRpcCommon(sessionRegistry, dispatcher, requestIdGenerator, transport, 9002, 1);
	const auto firstLimitedCall = limitedRpcCommon.Call<FAddRpc>(
		{ERpcServerType::Cache, 0, 0},
		std::chrono::seconds(1),
		[](const std::int32_t, std::string) {},
		[](const FRpcCallFailure&) {},
		std::int32_t{5},
		std::int32_t{6});
	const auto secondLimitedCall = limitedRpcCommon.Call<FAddRpc>(
		{ERpcServerType::Cache, 0, 0},
		std::chrono::seconds(1),
		[](const std::int32_t, std::string) {},
		[](const FRpcCallFailure&) {},
		std::int32_t{7},
		std::int32_t{8});
	Check(firstLimitedCall.accepted, "first limited call accepted", failureCount);
	Check(!secondLimitedCall.accepted && secondLimitedCall.error == ERpcCallError::PendingLimitExceeded,
		"pending call limit enforced",
		failureCount);
	limitedRpcCommon.FailSession(101, ERpcCallError::Disconnected);

	transport.allowSend = false;
	FRpcCommon sendFailureRpcCommon(sessionRegistry, dispatcher, requestIdGenerator, transport, 9003);
	const auto sendFailureNotification =
		sendFailureRpcCommon.Notify<FStateChangedNoti>({ERpcServerType::Cache, 9, 0}, std::uint64_t{9}, std::string("state"));
	Check(!sendFailureNotification.accepted && sendFailureNotification.error == ERpcCallError::SendFailed &&
			  sendFailureNotification.rpcSessionId == 102,
		"notification send failure returned to caller",
		failureCount);
	Check(sendFailureRpcCommon.GetPendingCallCount() == 0, "notification send failure creates no pending call", failureCount);
	const auto sendFailureCall = sendFailureRpcCommon.Call<FAddRpc>(
		{ERpcServerType::Cache, 0, 0},
		std::chrono::seconds(1),
		[](const std::int32_t, std::string) {},
		[](const FRpcCallFailure&) {},
		std::int32_t{9},
		std::int32_t{10});
	Check(!sendFailureCall.accepted && sendFailureCall.error == ERpcCallError::SendFailed, "send failure returned to caller", failureCount);
	Check(sendFailureRpcCommon.GetPendingCallCount() == 0, "send failure leaves no pending call", failureCount);

	const auto unavailableCall = sendFailureRpcCommon.Call<FAddRpc>(
		{ERpcServerType::Login, 0, 0},
		std::chrono::seconds(1),
		[](const std::int32_t, std::string) {},
		[](const FRpcCallFailure&) {},
		std::int32_t{11},
		std::int32_t{12});
	Check(!unavailableCall.accepted && unavailableCall.error == ERpcCallError::SessionUnavailable,
		"missing target session rejected",
		failureCount);

	sessionRegistry.Remove(101);
	Check(!sessionRegistry.Find(101), "removed session is unavailable", failureCount);

	if (failureCount != 0)
	{
		std::cerr << "RpcLib smoke test failed. failures=" << failureCount << '\n';
		return 1;
	}

	std::cout << "[PASS] RpcLib smoke test\n";
	return 0;
}
