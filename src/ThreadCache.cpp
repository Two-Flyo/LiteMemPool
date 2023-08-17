#include "../include/ThreadCache.h"
#include "../include/CentralCache.h"



void* ThreadCache::Allocate(size_t size)
{
	assert(size <= MAX_BYTES);
	size_t alignSize = Calculator::GetAlignedSize(size); //计算对齐后的大小
	size_t index = Calculator::Index(size);              //计算该大小对应的哈希桶号

	//该空间大小对应的哈希桶不为空
	if (!_freeLists[index].Empty())
	{
		return _freeLists[index].Pop();
	}
	else //对应哈希桶为空，向下一层申请
	{
		return AllocFromCentralCache(index, alignSize);
	}
}

void ThreadCache::Deallocate(void* ptr, size_t size)
{
	assert(size <= MAX_BYTES);
	assert(ptr);

	//找出映射的自由链表桶进行头插
	size_t index = Calculator::Index(size);
	_freeLists[index].Push(ptr);

	// 当链表长度大于一次批量申请的内存时就开始还一段list给central cache
	if (_freeLists[index].Size() >= _freeLists[index].MaxSize())
	{
		ReleaseTooLong(_freeLists[index], size);
	}
}

void* ThreadCache::AllocFromCentralCache(size_t index, size_t size)
{
	// 慢开始反馈调节算法
	// 1.最开始不会一次向central cache一次批量要太多，因为要太多了可能用不完
	// 2.如果你不断有size大小的内存需求，那么batchNum就会不断增长，直到上限
	// 3.size越大，一次向central cache要的batchNum就越小
	// 4.size越小，一次向central cache要的batchNum就越大

	size_t batchNum = std::min(_freeLists[index].MaxSize(), Calculator::NumMoveSize(size));	
	if (_freeLists[index].MaxSize() == batchNum)
	{
		_freeLists[index].MaxSize() += 1;
	}

	void* start = nullptr;
	void* end = nullptr;
	size_t actualNum = CentralCache::GetInstance()->FetchRangeObj(start, end, batchNum, size);
	assert(actualNum > 0);

	if (actualNum == 1)
		assert(start == end);
	else
		_freeLists[index].PushRange(NextObj(start), end, actualNum-1);
	return start;
}

void ThreadCache::ReleaseTooLong(FreeList& list, size_t size)
{
	void* start = nullptr;
	void* end = nullptr;
	list.PopRange(start, end,list.MaxSize());

	CentralCache::GetInstance()->ReleaseListToSpans(start, size);
}

