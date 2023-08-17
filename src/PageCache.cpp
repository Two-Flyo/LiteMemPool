#include "../include/PageCache.h"
PageCache PageCache::_pInst;


//获取一个K页的span
Span* PageCache::NewSpan(size_t k)
{
	assert(k > 0);

	// 大于128K的直接向堆申请
	if (k > NPAGES - 1)
	{
		void* ptr = AllocateMemory(k);
		//Span* span = new Span;
		Span* span = _spanPool.New();
		span->_pageId = reinterpret_cast<PAGE_ID>(ptr) >> PAGE_SHIFT;
		span->_n = k;
		_idSpanMap[span->_pageId] = span;

		return span;
	}

	// 先检查K页对应的桶有无span
	if (!_spanLists[k].Empty())
	{
		Span* kspan = _spanLists[k].PopFront();
		for (PAGE_ID i = 0; i < kspan->_n; i++)
		{
			_idSpanMap[kspan->_pageId + i] = kspan;
		}
		return kspan;
	}
	//检查一下后面的桶有无span,如果有可以切分
	for (size_t i = k + 1; i < NPAGES; i++)
	{
		if (!_spanLists[i].Empty())
		{
			Span* nspan = _spanLists[i].PopFront();
			Span* kspan = _spanPool.New();
			//在nspan的头部切一个K页下来
			kspan->_pageId = nspan->_pageId;
			kspan->_n = k;
			nspan->_pageId += k;
			nspan->_n -= k;

			//把剩下的nspan挂到对应位置
			_spanLists[nspan->_n].PushFront(nspan);

			// 存储nspan的首和尾的页号与nspan映射，方便page cache 回收内存时进行合并查找
			_idSpanMap[nspan->_pageId] = nspan;
			_idSpanMap[nspan->_pageId + nspan->_n - 1] = nspan;

			// 建立id和span的映射，方便central cache回收小块内存时，查找对应的span
			for (PAGE_ID i = 0; i < kspan->_n; i++)
			{
				_idSpanMap[kspan->_pageId + i] = kspan;
			}

			return kspan;
		}
	}
	//程序执行到这里，说明后面已经没有了大页的span了，需要找堆去申请128页的span了
	Span* bigSpan = _spanPool.New();
	void* ptr = AllocateMemory(NPAGES - 1);
	bigSpan->_pageId = reinterpret_cast<PAGE_ID>(ptr) >> PAGE_SHIFT;
	bigSpan->_n = NPAGES - 1;

	_spanLists[bigSpan->_n].PushFront(bigSpan);

	return NewSpan(k);
}

Span* PageCache::MapObjectToSpan(void* obj)
{
	PAGE_ID id = (reinterpret_cast<PAGE_ID>(obj)) >> PAGE_SHIFT;

	std::unique_lock<std::mutex> lock(_pageMtx);
	auto ret = _idSpanMap.find(id);
	if (ret != _idSpanMap.end())
	{
		return ret->second;
	}
	else
	{
		assert(false);
		return nullptr;
	}
}

void PageCache::ReleaseSpanToPageCache(Span* span)
{
	//大于128页的直接还给堆
	if (span->_n > NPAGES - 1)
	{
		void* ptr = reinterpret_cast<void*>(span->_pageId  << PAGE_SHIFT);
		FreeMemory(ptr);
		_spanPool.Delete(span);

		return;
	}

	// 对span 前后的页尝试进行合并，缓解内存碎片问题
	while (true)
	{
		PAGE_ID prevId = span->_pageId - 1;
		auto ret = _idSpanMap.find(prevId);
		// 前面的页号没有，不合并了	
		if (ret == _idSpanMap.end())
			break;
		// 前面相邻页的span在使用，不合并了
		Span* prevSpan = ret->second;
		if (prevSpan->_used == true)
			break;
		// 合并超出了128页，没法管理，不合并
		if (prevSpan->_n + span->_n > NPAGES - 1)
			break;
		span->_pageId = prevSpan->_pageId;
		span->_n += prevSpan->_n;

		_spanLists[prevSpan->_n].Erase(prevSpan);
		_spanPool.Delete(prevSpan);
	}

	// 向后合并
	while (true)
	{
		PAGE_ID nextId = span->_pageId + span->_n;
		auto ret = _idSpanMap.find(nextId);
		if (ret == _idSpanMap.end())
			break;
		Span* nextSpan = ret->second;
		if (nextSpan->_used == true)
			break;
		if (nextSpan->_n + span->_n > NPAGES - 1)
			break;
		
		span->_n += nextSpan->_n;

		_spanLists[nextSpan->_n].Erase(nextSpan);
		_spanPool.Delete(nextSpan);
	}

	_spanLists[span->_n].PushFront(span);
	span->_used = false;
	_idSpanMap[span->_pageId] = span;
	_idSpanMap[span->_pageId + span->_n - 1] = span;
}