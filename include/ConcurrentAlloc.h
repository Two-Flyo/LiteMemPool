#pragma once

#include "Common.h"
#include "ThreadCache.h"
#include "PageCache.h"
#include "ObjectPool.h"

// TLS thread local storage
static thread_local ThreadCache* pTLSThreadCache = nullptr;

static void* ConcurrentAlloc(size_t size)
{
	if (size > MAX_BYTES)
	{
		size_t alignSize = Calculator::GetAlignedSize(size);
		size_t k = alignSize >> PAGE_SHIFT;

		PageCache::GetInstance()->_pageMtx.lock();
		Span* span = PageCache::GetInstance()->NewSpan(k);
		span->_objSize = size;
		PageCache::GetInstance()->_pageMtx.unlock();
		void* ptr = reinterpret_cast<void*>(span->_pageId << PAGE_SHIFT);
		return ptr;
	}
	else
	{
		// 通过TLS无所得获取自己专属的ThreadCache对象
		if (pTLSThreadCache == nullptr)
		{
			static ObjectPool<ThreadCache> tcPool;
			pTLSThreadCache = tcPool.New();
		}

		// cout << std::this_thread::get_id() << ":" << pTLSThreadCache << endl;

		return pTLSThreadCache->Allocate(size);
	}
}

static void ConcurrentFree(void* ptr)
{
	Span* span = PageCache::GetInstance()->MapObjectToSpan(ptr);
	size_t size = span->_objSize;

	if (size > MAX_BYTES)
	{
		PageCache::GetInstance()->_pageMtx.lock();
		PageCache::GetInstance()->ReleaseSpanToPageCache(span);
		PageCache::GetInstance()->_pageMtx.unlock();
	}
	else
	{
		assert(pTLSThreadCache);
		pTLSThreadCache->Deallocate(ptr, size);
	}


}