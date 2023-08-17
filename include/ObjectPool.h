#pragma once

#include "Common.h"

//定长内存池
template<typename T>
class ObjectPool
{

public:
	T* New()
	{
		T* obj = nullptr;

		// 优先使用归还的小块内存
		if (_freeList)
		{
			void* next = *(static_cast<void**>(_freeList));
			obj = static_cast<T*>(_freeList);
			_freeList = next;
		}
		else
		{
			// 剩余内存不够一个对象的大小时，则重新开辟空间
			if (_remainBytes < sizeof(T))
			{
				_remainBytes = 128 * 1024;
				//使用WindowsAPI去申请内存
				_memory = static_cast<char*>(AllocateMemory(_remainBytes>>13));
				 //_memory = static_cast<char*>(malloc(_remainBytes));
				if (_memory == nullptr)
				{
					throw std::bad_alloc();
				}
			}

			obj = reinterpret_cast<T*>(_memory);

			//适配所申请对象的空间小于一个指针大小的情况
			size_t objSize = sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T);
			_memory += objSize;
			_remainBytes -= objSize;

		}

		//显示调用T的构造函数初始化我们分配的内存空间
		new(obj)T;

		return obj;
	}

	void Delete(T* obj)
	{
		//显示调用析构函数清理对象
		obj->~T();
		// 使用链表的头插，将归还的小块内存连接起来
		//使用void**/int**/等二级指针去自适配32/64位环境的指针大小
		*reinterpret_cast<void**>(obj) = _freeList;
		_freeList = obj;
	}

private:
	char* _memory = nullptr;   //指向我们提前申请的大块内存
	void* _freeList = nullptr; //指向申请后归还的小块内存形成的链表的头指针 
	size_t _remainBytes = 0;   //大块内存的剩余空间
};

struct TreeNode
{
	int _val;
	TreeNode* _left;
	TreeNode* _right;
	TreeNode()
		:_val(0)
		, _left(nullptr)
		, _right(nullptr)
	{}
};

