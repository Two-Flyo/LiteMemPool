#pragma once

#include "Common.h"	


class ThreadCache
{
public:
	//申请内存对象
	void* Allocate(size_t size);

	//释放内存对象
	void Deallocate(void* ptr, size_t size);

	//从中心缓存获取对象
	void* AllocFromCentralCache(size_t index, size_t size);
	
	//自由链表链接过长时，回收内存到central cache
	void ReleaseTooLong(FreeList& list, size_t size);

private:
	FreeList _freeLists[BUCKET_NUMS];
};



