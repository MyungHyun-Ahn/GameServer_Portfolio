#pragma once

namespace RpcLib::Dispatch
{
	template <typename TRpcMethod> class TRpcReply final
	{
	public:
		using FResponseArguments = typename TRpcMethod::FResponseArguments;

		template <typename... TArguments>
		bool Send(
			TArguments&&... arguments)
		{
			using FProvidedArguments = std::tuple<std::remove_cvref_t<TArguments>...>;
			static_assert(
				std::same_as<FProvidedArguments, FResponseArguments>, "RPC response arguments do not match the method signature.");

			if (m_arguments.has_value())
			{
				return false;
			}

			m_arguments.emplace(std::forward<TArguments>(arguments)...);
			return true;
		}

		bool HasResponse() const noexcept
		{
			return m_arguments.has_value();
		}

		const FResponseArguments& GetArguments() const
		{
			assert(m_arguments.has_value());
			return *m_arguments;
		}

	private:
		std::optional<FResponseArguments> m_arguments;
	};
}
