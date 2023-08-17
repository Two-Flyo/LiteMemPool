#pragma once

#include "Common.h"	
#include "PageCache.h"


//单例
class CentralCache
{
public:
	static CentralCache* GetInstance()
	{
		return &_sInst;
	}

	// 获取一个非空的Span
	Span* GetOneSpan(SpanList& list, size_t size);


	// 从中心缓存获取一定数量的对象给thread cache
	size_t FetchRangeObj(void*& start, void*& end, size_t n, size_t bytes);

	//将一些过多的对象释放到span跨度
	void ReleaseListToSpans(void* start, size_t size);

private:
	static CentralCache _sInst;
	CentralCache() {}

	CentralCache(const CentralCache&) = delete;
private:
	SpanList _spanLists[BUCKET_NUMS];
};
