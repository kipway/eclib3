/*!
\file ec_alloctor.h
\author	jiangyong
\email  kipway@outlook.com
\update 2022.4.29

memory
	eclib memory allocator for ec::string, ec::hashmap, and other small objects etc.

eclib 3.0 Copyright (c) 2017-2022, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/

#pragma once
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include "ec_mutex.h"

#ifndef EC_OBJ_TNY_SIZE
#define EC_OBJ_TNY_SIZE 64
#endif

#ifndef EC_OBJ_SML_SIZE
#define EC_OBJ_SML_SIZE 128
#endif

#ifndef EC_OBJ_MID_SIZE
#define EC_OBJ_MID_SIZE 256
#endif

#ifndef EC_OBJ_BIG_SIZE
#define EC_OBJ_BIG_SIZE 512
#endif

#ifndef EC_ALLOCTOR_HEAP_SIZE
#if defined(_MEM_TINY) // < 256M
#define EC_ALLOCTOR_HEAP_SIZE (256 * 1024)  // 256K heap size
#elif defined(_MEM_SML) // < 1G
#define EC_ALLOCTOR_HEAP_SIZE (1024 * 1024) // 1M heap size
#else
#define EC_ALLOCTOR_HEAP_SIZE (2 * 1024 * 1024) // 2M heap size
#endif
#endif

#define DECLARE_EC_ALLOCTOR ec::selfalloctor g_ec_alloctor_self;\
ec::selfalloctor* ec::p_ec_alloctor_self = &g_ec_alloctor_self;\
ec::allocator g_ec_allocator(\
	EC_OBJ_TNY_SIZE,(EC_ALLOCTOR_HEAP_SIZE/EC_OBJ_TNY_SIZE),\
	EC_OBJ_SML_SIZE,(EC_ALLOCTOR_HEAP_SIZE/EC_OBJ_SML_SIZE),\
	EC_OBJ_MID_SIZE,(EC_ALLOCTOR_HEAP_SIZE/EC_OBJ_MID_SIZE),\
	EC_OBJ_BIG_SIZE,(EC_ALLOCTOR_HEAP_SIZE/EC_OBJ_BIG_SIZE));\
ec::allocator* ec::p_ec_allocator = &g_ec_allocator;

#ifndef EC_ALLOCTOR_GC_MINHEAPS // start garbage collection min number of heaps
#if defined(_MEM_TINY) // < 256M
#define EC_ALLOCTOR_GC_MINHEAPS 2
#elif defined(_MEM_SML) // < 1G
#define EC_ALLOCTOR_GC_MINHEAPS 4
#else
#define EC_ALLOCTOR_GC_MINHEAPS 8
#endif
#endif

#define EC_ALLOCTOR_ALIGN 8u
namespace ec {
	class null_lock final // null lock for map
	{
	public:
		void lock() { }
		void unlock() {}
	};

	template <size_t _SIZE, size_t _NUM, class LOCK = null_lock>
	class tinyalloc_ final // self-use memory allocator for memheap_,blk_alloctor etc.
	{
	protected:
		struct t_blk
		{
			struct t_blk* pnext;
		};
		int _numfree;
		t_blk* _phead;
		char _mem[_SIZE * _NUM];
		LOCK _lck;

	public:

		tinyalloc_()
		{
			t_blk* pblk;
			_phead = (t_blk*)&_mem;
			for (auto i = 0u; i < _NUM; i++) {
				pblk = (t_blk*)&_mem[i * _SIZE];
				if (i + 1 == _NUM)
					pblk->pnext = nullptr;
				else
					pblk->pnext = (t_blk*)&_mem[i * _SIZE + _SIZE];
			}
			_numfree = (int)_NUM;
		}

		~tinyalloc_() {
		}

		inline int numfree() {
			return _numfree;
		}

		void* malloc_(size_t size)
		{
			if (size > _SIZE)
				return ::malloc(size);
			safe_lock<LOCK> lck(&_lck);
			if (!_phead)
				return ::malloc(_SIZE);
			t_blk* pret = _phead;
			_phead = _phead->pnext;
			--_numfree;
			return pret;
		}

		void free_(void* pf)
		{
			safe_lock<LOCK> lck(&_lck);
			if (!pf)
				return;
			char* p = (char*)pf;
			if (p < &_mem[0] || p >= &_mem[_SIZE * _NUM - 1]) {
				::free(pf);
				return;
			}
			t_blk* pblk = (t_blk*)pf;
			pblk->pnext = _phead;
			_phead = pblk;
			++_numfree;
		}
	};

#if defined(_MEM_TINY) // < 256M
	using selfalloctor = tinyalloc_<64, 512, spinlock>; //32K
#elif defined(_MEM_SML) // < 1G
	using selfalloctor = tinyalloc_<64, 1024, spinlock>; //64k
#else
	using selfalloctor = tinyalloc_<64, 2048, spinlock>; //128k
#endif
	extern selfalloctor* p_ec_alloctor_self; //self use global memory allocator for memblk_, blk_alloctor

	class memheap_ final // memory heap, memory blocks of the same size
	{
		struct t_blk
		{
			struct t_blk* pnext;
		};
	protected:
		size_t _numfree;// number of free blocks
		size_t _numblk;//number of all blocks
		size_t _sizeblk;// size of block bytes
		char* _pmem;// memory allocated from the system
		t_blk* _phead; // head block pointer of list
		void* _palloc; //blk_alloctor for release optimization
	public:
		memheap_* _pnext;

		inline size_t numfree() {
			return _numfree;
		}

		inline size_t numblk() {
			return _numblk;
		}

		inline size_t sizeblk() {
			return _sizeblk;
		}

		inline void* getalloc() const {
			return _palloc;
		}

		static void* operator new(size_t size)
		{
			return p_ec_alloctor_self->malloc_(size);
		}

		static void operator delete(void* p)
		{
			p_ec_alloctor_self->free_(p);
		}
	public:
		memheap_(void* palloc) :_numfree(0), _numblk(0), _sizeblk(0), _pmem(nullptr),
			_phead(nullptr), _palloc(palloc), _pnext(nullptr)
		{
		}
		~memheap_()
		{
			if (_pmem) {
				free(_pmem);
				_pmem = nullptr;
				_phead = nullptr;
				_numblk = 0;
				_numfree = 0;
				_sizeblk = 0;
				_palloc = nullptr;
			}
		}

		bool init(size_t sizeblk, size_t numblk)
		{
			if (sizeblk % EC_ALLOCTOR_ALIGN)
				sizeblk += (EC_ALLOCTOR_ALIGN - sizeblk % EC_ALLOCTOR_ALIGN);
			_pmem = (char*)malloc(numblk * (sizeblk + EC_ALLOCTOR_ALIGN));
			if (!_pmem)
				return false;
			_sizeblk = sizeblk;
			_numblk = numblk;

			char* ps = _pmem;
			sizeblk += EC_ALLOCTOR_ALIGN;

			for (size_t i = 0u; i < numblk; i++) {
				*reinterpret_cast<memheap_**>(ps) = this;
				if (i + 1 == numblk)
					reinterpret_cast<t_blk*>(ps + EC_ALLOCTOR_ALIGN)->pnext = nullptr;
				else
					reinterpret_cast<t_blk*>(ps + EC_ALLOCTOR_ALIGN)->pnext = reinterpret_cast<t_blk*>(ps + sizeblk + EC_ALLOCTOR_ALIGN);
				ps += sizeblk;
			}
			_phead = reinterpret_cast<t_blk*>(_pmem + EC_ALLOCTOR_ALIGN);
			_numfree = _numblk;
			return true;
		}

		void* malloc_(size_t size)
		{
			if (!_phead)
				return nullptr;
			t_blk* pret = _phead;
			_phead = _phead->pnext;
			_numfree--;
			return pret;
		}

		bool free_(void* pf)
		{
			t_blk* pblk = (t_blk*)pf;
			pblk->pnext = _phead;
			_phead = pblk;
			_numfree++;
			return true;
		}

		bool empty() const
		{
			return 0u == _numfree;
		}

		bool canfree() const
		{
			return _numblk == _numfree;
		}
	}; //memblk_

	template<class LOCK = null_lock>
	class blk_alloctor final  // memory block alloctor
	{
	protected:
		memheap_* _phead;
		LOCK _lck;
		int32_t _numheaps; // 堆个数，用于辅助释放空闲堆
		int32_t _numfreeblks; // 空闲内存块数,用于快速增加堆
		uint32_t _sizeblk; // 内存块的大小，构造或者初始化时设定,EC_ALLOCTOR_ALIGN字节对齐.
		uint32_t _numblksperheap; //每个堆里的内存块个数
	public:
		inline int32_t numheaps() const
		{
			return _numheaps;
		}

		inline size_t sizeblk() const
		{
			return _sizeblk;
		}

		inline int numfreeblks() const
		{
			return _numfreeblks;
		}

		static void* operator new(size_t size)
		{
			return p_ec_alloctor_self->malloc_(size);
		}

		static void operator delete(void* p)
		{
			p_ec_alloctor_self->free_(p);
		}

		blk_alloctor() :
			_phead(nullptr),
			_numheaps(0),
			_numfreeblks(0),
			_sizeblk(0),
			_numblksperheap(0)
		{
		}

		blk_alloctor(size_t sizeblk, size_t numblk) :
			_phead(nullptr),
			_numheaps(0),
			_numfreeblks(0),
			_sizeblk(0),
			_numblksperheap(0)
		{
			init(sizeblk, numblk);
		}

		~blk_alloctor() {
			memheap_* p = _phead, * pn;
			while (p) {
				pn = p->_pnext;
				delete p;
				p = pn;
			}
			_phead = nullptr;
			_numheaps = 0;
			_numfreeblks = 0;
			_sizeblk = 0;
			_numblksperheap = 0;
		}

		bool init(size_t sizeblk, size_t numblk)
		{
			if (_phead)
				return true;
			_phead = new memheap_(this);
			if (!_phead)
				return false;

			if (!_phead->init(sizeblk, numblk)) {
				delete _phead;
				_phead = nullptr;
				return false;
			}
			_sizeblk = (uint32_t)_phead->sizeblk();
			_numblksperheap = (uint32_t)numblk;
			_numheaps = 1;
			_numfreeblks += (int)_numblksperheap;
			return true;
		}

		void* malloc_(size_t size, size_t* poutsize)
		{
			safe_lock<LOCK> lck(&_lck);
			memheap_* pheap;
			if (!_numfreeblks) {
				pheap = new memheap_(this);
				if (!pheap)
					return nullptr;
				if (!pheap->init(_sizeblk, _numblksperheap)) {
					delete pheap;
					return nullptr;
				}
				pheap->_pnext = _phead;
				_phead = pheap;
				_numheaps++;
				_numfreeblks += (int32_t)(_numblksperheap - 1);
				if (poutsize)
					*poutsize = _sizeblk;
				return _phead->malloc_(size);
			}

			void* pret = nullptr;
			pheap = _phead;
			while (pheap) {
				if (nullptr != (pret = pheap->malloc_(size))) {
					_numfreeblks--;
					if (poutsize)
						*poutsize = _sizeblk;
					return pret;
				}
				pheap = pheap->_pnext;
			}
			assert(pret != nullptr);
			return nullptr;
		}

		bool free_(void* p)// for single allotor such as ec::hashmap
		{
			memheap_** pheap = (memheap_**)(static_cast<char*>(p) - EC_ALLOCTOR_ALIGN);
			if (!*pheap) { // system malloc
				::free(pheap);
				return true;
			}
			_lck.lock();
			assert((*pheap)->getalloc() == this);
			(*pheap)->free_(p);
			_numfreeblks++;
			if (_numheaps > EC_ALLOCTOR_GC_MINHEAPS && (*pheap)->canfree() && (*pheap) != _phead)
				gc_();
			_lck.unlock();
			return true;
		}

		bool free_(memheap_* pheap, void* p) // for multiple allotor
		{
			_lck.lock();
			pheap->free_(p);
			_numfreeblks++;
			if (_numheaps > EC_ALLOCTOR_GC_MINHEAPS && pheap->canfree() && pheap != _phead)
				gc_();
			_lck.unlock();
			return true;
		}

		size_t numfree() // for debug
		{
			safe_lock<LOCK> lck(&_lck);
			size_t zr = 0u;
			memheap_* p = _phead;
			while (p) {
				zr += p->numfree();
				p = p->_pnext;
			}
			return zr;
		}

	private:
		int gc_() //garbage collection
		{
			int n = 0;
			memheap_* p = _phead; //_phead永远不会被回收
			memheap_* pnext;
			if (!p)
				return n;
			pnext = p->_pnext;
			while (pnext) {
				if (pnext->canfree()) {
					p->_pnext = pnext->_pnext;
					delete pnext;
					_numheaps--;
					_numfreeblks -= (int)(_numblksperheap);
					n++;
				}
				else
					p = pnext;
				pnext = p->_pnext;
			}
			return n;
		}
	};

	class allocator final
	{
	protected:
		using PA_ = blk_alloctor<spinlock>*;
		unsigned int _size;
		PA_ _alloctors[8];

		bool add_alloctor(size_t sizeblk, size_t numblk)
		{
			if (_size == sizeof(_alloctors) / sizeof(PA_))
				return false;
			PA_ p = new blk_alloctor<spinlock>;
			if (!p->init(sizeblk, numblk)) {
				delete p;
				return false;
			}
			_alloctors[_size++] = p;
			return true;
		}
	public:
		allocator(size_t sizetiny, size_t numtiny,
			size_t sizesml = 0, size_t numsml = 0,
			size_t sizemid = 0, size_t nummid = 0,
			size_t sizelg = 0, size_t numlg = 0
		) :_size(0) {
			add_alloctor(sizetiny, numtiny);
			if (sizesml && numsml)
				add_alloctor(sizesml, numsml);
			if (sizemid && nummid)
				add_alloctor(sizemid, nummid);
			if (sizelg && numlg)
				add_alloctor(sizelg, numlg);
		}
		~allocator() {
			for (auto i = 0u; i < _size; i++) {
				if (_alloctors[i]) {
					delete _alloctors[i];
					_alloctors[i] = nullptr;
				}
			}
			_size = 0;
		}
		size_t maxblksize()
		{
			if (!_size)
				return 0;
			return _alloctors[_size - 1]->sizeblk();
		}

		void* malloc_(size_t size, size_t* psize = nullptr)
		{
			void* pret = nullptr;
			if (size > EC_OBJ_BIG_SIZE) { // malloc from system
				pret = ::malloc(size + EC_ALLOCTOR_ALIGN);
				if (!pret)
					return nullptr;
				*reinterpret_cast<memheap_**>(pret) = nullptr;
				if (psize)
					*psize = size;
				return reinterpret_cast<char*>(pret) + EC_ALLOCTOR_ALIGN;
			}

			for (auto i = 0u; i < _size; i++) {
				if (_alloctors[i]->sizeblk() >= size) {
					pret = _alloctors[i]->malloc_(size, psize);
					return pret;
				}
			}
			return pret;
		}

		void free_(void* p)
		{
			memheap_** pheap = (memheap_**)(reinterpret_cast<char*>(p) - EC_ALLOCTOR_ALIGN);
			if (!*pheap) { // system malloc
				::free(pheap);
				return;
			}
			reinterpret_cast<blk_alloctor<spinlock>*>((*pheap)->getalloc())->free_(*pheap, p);
		}

		void prtfree() // for debug
		{
			printf("\nprintf ec::alloctor{\n");
			for (auto i = 0u; i < _size; i++) {
				printf("heaps = %d, blksize %zu: numfree=%d, calculate free=%zu\n",
					_alloctors[i]->numheaps(), _alloctors[i]->sizeblk(), 
					_alloctors[i]->numfreeblks(), _alloctors[i]->numfree());
			}
			printf("}\n\n");
		}
	};
	extern ec::allocator* p_ec_allocator;
}//ec

