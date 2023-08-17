#pragma once

#include "Common.h"
#include "ObjectPool.h"


class PageCache
{
public:
	static PageCache* GetInstance()
	{
		return &_pInst;
	}

	// 获取从对象到span的映射
	Span* MapObjectToSpan(void* obj);

	//释放空闲span回到Page cache, 并合并相邻的span
	void ReleaseSpanToPageCache(Span* span);


	// 获取一个k页的span
	Span* NewSpan(size_t k);
	std::mutex _pageMtx;

private:
	PageCache() {}
	PageCache(const PageCache&) = delete;

	ObjectPool<Span> _spanPool;
	SpanList _spanLists[NPAGES];
	static PageCache _pInst;
	std::unordered_map<PAGE_ID, Span*> _idSpanMap;
};
