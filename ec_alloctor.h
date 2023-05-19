/*!
\file ec_alloctor.h
\author	jiangyong
\email  kipway@outlook.com
\update 2023.5.13

memory
	eclib memory allocator for ec::string, ec::hashmap, and other small objects etc.

eclib 3.0 Copyright (c) 2017-2023, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/

#pragma once
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <assert.h>

#ifndef _WIN32
#include <malloc.h>
namespace ec {
	struct glibc_noarena
	{
		glibc_noarena() {
			mallopt(M_ARENA_MAX, 1);
#if defined(_MEM_TINY) // < 256M
			mallopt(M_MMAP_THRESHOLD, 160 * 1024);
			mallopt(M_TRIM_THRESHOLD, 160 * 1024);
#elif defined(_MEM_SML) // < 1G
			mallopt(M_MMAP_THRESHOLD, 256 * 1024);
			mallopt(M_TRIM_THRESHOLD, 256 * 1024);
#else
			mallopt(M_MMAP_THRESHOLD, 1024 * 1024);
			mallopt(M_TRIM_THRESHOLD, 1024 * 1024);
#endif
		}
	};
}
#else
namespace ec {
	struct glibc_noarena
	{
	};
}
#endif
#define DECLARE_EC_GLIBC_NOARENA ec::glibc_noarena g_ec_glibc_noarena;

#include "ec_mutex.h"
#ifndef EC_ALLOCTOR_ALIGN
#define EC_ALLOCTOR_ALIGN 8u
#endif

#ifndef EC_ALLOCTOR_SHEAP_SIZE  // small heap size
#if defined(_MEM_TINY) // < 256M
#define EC_ALLOCTOR_SHEAP_SIZE (128 * 1024) // 128K heap size
#elif defined(_MEM_SML) // < 1G
#define EC_ALLOCTOR_SHEAP_SIZE (256 * 1024) // 256K heap size
#else
#define EC_ALLOCTOR_SHEAP_SIZE (1024 * 1024) // 1M heap size
#endif
#endif

#ifndef EC_ALLOCTOR_MHEAP_SIZE // middle heap size
#if defined(_MEM_TINY) // < 256M
#define EC_ALLOCTOR_MHEAP_SIZE (256 * 1024) // 256K heap size
#elif defined(_MEM_SML) // < 1G
#define EC_ALLOCTOR_MHEAP_SIZE (512 * 1024) // 512K heap size
#else
#define EC_ALLOCTOR_MHEAP_SIZE (2 * 1024 * 1024) // 1M heap size
#endif
#endif

#ifndef EC_ALLOCTOR_HHEAP_SIZE // huge heap size
#if defined(_MEM_TINY) // < 256M
#define EC_ALLOCTOR_HHEAP_SIZE (512 * 1024) // 512K heap size
#elif defined(_MEM_SML) // < 1G
#define EC_ALLOCTOR_HHEAP_SIZE (1 * 1024 * 1024) // 1M heap size
#else
#define EC_ALLOCTOR_HHEAP_SIZE (4 * 1024 * 1024) // 4M heap size
#endif
#endif

#ifndef EC_ALLOCTOR_GC_MINHEAPS // start garbage collection min number of heaps
#if defined(_MEM_TINY) // < 256M
#define EC_ALLOCTOR_GC_MINHEAPS 1
#elif defined(_MEM_SML) // < 1G
#define EC_ALLOCTOR_GC_MINHEAPS 2
#else
#define EC_ALLOCTOR_GC_MINHEAPS 3
#endif
#endif

#ifndef EC_SIZE_BLK_ALLOCATOR
#define EC_SIZE_BLK_ALLOCATOR 32
#endif

namespace ec {
	class null_lock final // null lock
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
			safe_lock<LOCK> lck(&_lck);
			if (size > _SIZE || !_phead)
				return ::malloc(size);
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
}// namespace ec

namespace ec{
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

		static void* operator new(size_t size);
		static void operator delete(void* p);
		static void* operator new(size_t size, void* ptr) { return ptr; }
		static void operator delete(void* ptr, void* voidptr2) noexcept {}
	public:
		memheap_(void* palloc) :_numfree(0), _numblk(0), _sizeblk(0), _pmem(nullptr),
			_phead(nullptr), _palloc(palloc), _pnext(nullptr)
		{
		}
		~memheap_()
		{
			if (_pmem) {
				::free(_pmem);
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
			_pmem = (char*)::malloc(numblk * (sizeblk + EC_ALLOCTOR_ALIGN));
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

		static void* operator new(size_t size);
		static void operator delete(void* p);
		static void* operator new(size_t size, void* ptr) { return ptr; }
		static void operator delete(void* ptr, void* voidptr2) noexcept {}

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

		bool init(size_t sizeblk, size_t numblk, bool balloc = true)
		{
			if (_phead)
				return true;
			if (!balloc) {
				if (sizeblk % EC_ALLOCTOR_ALIGN)
					sizeblk += (EC_ALLOCTOR_ALIGN - sizeblk % EC_ALLOCTOR_ALIGN);
				_sizeblk = (uint32_t)sizeblk;
				_numblksperheap = (uint32_t)numblk;
				return true;
			}
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
			memheap_* pprior = nullptr;
			pheap = _phead;
			while (pheap) {
				if (nullptr != (pret = pheap->malloc_(size))) {
					_numfreeblks--;
					if (poutsize)
						*poutsize = _sizeblk;
					if (pprior && pheap->numfree() > 0) { // move to head for next fast malloc
						pprior->_pnext = pheap->_pnext;
						pheap->_pnext = _phead;
						_phead = pheap;
					}
					return pret;
				}
				pprior = pheap;
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
			if (_numheaps > EC_ALLOCTOR_GC_MINHEAPS && (*pheap)->canfree() && (*pheap) != _phead 
				&& _numfreeblks % (int32_t)_numblksperheap)//防止不断整块分配和释放
				gc_();
			_lck.unlock();
			return true;
		}

		bool free_(memheap_* pheap, void* p) // for multiple allotor
		{
			_lck.lock();
			pheap->free_(p);
			_numfreeblks++;
			if (_numheaps > EC_ALLOCTOR_GC_MINHEAPS && pheap->canfree() && pheap != _phead 
				&& _numfreeblks % (int32_t)_numblksperheap)//防止不断整块分配和释放
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
		PA_ _alloctors[EC_SIZE_BLK_ALLOCATOR];

	public:
		bool add_alloctor(size_t sizeblk, size_t numblk, bool balloc = true)
		{
			if (_size == sizeof(_alloctors) / sizeof(PA_))
				return false;
			PA_ p = new blk_alloctor<spinlock>;
			if (!p->init(sizeblk, numblk, balloc)) {
				delete p;
				return false;
			}
			_alloctors[_size++] = p;
			return true;
		}
	public:
		allocator() :_size(0), _alloctors{ nullptr } {
		}

		allocator(size_t sizetiny, size_t numtiny,
			size_t sizesml = 0, size_t numsml = 0,
			size_t sizemid = 0, size_t nummid = 0,
			size_t sizelg = 0, size_t numlg = 0
		) :_size(0), _alloctors{ nullptr } {
			if (sizetiny && numtiny)
				add_alloctor(sizetiny, numtiny);
			if (sizesml && numsml)
				add_alloctor(sizesml, numsml);
			if (sizemid && nummid)
				add_alloctor(sizemid, nummid, false);
			if (sizelg && numlg)
				add_alloctor(sizelg, numlg, false);
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
			return 0u == _size ? 0 : _alloctors[_size - 1]->sizeblk();
		}

		void* malloc_(size_t size, size_t* psize = nullptr)
		{
			void* pret = nullptr;
			if (size > maxblksize()) { // malloc from system
				pret = ::malloc(size + EC_ALLOCTOR_ALIGN);
				if (!pret)
					return nullptr;
				*reinterpret_cast<memheap_**>(pret) = nullptr;
				if (psize)
					*psize = size;
				return reinterpret_cast<char*>(pret) + EC_ALLOCTOR_ALIGN;
			}
			uint32_t i = 0u;
			if (_size > 16) { // 1/2 find
				int nl = 0, nh = (int)_size - 1, nm = 0;
				while (nl <= nh) {
					nm = (nl + nh) / 2;
					if (_alloctors[nm]->sizeblk() < size)
						nl = nm + 1;
					else if (_alloctors[nm]->sizeblk() > size)
						nh = nm - 1;
					else 
						break;
				}
				i = nm;
			}
			for (; i < _size; i++) {
				if (_alloctors[i]->sizeblk() >= size) {
					pret = _alloctors[i]->malloc_(size, psize);
					return pret;
				}
			}
			return pret;
		}

		void* realloc_(void* ptr, size_t size, size_t* poutsize = nullptr)
		{
			if (!ptr) { // malloc
				if (!size)
					return nullptr;
				return malloc_(size, poutsize);
			}
			if (!size) { // free
				free_(ptr);
				return nullptr;
			}
			memheap_** pheap = (memheap_**)(reinterpret_cast<char*>(ptr) - EC_ALLOCTOR_ALIGN);
			if (!*pheap) { // system malloc
				void* pret = ::realloc(pheap, size + EC_ALLOCTOR_ALIGN);
				if (poutsize)
					*poutsize = size;
				return reinterpret_cast<char*>(pret) + EC_ALLOCTOR_ALIGN;
			}
			size_t sizeorg = (*pheap)->sizeblk();
			if (sizeorg >= size)
				return ptr;
			char* pnew = (char*)malloc_(size, poutsize);
			if (!pnew)
				return nullptr;
			memcpy(pnew, ptr, sizeorg);
			free_(ptr);
			return pnew;
		}

		void free_(void* p)
		{
			if (!p)
				return;
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

		template<class STR_ = std::string>
		void meminfo(STR_& sout) // for debug
		{
			char stmp[256];
			sout.append("\nec::alloctor{\n");
			for (auto i = 0u; i < _size; i++) {
				sprintf(stmp, "heaps = %d", _alloctors[i]->numheaps());
				sout.append(stmp);

				sprintf(stmp, ",blksize = %zu", _alloctors[i]->sizeblk());
				sout.append(stmp);

				sprintf(stmp, ",numfree = %d", _alloctors[i]->numfreeblks());
				sout.append(stmp);

				sprintf(stmp, ",calculate = %zu\n", _alloctors[i]->numfree());
				sout.append(stmp);
			}
			sout.append("}\n\n");
		}
	};

	constexpr size_t zbaseobjsize = sizeof(memheap_) > sizeof(blk_alloctor<spinlock>) ? sizeof(memheap_) : sizeof(blk_alloctor<spinlock>);
	constexpr size_t zselfblksize = (zbaseobjsize % 8u) ? zbaseobjsize + 8u - zbaseobjsize % 8u : zbaseobjsize;

#ifndef EC_ALLOCATOR_SELF_SIZE
#if defined(_MEM_TINY) // < 256M
	using selfalloctor = tinyalloc_<zselfblksize, 512, spinlock>; //32K
#elif defined(_MEM_SML) // < 1G
	using selfalloctor = tinyalloc_<zselfblksize, 2048, spinlock>; //128k
#else
	using selfalloctor = tinyalloc_<zselfblksize, 4096, spinlock>; //256k
#endif
#else
	using selfalloctor = tinyalloc_<zselfblksize, EC_ALLOCATOR_SELF_SIZE, spinlock>;
#endif
}//ec

ec::selfalloctor* get_ec_alloctor_self(); //self use global memory allocator for memblk_, blk_alloctor

#define DECLARE_EC_ALLOCTOR ec_allocator_ g_ec_allocator;\
ec::selfalloctor* get_ec_alloctor_self() { return &g_ec_allocator._alloctor_self; }\
ec::allocator* get_ec_allocator() { return &g_ec_allocator._alloctor; }\
void* ec::memheap_::operator new(size_t size){\
	return get_ec_alloctor_self()->malloc_(size);\
}\
void ec::memheap_::operator delete(void* p){\
	get_ec_alloctor_self()->free_(p);\
}\
template<class LOCK>\
 void* ec::blk_alloctor<LOCK>::operator new(size_t size){\
	 return get_ec_alloctor_self()->malloc_(size);\
 }\
 template<class LOCK>\
 void ec::blk_alloctor<LOCK>::operator delete(void* p) {\
	 get_ec_alloctor_self()->free_(p);\
 }

ec::allocator* get_ec_allocator();
class ec_allocator_ {
public:
	ec_allocator_() {
		size_t sheaps[10]{ 40, 80, 128, 256, 320, 512, 640, 800, 1024, 1600 };
		size_t mheaps[10]{ 2* 1024, 3 * 1024, 5 * 1024, 8 * 1024, 12 * 1024, 16 * 1024, 20 * 1024, 24 * 1024, 32 * 1024, 48 * 1024 };
		size_t hheaps[8]{ 64 * 1024, 128 * 1024, 256 * 1024, 400 * 1024, 640 * 1024, 1024 * 1024, 2 * 1024 * 1024 , 4 * 1024 * 1024 };

		for (auto i = 0u; i < sizeof(sheaps) / sizeof(size_t); i++) {
			if (EC_ALLOCTOR_SHEAP_SIZE / sheaps[i] < 4)
				break;
			_alloctor.add_alloctor(sheaps[i] - EC_ALLOCTOR_ALIGN, EC_ALLOCTOR_SHEAP_SIZE / sheaps[i], i < 3);
		}

		for (auto i = 0u; i < sizeof(mheaps) / sizeof(size_t); i++) {
			if (EC_ALLOCTOR_MHEAP_SIZE / mheaps[i] < 4)
				break;
			_alloctor.add_alloctor(mheaps[i] - EC_ALLOCTOR_ALIGN, EC_ALLOCTOR_MHEAP_SIZE / mheaps[i], false);
		}

		for (auto i = 0u; i < sizeof(hheaps) / sizeof(size_t); i++) {
			if (EC_ALLOCTOR_HHEAP_SIZE / hheaps[i] < 4)
				break;
			_alloctor.add_alloctor(hheaps[i] - EC_ALLOCTOR_ALIGN, EC_ALLOCTOR_HHEAP_SIZE / hheaps[i], false);
		}
	}
	ec::selfalloctor _alloctor_self;
	ec::allocator _alloctor;
};

namespace ec {
	// allocator for std containers
	template <class _Ty>
	class std_allocator
	{
	public:
		using value_type = _Ty;
		using pointer = _Ty*;
		using reference = _Ty&;
		using const_pointer = const _Ty*;
		using const_reference = const _Ty&;
		using size_type = size_t;
		using difference_type = ptrdiff_t;

		std_allocator() noexcept {
		}

		std_allocator(const std_allocator& alloc) noexcept {
		}

		template <class U>
		std_allocator(const std_allocator<U>& alloc) noexcept {
		}
		~std_allocator() {
		}

		template <class _Other>
		struct  rebind {
			using other = std_allocator<_Other>;
		};

		pointer address(reference x) const noexcept {
			return &x;
		}

		const_pointer address(const_reference x) const noexcept {
			return &x;
		}

		pointer allocate(size_type n, const void* hint = 0) {
			return (pointer)get_ec_allocator()->malloc_(sizeof(value_type) * n);
		}

		void deallocate(pointer p, size_type n) {
			get_ec_allocator()->free_(p);
		}

		size_type max_size() const noexcept {
			return size_t(-1) / sizeof(value_type);
		};

		void construct(pointer p, const_reference val) {
			new ((void*)p) value_type(val);
		}

		template <class _Objty, class... _Types>
		void construct(_Objty* _Ptr, _Types&&... _Args) {
			new ((void*)_Ptr) _Objty(std::forward<_Types>(_Args)...);
		}

		template <class _Uty>
		void destroy(_Uty* const _Ptr) {
			_Ptr->~_Uty();
		}
	};

	template <>
	class std_allocator<void> {
	public:
		using value_type = void;
		typedef void* pointer;
		typedef const void* const_pointer;

		template <class _Other>
		struct  rebind {
			using other = std_allocator<_Other>;
		};
	};

	template <class _Ty, class _Other>
	bool operator==(const std_allocator<_Ty>&, const std_allocator<_Other>&) noexcept {
		return true;
	}

	template <class _Ty, class _Other>
	bool operator!=(const std_allocator<_Ty>&, const std_allocator<_Other>&) noexcept {
		return false;
	}

	template <class OBJ, class... _Types>
	OBJ* newobj(_Types&&... _Args) {
		void* pobj = get_ec_allocator()->malloc_(sizeof(OBJ));
		new ((void*)pobj) OBJ(std::forward<_Types>(_Args)...);
		return (OBJ*)pobj;
	}

	template <class OBJ>
	void delobj(OBJ* pobj) {
		if (pobj) {
			pobj->~OBJ();
			get_ec_allocator()->free_(pobj);
		}
	}
} // namespace ec

inline void* ec_malloc(size_t size, size_t* psize = nullptr) {
	return  get_ec_allocator()->malloc_(size, psize);
}

inline void* ec_realloc(void* ptr, size_t size, size_t* psize = nullptr)
{
	return  get_ec_allocator()->realloc_(ptr, size, psize);
}

inline void ec_free(void* ptr)
{
	if (ptr)
		get_ec_allocator()->free_(ptr);
}

inline void* ec_calloc(size_t num, size_t size)
{
	void* pr = get_ec_allocator()->malloc_(num * size);
	if (pr)
		memset(pr, 0, num * size);
	return pr;
}

inline size_t ec_maxblksize() {
	return get_ec_allocator()->maxblksize();
}

#ifndef _USE_EC_OBJ_ALLOCATOR
#define _USE_EC_OBJ_ALLOCATOR \
static void* operator new(size_t size)\
{\
	return get_ec_allocator()->malloc_(size);\
}\
static void* operator new(size_t size, void* ptr)\
{\
	return ptr;\
}\
static void operator delete(void* p)\
{\
	get_ec_allocator()->free_(p);\
}\
static void operator delete(void* ptr, void* voidptr2) noexcept\
{\
}
#endif
/*
// tstmem.cpp 
// test ec_allocator
// g++ -O2 -pthread -I../eclib3 -std=c++11  -otstmem tstmem.cpp
// at intel N6005 ubuntu server 22.04 G++ 11.20
// ec_queue(us) : create =  1886928, delete =  863805, sum =  2750733, total =  2750989
// mem_queue(us): create =  2529579, delete = 1058470, sum =  3588049, total =  3588315
// std_queue(us): create =  2740759, delete = 1227592, sum =  3968351, total =  3968493
// 
// windows 11, i5-12500T, VS2022 x64 release
// ec_queue(us) : create =  2709310, delete = 1243576, sum =  3952886, total =  3952886
// mem_queue(us): create =  2617739, delete = 1090900, sum =  3708639, total =  3708639
// std_queue(us): create =  4407928, delete = 1276998, sum =  5684926, total =  5684926

#include "ec_system.h"
#include "ec_alloctor.h"
DECLARE_EC_GLIBC_NOARENA
DECLARE_EC_ALLOCTOR

#include <queue>
#include <string>

#include "ec_string.h"
#include "ec_queue.h"
#include "ec_time.h"

template<class STR_ = std::string>
class cnode
{
public:
	int _x;
	int _y;
	STR_ _str;
	STR_ _str2;
	STR_ _str3;
	cnode() :_x(0), _y(0) {

	}
	cnode(int x, int y, const char* s) :_x(x), _y(y) {
		_str.assign(s);
		_str2.assign(s);
		_str3.assign(s);
		_str[0] = '1' + x % 9;
		_str2[0] = '2' + x % 9;
		_str3[0] = '3' + x % 9;
	}
};

using memstr_ = std::basic_string<char, std::char_traits<char>, ec::std_allocator<char>>;
using std_queue = std::queue< cnode<>, std::deque<cnode<>>>;
using mem_queue = std::queue< cnode<memstr_>, std::deque<cnode<memstr_>, ec::std_allocator<cnode<memstr_>> > >;
using ec_queue = ec::queue<cnode<ec::string>>;

#define size_objs (1024 * 8)
const char* g_str = "DECLARE_EC_GLIBC_NOARENA+DECLARE_EC_GLIBC_NOARENA+DECLARE_EC_GLIBC_NOARENA";
class tst_queue
{
public:
	std::vector<std::string> _strs;
	tst_queue()
	{
		_strs.reserve(size_objs);
		for (int i = 0; i < size_objs; i++) {
			std::string s = g_str;
			s.append(std::to_string(i));
			_strs.push_back(std::move(s));
		}
	}

	template<class QU_>
	int64_t createobj(QU_& qu)
	{
		int64_t t1 = ec::time_ns(), t2 = 0;
		for (int i = 0; i < size_objs; i++) {
			qu.emplace(i, i + 1, _strs[i].c_str());
		}
		t2 = ec::time_ns();
		return t2 - t1;
	}

	template<class QU_>
	int64_t deleteobj(QU_& qu)
	{
		int64_t t1 = ec::time_ns(), t2 = 0;
		while (!qu.empty()) {
			qu.pop();
		}
		t2 = ec::time_ns();
		return t2 - t1;
	}

#define LOOP_NUM 2000
	void test()
	{
		ec_queue  ecq;
		mem_queue memq;
		std_queue stdq;

		int64_t t1, t2, tt1, tt2;

		t1 = 0;
		t2 = 0;
		tt1 = ec::time_ns();
		for (auto i = 0; i < LOOP_NUM; i++) {
			t1 += createobj(ecq);
			t2 += deleteobj(ecq);
		}
		tt2 = ec::time_ns();
		printf("ec_queue(us) : create = %8jd, delete =%8jd, sum = %8jd, total = %8jd\n", t1, t2, t1 + t2, tt2 - tt1);
#ifdef _WIN32
		Sleep(1000);
#else
		sleep(1);
#endif

		t1 = 0;
		t2 = 0;
		tt1 = ec::time_ns();
		for (auto i = 0; i < LOOP_NUM; i++) {
			t1 += createobj(memq);
			t2 += deleteobj(memq);
		}
		tt2 = ec::time_ns();
		printf("mem_queue(us): create = %8jd, delete =%8jd, sum = %8jd, total = %8jd\n", t1, t2, t1 + t2, tt2 - tt1);
#ifdef _WIN32
		Sleep(1000);
#else
		sleep(1);
#endif

		t1 = 0;
		t2 = 0;
		tt1 = ec::time_ns();
		for (auto i = 0; i < LOOP_NUM; i++) {
			t1 += createobj(stdq);
			t2 += deleteobj(stdq);
		}
		tt2 = ec::time_ns();
		printf("std_queue(us): create = %8jd, delete =%8jd, sum = %8jd, total = %8jd\n", t1, t2, t1 + t2, tt2 - tt1);
	}
};

int main()
{
	tst_queue qu;
	qu.test();
	return 0;
}*/