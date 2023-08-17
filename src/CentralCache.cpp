#include "../include/CentralCache.h"

CentralCache CentralCache::_sInst;

size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size)
{
	size_t index = Calculator::Index(size);
	_spanLists[index]._mtx.lock();
			
	Span* span = GetOneSpan(_spanLists[index], size);
	assert(span);
	assert(span->_freeList);


	// 从span中获取batchNum个对象，如果不够，有多少拿多少
	start = span->_freeList;
	end = start;
	size_t actualNum = 1;
	for(size_t i = 0; i < batchNum - 1 && NextObj(end) != nullptr; i++)
	{
		end = NextObj(end);
		actualNum++;
	}
	span->_freeList = NextObj(end);
	NextObj(end) = nullptr;
	span->_useCount += actualNum;
	_spanLists[index]._mtx.unlock();

	return actualNum;
}

Span* CentralCache::GetOneSpan(SpanList& list, size_t size)
{
	// 查看当前的spanList中是否还有未分配对象的span
	Span* it = list.Begin();
	while (it != list.End())
	{
		if (it->_freeList != nullptr)
			return it;
		else
			it = it->_next;
	}

	// 先把central cache的桶锁释放，这样如果其他线程施法内存，不会阻塞
	list._mtx.unlock();	

	// 走到这里说明没有空闲的span,只能找page cache
	PageCache::GetInstance()->_pageMtx.lock();
	Span* span = PageCache::GetInstance()->NewSpan(Calculator::NumMovePage(size));
	span->_used = true;
	span->_objSize = size;
	PageCache::GetInstance()->_pageMtx.unlock();

	// 对获取的span进行切分，不需要加锁，因为其他线程访问不到这个span


	//计算大块内存span的起始地址和字节数大小
	char* start = reinterpret_cast<char*>(span->_pageId << PAGE_SHIFT);
	size_t bytes = span->_n << PAGE_SHIFT;
	char* end = start + bytes;
	// 把大块内存切成自由链表链接起来
	//1.先切一块下来做链表的头结点，方便尾插
	span->_freeList = start;
	start += size;
	void* tail = span->_freeList;
	while (start < end)
	{
		NextObj(tail) = start;
		tail = start;
		start += size;
	}
	NextObj(tail) = nullptr;

	//切好span以后，需要把span挂到桶里面的时候，再加锁
	list._mtx.lock();
	list.PushFront(span);

	return span;
}



void CentralCache::ReleaseListToSpans(void* start, size_t size)
{
	size_t index = Calculator::Index(size);
	_spanLists[index]._mtx.lock();

	while (start)
	{
		void* next = NextObj(start);
		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);
		NextObj(start) = span->_freeList;
		span->_freeList = start;
		span->_useCount--;
		//cout << std::this_thread::get_id() << endl;
		// 说明span切分好的所有小块内存都回来了，这个span就可以再回收给page cache
		// page cache可以再尝试去做前后页的合并了
		if (span->_useCount == 0)
		{
			// cout << "Test->" <<std::this_thread::get_id() << endl;
			_spanLists[index].Erase(span);	
			span->_freeList = nullptr;
			span->_next = nullptr;
			span->_prev = nullptr;	
			
			// 释放span给page cache时，用page cache的锁就可以了
			// 这时把桶锁解掉
			_spanLists[index]._mtx.unlock();
			PageCache::GetInstance()->_pageMtx.lock();
			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
			PageCache::GetInstance()->_pageMtx.unlock();

			_spanLists[index]._mtx.lock();
		}
		start = next;
	}

	
	_spanLists[index]._mtx.unlock();
	}