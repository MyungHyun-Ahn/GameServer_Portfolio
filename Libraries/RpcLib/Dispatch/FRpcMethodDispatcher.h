#pragma once

namespace RpcLib::Dispatch
{
	using FRpcRawMethodHandler =
		std::function<Protocol::ERpcResponseCode(const FRpcCallContext&, std::span<const char>, std::vector<char>&)>;
	using FRpcRawNotificationHandler =
		std::function<Protocol::ERpcNotificationDispatchResult(const FRpcCallContext&, std::span<const char>)>;

	class FRpcMethodDispatcher final
	{
	public:
		template <typename TRpcMethod, typename THandler>
		bool Register(
			THandler&& handler)
		{
			static_assert(TRpcMethod::kServiceId != 0, "RPC service id must not be zero.");
			static_assert(TRpcMethod::kMethodId != 0, "RPC method id must not be zero.");

			using FRequestArguments = typename TRpcMethod::FRequestArguments;
			FRpcRawMethodHandler rawHandler = [registeredHandler = std::forward<THandler>(handler)](const FRpcCallContext& context,
												  const std::span<const char> payload,
												  std::vector<char>& outPayload) mutable -> Protocol::ERpcResponseCode
			{
				NetworkLib::Packet::Serialization::FPacketReader reader(payload.data(), payload.size());
				FRequestArguments arguments{};
				if (!Protocol::ReadRpcArguments(reader, arguments) || !reader.IsAtEnd())
				{
					return Protocol::ERpcResponseCode::InvalidPayload;
				}
				if (!HasMatchingRoutingKey<TRpcMethod>(context.routingKey, arguments))
				{
					return Protocol::ERpcResponseCode::InvalidPayload;
				}

				TRpcReply<TRpcMethod> reply;
				try
				{
					std::apply(
						[&registeredHandler, &context, &reply](auto&&... values)
						{
							if constexpr (std::invocable<THandler&, const FRpcCallContext&, TRpcReply<TRpcMethod>&, decltype(values)...>)
							{
								std::invoke(registeredHandler, context, reply, std::move(values)...);
							}
							else
							{
								static_assert(std::invocable<THandler&, TRpcReply<TRpcMethod>&, decltype(values)...>,
									"RPC handler must be invocable as (const FRpcCallContext&, TRpcReply&, args...) or (TRpcReply&, "
									"args...).");
								std::invoke(registeredHandler, reply, std::move(values)...);
							}
						},
						std::move(arguments));
				}
				catch (...)
				{
					return Protocol::ERpcResponseCode::HandlerException;
				}

				if (!reply.HasResponse())
				{
					return Protocol::ERpcResponseCode::HandlerDidNotReply;
				}

				try
				{
					NetworkLib::Packet::Serialization::FPacketWriter writer;
					if (!Protocol::WriteRpcArguments(writer, reply.GetArguments()) || writer.GetBodySize() > Protocol::kMaxRpcPayloadBytes)
					{
						return Protocol::ERpcResponseCode::InvalidPayload;
					}

					outPayload = writer.MoveBuffer();
				}
				catch (...)
				{
					return Protocol::ERpcResponseCode::HandlerException;
				}

				return Protocol::ERpcResponseCode::Success;
			};

			return RegisterRaw(TRpcMethod::kServiceId, TRpcMethod::kMethodId, std::move(rawHandler));
		}

