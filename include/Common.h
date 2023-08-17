#pragma once
#include <iostream>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <ctime>
#include <cassert>


using std::cout;
using std::endl;

static const size_t MAX_BYTES = 256 * 1024;
static const size_t BUCKET_NUMS = 208;
static const size_t NPAGES = 129;
static const size_t PAGE_SHIFT = 13;

typedef size_t PAGE_ID;

#ifdef _WIN32  // Windows 平台
#define NOMINMAX
#include <windows.h>
inline static void* AllocateMemory(size_t kpage) {
	return VirtualAlloc(NULL, kpage<<13, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}
inline static void FreeMemory(void* address) {
	VirtualFree(address, 0, MEM_RELEASE);
}
#else  // Linux 平台
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
inline static void* AllocateMemory(size_t kapge) {
	void* addr = sbrk(0);
	void* ret = sbrk(kapge<<13);
	if (ret == (void*)-1) {
		ret = nullptr;
	}
	else if (errno == ENOMEM) {
		ret = mmap(addr, kapge << 13, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	}
	return ret;
}
inline static void FreeMemory(void* address) {
	munmap(address,0);
}
#endif



//获取对象内存中前面的指针大小个字节
static inline void*& NextObj(void* obj)
{
	return *static_cast<void**>(obj);
}

// 管理切分好的小对象的自由链表
class FreeList
{
public:
	void Push(void* obj)
	{
		assert(obj);

		// 头插
		NextObj(obj) = _freeList;
		_freeList = obj;

		++_size;
	}
	void PushRange(void* start, void* end, size_t n)
	{
		NextObj(end) = _freeList;
		_freeList = start;

		_size += n;
	}

	void PopRange(void*& start, void*& end, size_t n)
	{
		assert(n >= _size);
		start = _freeList;
		end = start;
		for (size_t i = 0; i < n - 1; i++)
		{
			end = NextObj(end);
		}
		_freeList = NextObj(end);
		NextObj(end) = nullptr;
		_size -= n;
	}

	size_t Size()
	{
		return _size;
	}

	void* Pop()
	{
		assert(_freeList);

		// 头删
		void* obj = _freeList;
		_freeList = NextObj(obj);
		--_size;
		return obj;
	}

	bool Empty()
	{
		return _freeList == nullptr;
	}


	size_t& MaxSize()
	{
		return _maxSize;
	}

private:
	void* _freeList = nullptr;
	size_t _maxSize = 1;
	size_t _size = 0;
};


// 根据对齐映射规则计算相关参数
class Calculator
{
public:
	/*
		申请容量                  对齐大小          哈希桶数
		[1,128]                  8byte对齐         [0,16)
		[128+1,1024]             16byte对齐        [16,72)
		[1024+1, 8*1024]         128byte对齐       [72,128)
		[8*1024+1,64*1024]       1024byte对齐      [128,184)
		[64*1024+1,256*1024]     8*1024byte对齐    [184, 208)
	*/

	//size:申请字节数 alignment:对齐数
	static size_t _GetAlignedSize(size_t size, size_t alignment)
	{
		return ((size + alignment - 1) & ~(alignment - 1));
	}
	static size_t GetAlignedSize(size_t size)
	{
		if (size <= 128)
		{
			return _GetAlignedSize(size, 8);
		}
		else if (size <= 1024)
		{
			return _GetAlignedSize(size, 16);
		}
		else if (size <= 8 * 1024)
		{
			return _GetAlignedSize(size, 128);
		}
		else if (size <= 64 * 1024)
		{
			return _GetAlignedSize(size, 1024);
		}
		else if (size <= 256 * 1024)
		{
			return _GetAlignedSize(size, 8 * 1024);
		}
		else
		{
			return _GetAlignedSize(size, 1 << PAGE_SHIFT);
		}

	}

	static size_t _Index(size_t size, size_t shift_num) // 8=2^3 => shift_num = 3
	{
		return ((size + (static_cast<size_t>(1) << shift_num) - 1) >> shift_num) - 1;
	}

	// 计算对应空间大小映射的自由链表桶号
	static size_t Index(size_t size)
	{
		assert(size <= MAX_BYTES);
		// 每个区间有多少个链
		static int group_array[4] = { 16, 56, 56, 56 };
		if (size <= 128) {
			return _Index(size, 3);
		}
		else if (size <= 1024) {
			return _Index(size - 128, 4) + group_array[0];
		}
		else if (size <= 8 * 1024) {
			return _Index(size - 1024, 7) + group_array[0] + group_array[1];
		}
		else if (size <= 64 * 1024) {
			return _Index(size - 8 * 1024, 10) + group_array[0] + group_array[1]
				+ group_array[2];
		}
		else if (size <= 256 * 1024) {
			return _Index(size - 64 * 1024, 13) + group_array[0] +
				group_array[1] + group_array[2] + group_array[3];
		}
		else {
			assert(false);
		}
		return -1;
	}

	//thread cache 一次从central cache中获取多少个对象
	static size_t NumMoveSize(size_t size)
	{
		assert(size > 0);

		if (size == 0)
			return 0;
		// [2, 512]，一次批量移动多少个对象的(慢启动)上限值
		// 小对象一次批量上限高
		// 大对象一次批量上限低
		size_t num = MAX_BYTES / size;
		if (num < 2)
			num = 2;
		if (num > 512)
			num = 512;
		return num;
	}

	// page cache一次向操作系统申请几个页
	static size_t NumMovePage(size_t size)
	{
		size_t num = NumMoveSize(size);
		size_t npage = num * size;
		npage >>= PAGE_SHIFT;
		if (npage == 0)
			npage = 1;
		return npage;
	}


};

// 管理多个连续页大块内存跨度结构
struct Span
{
	PAGE_ID _pageId = 0; //页号
	size_t _n = 0;       //页数

	Span* _next = nullptr;
	Span* _prev = nullptr;

	size_t _objSize = 0; // 切好的小对象的大小
	size_t _useCount = 0; //切好的小块内存被分配给thread cache的计数
	void* _freeList = nullptr; //切好的小块内存的自由链表

	bool _used = false; // 是否再被使用
};

class SpanList
{
public:
	SpanList()
	{
		_head = new Span;
		_head->_next = _head;
		_head->_prev = _head;
	}

	void Insert(Span* pos, Span* newSpan)
	{
		assert(pos);
		assert(newSpan);

		Span* prev = pos->_prev;

		prev->_next = newSpan;
		newSpan->_prev = prev;
		newSpan->_next = pos;
		pos->_prev = newSpan;

	}

	void PushFront(Span* span)
	{
		Insert(Begin(), span);
	}

	Span* PopFront()
	{
		Span* front = _head->_next;
		Erase(front);

		return front;
	}

	bool Empty()
	{
		return _head->_next == _head;
	}

	Span* Begin()
	{
		return _head->_next;
	}

	Span* End()
	{
		return _head;
	}

	void Erase(Span* pos)
	{
		assert(pos != _head);
		assert(pos);

		Span* prev = pos->_prev;
		Span* next = pos->_next;
		prev->_next = next;
		next->_prev = prev;
	}

private:
	Span* _head;
public:
	std::mutex _mtx; //桶锁：向相同的桶申请内存需要加锁
};