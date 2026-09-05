#pragma once

namespace RpcLib
{
	// One FRpcCommon belongs to one Content Instance. ProcessResponse and ProcessTimeouts run on that Content Thread.
	class FRpcCommon final
	{
	public:
		FRpcCommon(Session::FRpcSessionRegistry& sessionRegistry,
			Dispatch::FRpcMethodDispatcher& methodDispatcher,
			Call::FRpcRequestIdGenerator& requestIdGenerator,
			Transport::IRpcTransport& transport,
			std::uint64_t originContentInstanceId,
			std::size_t maxPendingCallCount = 1024) noexcept;

		FRpcCommon(const FRpcCommon&) = delete;
		FRpcCommon& operator=(const FRpcCommon&) = delete;
		FRpcCommon(FRpcCommon&&) = delete;
		FRpcCommon& operator=(FRpcCommon&&) = delete;

		template <typename TRpcMethod, typename THandler>
		bool Register(
			THandler&& handler)
		{
			return m_methodDispatcher.Register<TRpcMethod>(std::forward<THandler>(handler));
		}

		template <typename TNotification, typename THandler>
		bool RegisterNotification(
			THandler&& handler)
		{
			return m_methodDispatcher.RegisterNotification<TNotification>(std::forward<THandler>(handler));
		}

		template <typename TRpcMethod, typename TSuccess, typename TFailure, typename... TArguments>
		Protocol::FRpcCallStartResult Call(
			const Protocol::FRpcTarget& target,
			const std::chrono::milliseconds timeout,
			TSuccess&& onSuccess,
			TFailure&& onFailure,
			TArguments&&... arguments)
		{
			static_assert(TRpcMethod::kServiceId != 0, "RPC service id must not be zero.");
			static_assert(TRpcMethod::kMethodId != 0, "RPC method id must not be zero.");

			using FExpectedArguments = typename TRpcMethod::FRequestArguments;
			using FProvidedArguments = std::tuple<std::remove_cvref_t<TArguments>...>;
			static_assert(std::same_as<FExpectedArguments, FProvidedArguments>, "RPC request arguments do not match the method signature.");

			if (m_originContentInstanceId == 0 || target.serverType == Protocol::ERpcServerType::Unknown || timeout.count() <= 0)
			{
				return {false, 0, Protocol::ERpcCallError::InvalidArgument};
			}
			if (!HasMatchingRoutingKey<TRpcMethod>(target.routingKey, arguments...))
			{
				return {false, 0, Protocol::ERpcCallError::InvalidArgument};
			}

			const std::shared_ptr<Session::FRpcSession> rpcSession = m_sessionRegistry.SelectReady(target);
			if (rpcSession == nullptr)
			{
				return {false, 0, Protocol::ERpcCallError::SessionUnavailable};
			}

			Protocol::FRpcRequest request;
			request.requestId = m_requestIdGenerator.Next();
			request.serviceId = TRpcMethod::kServiceId;
			request.methodId = TRpcMethod::kMethodId;
			request.routingKey = target.routingKey;
			request.originContentInstanceId = m_originContentInstanceId;

			try
			{
				NetworkLib::Packet::Serialization::FPacketWriter writer;
				const FProvidedArguments requestArguments(std::forward<TArguments>(arguments)...);
				if (!Protocol::WriteRpcArguments(writer, requestArguments) || writer.GetBodySize() > Protocol::kMaxRpcPayloadBytes)
				{
					return {false, request.requestId, Protocol::ERpcCallError::SerializationFailed};
				}

				request.payload = writer.MoveBuffer();
			}
			catch (...)
			{
				return {false, request.requestId, Protocol::ERpcCallError::SerializationFailed};
			}

			using FResponseArguments = typename TRpcMethod::FResponseArguments;
			Call::FRpcPendingCall pendingCall;
			pendingCall.requestId = request.requestId;
			pendingCall.rpcSessionId = rpcSession->GetNetworkSessionId();
			pendingCall.serviceId = request.serviceId;
			pendingCall.methodId = request.methodId;
			pendingCall.deadline = std::chrono::steady_clock::now() + timeout;
			pendingCall.onResponse = [successCallback = std::forward<TSuccess>(onSuccess)](const std::span<const char> payload) mutable
			{
				NetworkLib::Packet::Serialization::FPacketReader reader(payload.data(), payload.size());
				FResponseArguments responseArguments{};
				if (!Protocol::ReadRpcArguments(reader, responseArguments) || !reader.IsAtEnd())
				{
					return false;
				}

				try
				{
					std::apply(
						[&successCallback](auto&&... values)
						{
							std::invoke(successCallback, std::move(values)...);
						},
						std::move(responseArguments));
				}
				catch (...)
				{
					return false;
				}

				return true;
			};
			pendingCall.onFailure = std::forward<TFailure>(onFailure);

			if (!m_pendingCallManager.TryAdd(std::move(pendingCall)))
			{
				return {false, request.requestId, Protocol::ERpcCallError::PendingLimitExceeded};
			}

			if (!m_transport.SendRequest(rpcSession->GetNetworkSessionId(), request))
			{
				m_pendingCallManager.Cancel(request.requestId);
				return {false, request.requestId, Protocol::ERpcCallError::SendFailed};
			}

			return {true, request.requestId, Protocol::ERpcCallError::None};
		}

