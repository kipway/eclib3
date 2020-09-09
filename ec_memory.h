/*!
\file ec_memory.h
\author	jiangyong
\email  kipway@outlook.com
\update 2020.9.6

memory
	fast memory allocator class for vector,hashmap etc.

autobuf
	buffer class auto free memory

eclib 3.0 Copyright (c) 2017-2018, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/

#pragma once
#include <cstdint>
#include <memory.h>
#include <vector>
#include "ec_mutex.h"

#define EC_MEM_ALIGN_SIZE (sizeof(size_t) * 2)
namespace ec {
	class memory
	{
	public:
		memory(const memory&) = delete;
		memory& operator = (const memory&) = delete;

		memory(size_t sblksize, size_t sblknum,
			size_t mblksize = 0, size_t mblknum = 0,
			size_t lblksize = 0, size_t lblknum = 0,
			spinlock* pmutex = nullptr
		) : _ps(nullptr), _pm(nullptr), _pl(nullptr), _pmutex(pmutex)
			, _blk_s(sblknum), _blk_m(mblknum), _blk_l(lblknum)
			, _nsys_malloc(0)
		{
			_sz_s = sblksize;// small memory blocks,Pre-allocation
			if (_sz_s % EC_MEM_ALIGN_SIZE)
				_sz_s += EC_MEM_ALIGN_SIZE - sblksize % EC_MEM_ALIGN_SIZE;
			malloc_block(_sz_s, _blk_s, _ps, _stks);

			_sz_m = mblksize; // medium memory blocks, malloc at the time of use
			if (_sz_m % EC_MEM_ALIGN_SIZE)
				_sz_m += EC_MEM_ALIGN_SIZE - mblksize % EC_MEM_ALIGN_SIZE;

			_sz_l = lblksize; // large memory blocks, malloc at the time of use
			if (_sz_l % EC_MEM_ALIGN_SIZE)
				_sz_l += EC_MEM_ALIGN_SIZE - lblksize % EC_MEM_ALIGN_SIZE;
		}
		~memory()
		{
			_stks.clear();
			_stkm.clear();
			_stkl.clear();
			if (_ps)
				::free(_ps);
			_ps = nullptr;
			if (_pm)
				::free(_pm);
			_pm = nullptr;
			if (_pl)
				::free(_pl);
			_pl = nullptr;
		}
		void *mem_malloc(size_t size)
		{
			unique_spinlock lck(_pmutex);
			size_t sizeout = 0;
			return _malloc(size, sizeout);
		}
		void mem_free(void *pmem)
		{
			unique_spinlock lck(_pmutex);
			return _free(pmem);
		}

		void *malloc(size_t size, size_t &outsize)
		{
			unique_spinlock lck(_pmutex);
			return _malloc(size, outsize);
		}

		void* mem_calloc(size_t count, size_t size)
		{
			void* p = mem_malloc(count * size);
			if (p)
				memset(p, 0, count * size);
			return p;
		}

		void *mem_realloc(void *ptr, size_t size)
		{
			unique_spinlock lck(_pmutex);
			return _realloc(ptr, size);
		}

		struct t_mem_info {
			int sysblks;// system blocks
			int sz_s, sz_m, sz_l;    // blocks size
			int blk_s, blk_m, blk_l; // blocks number
			int stk_s, stk_m, stk_l; // not use in stacks
		};

		void getinfo(t_mem_info *pinfo)
		{
			pinfo->sysblks = _nsys_malloc;
			pinfo->sz_s = (int)_sz_s;
			pinfo->sz_m = (int)_sz_m;
			pinfo->sz_l = (int)_sz_l;
			pinfo->blk_s = (int)_blk_s;
			pinfo->blk_m = (int)_blk_m;
			pinfo->blk_l = (int)_blk_l;
			pinfo->stk_s = (int)_stks.size();
			pinfo->stk_m = (int)_stkm.size();
			pinfo->stk_l = (int)_stkl.size();
		}

		inline size_t blksize_s()
		{
			return _sz_s;
		}
	private:
		void *_ps, *_pm, *_pl;
		spinlock* _pmutex;
		size_t _sz_s, _sz_m, _sz_l;  //blocks size
		size_t _blk_s, _blk_m, _blk_l; // blocks number
		std::vector<void*> _stks; // small memory blocks
		std::vector<void*> _stkm; // medium memory blocks
		std::vector<void*> _stkl; // large memory blocks
		int _nsys_malloc; //malloc memery blocks

		bool malloc_block(size_t blksize, size_t blknum, void * &ph, std::vector<void*> &stk)
		{
			if (!blknum || !blksize)
				return false;
			size_t i;
			ph = ::malloc(blksize * blknum);
			if (ph) {
				uint8_t *p = (uint8_t *)ph;
				stk.reserve(blknum + 2u - blknum % 2);
				for (i = 0; i < blknum; i++)
					stk.push_back(p + (blknum - 1 - i) * blksize);
			}
			return ph != nullptr;
		}

		void *_malloc(size_t size, size_t &outsize)
		{
			void* pr = nullptr;
			if (size <= _sz_s) {
				if (!_stks.empty() && (pr = _stks.back())) {
					_stks.pop_back();
					outsize = _sz_s;
					return pr;
				}
			}
			if (size <= _sz_m) {
				if (!_pm)
					malloc_block(_sz_m, _blk_m, _pm, _stkm);
				if (!_stkm.empty() && (pr = _stkm.back())) {
					_stkm.pop_back();
					outsize = _sz_m;
					return pr;
				}
				if (!_pl)
					malloc_block(_sz_l, _blk_l, _pl, _stkl);
				if (_pl && _stkl.size() > _blk_l / 2u) {
					if (nullptr != (pr = _stkl.back())) {
						_stkl.pop_back();
						outsize = _sz_l;
						return pr;
					}
				}
			}
			else if (size <= _sz_l) {
				if (!_pl)
					malloc_block(_sz_l, _blk_l, _pl, _stkl);
				if (!_stkl.empty() && (pr = _stkl.back())) {
					_stkl.pop_back();
					outsize = _sz_l;
					return pr;
				}
			}
			outsize = size;
			pr = ::malloc(size);
			if (!pr)
				outsize = 0;
			else
				_nsys_malloc++;
			return pr;
		}

		void *_realloc(void *ptr, size_t size)
		{
			if (!ptr && !size)
				return nullptr;
			if (!size) {
				_free(ptr);
				return nullptr;
			}
			size_t pa = (size_t)ptr, st = 0;
			if (_ps && pa >= (size_t)_ps  && pa < (size_t)_ps + _sz_s * _blk_s) {
				if (size <= _sz_s)
					return ptr;
				void* p = _malloc(size, st);
				if (!p)
					return nullptr;
				memcpy(p, ptr, _sz_s);
				_stks.push_back(ptr);
				return p;
			}
			else if (_pm &&  pa >= (size_t)_pm  && pa < (size_t)_pm + _sz_m * _blk_m) {
				if (size <= _sz_m)
					return ptr;
				void* p = _malloc(size, st);
				if (!p)
					return nullptr;
				memcpy(p, ptr, _sz_m);
				_stkm.push_back(ptr);
				return p;
			}
			else if (_pl &&  pa >= (size_t)_pl  && pa < (size_t)_pl + _sz_l * _blk_l) {
				if (size <= _sz_l)
					return ptr;
				void* p = _malloc(size, st);
				if (!p)
					return nullptr;
				memcpy(p, ptr, _sz_l);
				_stkl.push_back(ptr);
				return p;
			}
			return ::realloc(ptr, size);
		}

		void _free(void *pmem)
		{
			if (!pmem)
				return;
			size_t pa = (size_t)pmem;
			if (_ps && pa >= (size_t)_ps  && pa < (size_t)_ps + _sz_s * _blk_s)
				_stks.push_back(pmem);
			else if (_pm &&  pa >= (size_t)_pm  && pa < (size_t)_pm + _sz_m * _blk_m)
				_stkm.push_back(pmem);
			else if (_pl &&  pa >= (size_t)_pl  && pa < (size_t)_pl + _sz_l * _blk_l)
				_stkl.push_back(pmem);
			else {
				::free(pmem);
				_nsys_malloc--;
			}
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
			if (_pmem)
				_pbuf = (value_type*)_pmem->mem_malloc(_size * sizeof(value_type));
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
					_pmem->mem_free(_pbuf);
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
			if (_pmem)
				_pbuf = (value_type*)_pmem->mem_malloc(rsz * sizeof(value_type));
			else
				_pbuf = (value_type*)::malloc(rsz * sizeof(value_type));
			if (!_pbuf)
				return nullptr;
			_size = rsz;
			return _pbuf;
		}
	};
}
