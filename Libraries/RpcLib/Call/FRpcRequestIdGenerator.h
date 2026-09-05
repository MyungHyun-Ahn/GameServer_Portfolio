#pragma once

namespace RpcLib::Call
{
	class FRpcRequestIdGenerator final
	{
	public:
		Protocol::FRpcRequestId Next() noexcept;

	private:
		std::atomic<Protocol::FRpcRequestId> m_nextRequestId = 1;
	};
}