		template <typename TNotification, typename... TArguments>
		Protocol::FRpcNotificationSendResult Notify(
			const Protocol::FRpcTarget& target,
			TArguments&&... arguments)
		{
			static_assert(TNotification::kServiceId != 0, "RPC service id must not be zero.");
			static_assert(TNotification::kMethodId != 0, "RPC method id must not be zero.");

			using FExpectedArguments = typename TNotification::FArguments;
			using FProvidedArguments = std::tuple<std::remove_cvref_t<TArguments>...>;
			static_assert(
				std::same_as<FExpectedArguments, FProvidedArguments>, "RPC notification arguments do not match the method signature.");

			if (m_originContentInstanceId == 0 || target.serverType == Protocol::ERpcServerType::Unknown)
			{
				return {false, 0, Protocol::ERpcCallError::InvalidArgument};
			}
			if (!HasMatchingRoutingKey<TNotification>(target.routingKey, arguments...))
			{
				return {false, 0, Protocol::ERpcCallError::InvalidArgument};
			}

			const std::shared_ptr<Session::FRpcSession> rpcSession = m_sessionRegistry.SelectReady(target);
			if (rpcSession == nullptr)
			{
				return {false, 0, Protocol::ERpcCallError::SessionUnavailable};
			}

			Protocol::FRpcNotification notification;
			notification.serviceId = TNotification::kServiceId;
			notification.methodId = TNotification::kMethodId;
			notification.routingKey = target.routingKey;
			notification.originContentInstanceId = m_originContentInstanceId;

			try
			{
				NetworkLib::Packet::Serialization::FPacketWriter writer;
				const FProvidedArguments notificationArguments(std::forward<TArguments>(arguments)...);
				if (!Protocol::WriteRpcArguments(writer, notificationArguments) || writer.GetBodySize() > Protocol::kMaxRpcPayloadBytes)
				{
					return {false, rpcSession->GetNetworkSessionId(), Protocol::ERpcCallError::SerializationFailed};
				}

				notification.payload = writer.MoveBuffer();
			}
			catch (...)
			{
				return {false, rpcSession->GetNetworkSessionId(), Protocol::ERpcCallError::SerializationFailed};
			}

			if (!m_transport.SendNotification(rpcSession->GetNetworkSessionId(), notification))
			{
				return {false, rpcSession->GetNetworkSessionId(), Protocol::ERpcCallError::SendFailed};
			}

			return {true, rpcSession->GetNetworkSessionId(), Protocol::ERpcCallError::None};
		}

		Protocol::FRpcResponse DispatchRequest(std::uint64_t rpcSessionId, const Protocol::FRpcRequest& request) const;
		Protocol::FRpcResponse DispatchRequest(const Protocol::FRpcRequest& request) const;
		Protocol::ERpcNotificationDispatchResult DispatchNotification(std::uint64_t rpcSessionId,
			const Protocol::FRpcNotification& notification) const;
		Protocol::ERpcNotificationDispatchResult DispatchNotification(const Protocol::FRpcNotification& notification) const;
		Protocol::ERpcCompletionResult ProcessResponse(std::uint64_t rpcSessionId, const Protocol::FRpcResponse& response);
		std::size_t ProcessTimeouts(std::chrono::steady_clock::time_point now);
		std::size_t FailSession(std::uint64_t rpcSessionId, Protocol::ERpcCallError error);
		std::size_t GetPendingCallCount() const noexcept;
		std::uint64_t GetOriginContentInstanceId() const noexcept;

	private:
		template <typename TContract, typename... TArguments>
		static bool HasMatchingRoutingKey(
			const std::uint64_t routingKey,
			const TArguments&... arguments) noexcept
		{
			static_assert(
				requires {
					TContract::kHasRoutingKey;
					TContract::kRoutingKeyArgumentIndex;
				}, "RPC contract must declare routing-key metadata.");
			if constexpr (TContract::kHasRoutingKey)
			{
				static_assert(TContract::kRoutingKeyArgumentIndex < sizeof...(TArguments), "RPC routing-key index is out of range.");
				const auto argumentReferences = std::forward_as_tuple(arguments...);
				using FRoutingKey = std::remove_cvref_t<decltype(std::get<TContract::kRoutingKeyArgumentIndex>(argumentReferences))>;
				static_assert(std::is_integral_v<FRoutingKey> && std::is_unsigned_v<FRoutingKey> && !std::same_as<FRoutingKey, bool>,
					"RPC routing-key argument must be an unsigned integer.");
				return routingKey == static_cast<std::uint64_t>(std::get<TContract::kRoutingKeyArgumentIndex>(argumentReferences));
			}

			return true;
		}

		Session::FRpcSessionRegistry& m_sessionRegistry;
		Dispatch::FRpcMethodDispatcher& m_methodDispatcher;
		Call::FRpcRequestIdGenerator& m_requestIdGenerator;
		Transport::IRpcTransport& m_transport;
		std::uint64_t m_originContentInstanceId = 0;
		Call::FRpcPendingCallManager m_pendingCallManager;
	};
}
