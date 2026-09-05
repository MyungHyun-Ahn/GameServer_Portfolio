#include "RpcLibPch.h"

#include "Call/FRpcRequestIdGenerator.h"

namespace RpcLib::Call
{
	Protocol::FRpcRequestId FRpcRequestIdGenerator::Next() noexcept
	{
		Protocol::FRpcRequestId requestId = m_nextRequestId.fetch_add(1, std::memory_order_relaxed);
		if (requestId == 0)
		{
			requestId = m_nextRequestId.fetch_add(1, std::memory_order_relaxed);
		}

		return requestId;
	}
}
