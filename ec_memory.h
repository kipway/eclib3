/*!
\file ec_memory.h
\author	jiangyong
\email  kipway@outlook.com
\update 2021.1.12

memory
	fast memory allocator class for vector,hashmap etc.

autobuf
	buffer class auto free memory

mem_sets
	fast memory allocator

eclib 3.0 Copyright (c) 2017-2021, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/

#pragma once
#include <assert.h>
#include <cstdint>
#include <memory.h>
#include <vector>
#include "ec_mutex.h"

#ifndef EC_MEM_UPLEVEL
#define EC_MEM_UPLEVEL  2
#endif
#define EC_MEM_MAX_STKS 16
#define EC_MEM_ALIGN_SIZE (sizeof(size_t) * 2)
namespace ec {
	class memory
	{
	public:
		class stk
		{
		public:
			stk(size_t blksize, size_t blknum) : _blksize(blksize), _pos(0)
			{
				if (blksize % EC_MEM_ALIGN_SIZE)
					blksize += EC_MEM_ALIGN_SIZE - blksize % EC_MEM_ALIGN_SIZE;
				_pbuf = (unsigned char*)::malloc(blksize * blknum);
				if (!_pbuf)
					return;
				_pstk = (void**)::malloc(blknum * sizeof(void*));
				if (_pstk)
					_blknum = blknum;
				else {
					_blknum = 0;
					::free(_pbuf);
					_pbuf = nullptr;
					return;
				}
				for (auto i = _blknum; i > 0; i--)
					_pstk[_pos++] = _pbuf + (i - 1)* blksize;
			}
			~stk()
			{
				if (_pstk) {
					::free(_pstk);
					_pstk = nullptr;
				}
				if (_pbuf) {
					::free(_pbuf);
					_pbuf = nullptr;
				}
				_pos = 0;
				_blknum = 0;
			}
			void* pop()
			{
				if (_pstk && _pos) {
					--_pos;
					return _pstk[_pos];
				}
				return nullptr;
			}
			inline size_t blksize() const
			{
				return _blksize;
			}
			inline bool empty() const
			{
				return !_pos;
			}
			bool free(void* p)
			{
				if (_pbuf && reinterpret_cast<size_t>(p) >= (size_t)_pbuf
					&& reinterpret_cast<size_t>(p) < (size_t)_pbuf + _blksize * _blknum) {
					assert(_pos < _blknum);
					_pstk[_pos++] = p;
					return true;
				}
				return false;
			}

			inline bool in(void *p) const
			{
				return (_pbuf && reinterpret_cast<size_t>(p) >= (size_t)_pbuf
					&& reinterpret_cast<size_t>(p) < (size_t)_pbuf + _blksize * _blknum);
			}
		private:
			size_t _blksize, _blknum, _pos;
			void ** _pstk;
			unsigned char* _pbuf;
		}; // class stk

	public:
		memory(const memory&) = delete;
		memory& operator = (const memory&) = delete;
		memory(spinlock* plock = nullptr) :_plock(plock), _numstk(0)
		{
		}
		memory(size_t sblksize, size_t sblknum,
			size_t mblksize = 0, size_t mblknum = 0,
			size_t lblksize = 0, size_t lblknum = 0,
			spinlock* plock = nullptr
		) :memory(plock)
		{
			add_blk(sblksize, sblknum);
			add_blk(mblksize, mblknum);
			add_blk(lblksize, lblknum);
		}
		memory(size_t sblksize, size_t sblknum,
			size_t mblksize, size_t mblknum,
			size_t m2blksize, size_t m2blknum,
			size_t lblksize, size_t lblknum,
			spinlock* plock = nullptr
		) :memory(plock)
		{
			add_blk(sblksize, sblknum);
			add_blk(mblksize, mblknum);
			add_blk(m2blksize, m2blknum);
			add_blk(lblksize, lblknum);
		}
		~memory()
		{
			for (auto i = 0u; i < _numstk; i++) {
				delete _stks[i];
				_stks[i] = nullptr;
			}
			_numstk = 0;
		}
		void add_blk(size_t blksize, size_t blknum)
		{
			if (!blksize || !blknum)
				return;
			unique_spinlock lck(_plock);
			if (EC_MEM_MAX_STKS == _numstk)
				return;
			stk* p = new stk(blksize, blknum);
			if (!p)
				return;
			if (p->empty()) {
				delete p;
				return;
			}
			_stks[_numstk++] = p;
			if (_numstk < 2)
				return;
			qsort(_stks, _numstk, sizeof(stk*), compare);			
		}
		/*
		bool add_blk(size_t blksize, size_t blknum)
		{
			if (!blksize || !blknum)
				return true;
			unique_spinlock lck(_plock);
			if (EC_MEM_MAX_STKS == _numstk)
				return false;
			stk* p = new stk(blksize, blknum);
			if (!p)
				return false;
			if (p->empty()) {
				delete p;
				return false;
			}
			_stks[_numstk++] = p;
			if (_numstk < 2)
				return true;
			qsort(_stks, _numstk, sizeof(stk*), compare);
			return true;
		}*/
		void *malloc(size_t size, size_t &outsize, bool bext = false)
		{
			void *pret = _stkmalloc(size, outsize);
			if (pret)
				return pret;
			if (bext) {
				if (size % 16)
					size += 16 - size % 16;
				size += size / 2;
			}
			outsize = size;
			return ::malloc(size);
		}
		void *malloc(size_t size)
		{
			size_t zlen = 0;
			void *p = _stkmalloc(size, zlen);
			if (p)
				return p;
			return ::malloc(size);
		}
		void *realloc(void *ptr, size_t size, size_t &outsize)
		{
			if (!ptr)
				return  malloc(size, outsize);
			if (!size) {
				free(ptr);
				outsize = 0;
				return nullptr;
			}
			size_t ptrsize = _stkblksize(ptr);
			if (!ptrsize) {
				outsize = size;
				return ::realloc(ptr, size); // ptrsize==0 means that ptr is allocated by the system
			}

			if (ptrsize >= size && ptrsize < size * 2) {
				outsize = ptrsize;
				return ptr; // No need to adjust memory
			}

			void *p = malloc(size, outsize);
			if (p) {
				if (outsize == ptrsize) { // No need to adjust memory
					free(p);
					return ptr;
				}
				memcpy(p, ptr, ptrsize <= size ? ptrsize : size);
				free(ptr);
			}
			else
				outsize = 0;
			return p;
		}
		void free(void *p)
		{
			if (!_stkfree(p))
				::free(p);
		}
		size_t blksize_s()
		{
			if (!_numstk)
				return 0;
			return _stks[0]->blksize();
		}
	public: //Adapt to the previous version
		inline void *mem_malloc(size_t size)
		{
			return this->malloc(size);
		}
		void* mem_calloc(size_t count, size_t size)
		{
			void* p = malloc(count * size);
			if (p)
				memset(p, 0, count * size);
			return p;
		}
		inline void *mem_realloc(void *ptr, size_t size)
		{
			size_t zlen = 0;
			return this->realloc(ptr, size, zlen);
		}
		inline void mem_free(void *p)
		{
			this->free(p);
		}
	private:
		spinlock* _plock;
		size_t _numstk;
		stk*   _stks[EC_MEM_MAX_STKS];
	private:
		static int compare(const void* p1, const void* p2)
		{
			const stk *ps1 = *((const stk**)p1);
			const stk *ps2 = *((const stk**)p2);
			if (ps1->blksize() < ps2->blksize())
				return -1;
			else if (ps1->blksize() == ps2->blksize())
				return 0;
			return 1;
		}
		void *_stkmalloc(size_t size, size_t &outlen)
		{
			void * pret = nullptr;
			if (_plock)
				_plock->lock();
			int nup = 0;
			for (auto i = 0u; i < _numstk; i++) {
				if (_stks[i]->blksize() < size)
					continue;
				if (_stks[i]->empty()) {
					++nup;
					if (nup >= EC_MEM_UPLEVEL)
						break;
				}
				else {
					outlen = _stks[i]->blksize();
					pret = _stks[i]->pop();
					break;
				}
			}
			if (_plock)
				_plock->unlock();
			return pret;
		}
		bool _stkfree(void *p)
		{
			if (!p)
				return true;
			bool bret = false;
			if (_plock)
				_plock->lock();
			for (auto i = 0u; i < _numstk; i++) {
				if (_stks[i]->free(p)) {
					bret = true;
					break;
				}
			}
			if (_plock)
				_plock->unlock();
			return bret;
		}
		size_t _stkblksize(void *p)
		{
			size_t z = 0;
			if (_plock)
				_plock->lock();
			for (auto i = 0u; i < _numstk; i++) {
				if (_stks[i]->in(p)) {
					z = _stks[i]->blksize();
					break;
				}
			}
			if (_plock)
				_plock->unlock();
			return z;
		}
	};

	template <typename _Tp = char>
	class autobuf
	{
	public:
#if __GNUG__ && __GNUC__ < 5
		using value_type = typename std::enable_if<__has_trivial_copy(_Tp), _Tp>::type;
#else
		using value_type = typename std::enable_if<std::is_trivially_copyable<_Tp>::value, _Tp>::type;
#endif

		autobuf(const autobuf&) = delete;
		autobuf& operator = (const autobuf&) = delete;

		autobuf(memory* pmem = nullptr) :_pmem(pmem), _pbuf(nullptr), _size(0)
		{
		}
		autobuf(size_t size, memory* pmem = nullptr) :_pmem(pmem), _size(size)
		{
			if (_pmem) {
				_pbuf = (value_type*)_pmem->malloc(size * sizeof(value_type), _size);
				_size /= sizeof(value_type);
			}
			else
				_pbuf = (value_type*)::malloc(_size * sizeof(value_type));
			if (!_pbuf)
				_size = 0;
		}
		~autobuf()
		{
			clear();
		}
		autobuf& operator = (autobuf&& v) // for move
		{
			this->~autobuf();
			_pmem = v._pmem;
			_pbuf = v._pbuf;
			_size = v._size;

			v._pbuf = nullptr;
			v._size = 0;

			return *this;
		}
	private:
		memory* _pmem;
		value_type*   _pbuf;
		size_t  _size;
	public:
		inline value_type *data()
		{
			return _pbuf;
		}

		inline size_t size()
		{
			return _size;
		}

		void clear()
		{
			if (_pbuf) {
				if (_pmem)
					_pmem->free(_pbuf);
				else
					::free(_pbuf);
				_pbuf = nullptr;
				_size = 0;
			}
		}

		value_type* resize(size_t rsz)
		{ // not copy old data
			clear();
			if (!rsz)
				return nullptr;
			if (_pmem) {
				_pbuf = (value_type*)_pmem->malloc(rsz * sizeof(value_type), _size);
				_size /= sizeof(value_type);
			}
			else {
				_pbuf = (value_type*)::malloc(rsz * sizeof(value_type));
				_size = rsz;
			}
			if (!_pbuf) {
				_size = 0;
				return nullptr;
			}
			return _pbuf;
		}
};
	template <class _CLS, size_t _Num>
	class mem_sets
	{
	public:
		class mem_item
		{
		public:
			mem_item(size_t blksize, size_t blknum) :_blksize(blksize), _blknum(blknum)
			{
				_stack.reserve(_blknum);
				_pbuf = (uint8_t*)::malloc(_blksize * _blknum);
				if (_pbuf) {
					for (size_t i = 0u; i < _blknum; i++)
						_stack.push_back(_pbuf + i * _blksize);
				}
			}

			bool _free(void *p)
			{
				if (size_t(p) >= (size_t)_pbuf && (size_t)p < (size_t)_pbuf + _blksize * _blknum) {
					_stack.push_back(p);
					return true;
				}
				return false;
			}

			void* _malloc()
			{
				if (_stack.empty())
					return nullptr;
				void *p = _stack.back();
				_stack.pop_back();
				return p;
			}
			~mem_item() {
				if (_pbuf) {
					::free(_pbuf);
					_pbuf = nullptr;
					_stack.clear();
				}
			}
		private:
			size_t _blksize;
			size_t _blknum;
			uint8_t* _pbuf;
			std::vector<void*> _stack;
		};

	public:
		mem_sets() :_pos(0)
		{
			_mems.reserve(2048);
			_blksize = sizeof(_CLS);
			if (_blksize % 8u)
				_blksize += 8u - _blksize % 8u;
			mem_item *pm = new mem_item(_blksize, _Num);
			_mems.push_back(pm);
		}

		~mem_sets()
		{
			for (auto &i : _mems) {
				if (i) {
					delete i;
					i = nullptr;
				}
			}
		}
		_CLS* newcls()
		{
			ec::unique_spinlock lck(&_cs);
			void* pcls = nullptr;
			for (; _pos < _mems.size(); _pos++) {
				pcls = _mems[_pos]->_malloc();
				if (pcls) {
					new(pcls)_CLS();
					return (_CLS*)pcls;
				}
			}
			mem_item *pm = new mem_item(_blksize, _Num);
			if (!pm)
				return nullptr;
			_mems.push_back(pm);
			pcls = pm->_malloc();
			if (pcls) {
				new(pcls)_CLS();
				return (_CLS*)pcls;
			}
			return nullptr;
		}

		bool freecls(_CLS* p)
		{
			ec::unique_spinlock lck(&_cs);
			size_t i = 0u;
			for (; i < _mems.size(); i++) {
				if (_mems[i]->_free(p)) {
					if (i < _pos)
						_pos = i;
					p->~_CLS();
					return true;
				}
			}
			return false;
		}

	private:
		spinlock _cs;
		size_t _blksize;
		size_t _pos;
		std::vector<mem_item*> _mems;
	};
}