		template <typename TNotification, typename THandler>
		bool RegisterNotification(
			THandler&& handler)
		{
			static_assert(TNotification::kServiceId != 0, "RPC service id must not be zero.");
			static_assert(TNotification::kMethodId != 0, "RPC method id must not be zero.");

			using FArguments = typename TNotification::FArguments;
			FRpcRawNotificationHandler rawHandler =
				[registeredHandler = std::forward<THandler>(handler)](
					const FRpcCallContext& context, const std::span<const char> payload) mutable -> Protocol::ERpcNotificationDispatchResult
			{
				NetworkLib::Packet::Serialization::FPacketReader reader(payload.data(), payload.size());
				FArguments arguments{};
				if (!Protocol::ReadRpcArguments(reader, arguments) || !reader.IsAtEnd())
				{
					return Protocol::ERpcNotificationDispatchResult::InvalidPayload;
				}
				if (!HasMatchingRoutingKey<TNotification>(context.routingKey, arguments))
				{
					return Protocol::ERpcNotificationDispatchResult::InvalidPayload;
				}

				try
				{
					std::apply(
						[&registeredHandler, &context](auto&&... values)
						{
							if constexpr (std::invocable<THandler&, const FRpcCallContext&, decltype(values)...>)
							{
								std::invoke(registeredHandler, context, std::move(values)...);
							}
							else
							{
								static_assert(std::invocable<THandler&, decltype(values)...>,
									"RPC notification handler must be invocable as (const FRpcCallContext&, args...) or "
									"(args...).");
								std::invoke(registeredHandler, std::move(values)...);
							}
						},
						std::move(arguments));
				}
				catch (...)
				{
					return Protocol::ERpcNotificationDispatchResult::HandlerException;
				}

				return Protocol::ERpcNotificationDispatchResult::Dispatched;
			};

			return RegisterRawNotification(TNotification::kServiceId, TNotification::kMethodId, std::move(rawHandler));
		}

		Protocol::FRpcResponse Dispatch(const FRpcCallContext& context, const Protocol::FRpcRequest& request) const;
		Protocol::FRpcResponse Dispatch(const Protocol::FRpcRequest& request) const;
		Protocol::ERpcNotificationDispatchResult DispatchNotification(const FRpcCallContext& context,
			const Protocol::FRpcNotification& notification) const;
		Protocol::ERpcNotificationDispatchResult DispatchNotification(const Protocol::FRpcNotification& notification) const;
		std::size_t GetRegisteredMethodCount() const;
		std::size_t GetRegisteredNotificationCount() const;

	private:
		bool RegisterRaw(Protocol::FRpcServiceId serviceId, Protocol::FRpcMethodId methodId, FRpcRawMethodHandler handler);
		bool RegisterRawNotification(Protocol::FRpcServiceId serviceId,
			Protocol::FRpcMethodId methodId,
			FRpcRawNotificationHandler handler);

		template <typename TContract, typename TArguments>
		static bool HasMatchingRoutingKey(
			const std::uint64_t routingKey,
			const TArguments& arguments) noexcept
		{
			static_assert(
				requires {
					TContract::kHasRoutingKey;
					TContract::kRoutingKeyArgumentIndex;
				}, "RPC contract must declare routing-key metadata.");
			if constexpr (TContract::kHasRoutingKey)
			{
				static_assert(
					TContract::kRoutingKeyArgumentIndex < std::tuple_size_v<TArguments>, "RPC routing-key index is out of range.");
				using FRoutingKey = std::remove_cvref_t<decltype(std::get<TContract::kRoutingKeyArgumentIndex>(arguments))>;
				static_assert(std::is_integral_v<FRoutingKey> && std::is_unsigned_v<FRoutingKey> && !std::same_as<FRoutingKey, bool>,
					"RPC routing-key argument must be an unsigned integer.");
				return routingKey == static_cast<std::uint64_t>(std::get<TContract::kRoutingKeyArgumentIndex>(arguments));
			}

			return true;
		}

		static std::uint64_t MakeMethodKey(Protocol::FRpcServiceId serviceId, Protocol::FRpcMethodId methodId) noexcept;

	private:
		mutable std::shared_mutex m_lock;
		std::unordered_map<std::uint64_t, std::shared_ptr<FRpcRawMethodHandler>> m_handlers;
		std::unordered_map<std::uint64_t, std::shared_ptr<FRpcRawNotificationHandler>> m_notificationHandlers;
	};
}
