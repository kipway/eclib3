/*!
\file ec_memory.h
\author	jiangyong
\email  kipway@outlook.com
\update 2020.8.29

eclib class fast memory allocator.

class memory;

eclib 3.0 Copyright (c) 2017-2018, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

简介：
ec::memory
	是为vector,hashmap等准备的内存分配器。
有小，中，大，三种块大小的连续内存，每一种指定块的数量。其中小块在构造时就分配，中块和大块在第一次使用是分配。
每次使用按照块使用。
	可以指定一个自旋锁以便被多线程使用。

两个目的：
	1.提高效率，预分配内存。分配只是从块的stack pop操作, 释放只是放回块的stack push操作， 操作耗时都是O(1)固定的。
	2.连续内存，减少内存碎片。

适合频繁分配释放的场景，比如服务端处理网络IO消息时。

ec::autobuf
	是一个自动释放内存的模板类，主要用于临时分配一个空间，不必关心释放，作用域结束时会自动释放，支持从ec::memory分配内存。

*/
#pragma once
#include <cstdint>
#include <memory.h>
#include <vector>
#include "ec_mutex.h"

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
		) : _ps(nullptr), _pm(nullptr), _pl(nullptr), _pmutex(pmutex),
			_blk_s(sblknum), _blk_m(mblknum), _blk_l(lblknum),
			_nsys_malloc(0),
			_uerr_s(0), _uerr_m(0), _uerr_l(0)
		{
			_sz_s = sblksize;// small memory blocks,Pre-allocation
			if (_sz_s % (sizeof(size_t) * 2))
				_sz_s += (sizeof(size_t) * 2) - sblksize % (sizeof(size_t) * 2);
			malloc_block(_sz_s, _blk_s, _ps, _stks);

			_sz_m = mblksize; // medium memory blocks, malloc at the time of use
			if (_sz_m % (sizeof(size_t) * 2))
				_sz_m += (sizeof(size_t) * 2) - mblksize % (sizeof(size_t) * 2);

			_sz_l = lblksize; // large memory blocks, malloc at the time of use
			if (_sz_l % (sizeof(size_t) * 2))
				_sz_l += (sizeof(size_t) * 2) - lblksize % (sizeof(size_t) * 2);
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
			int sysblks, err_s, err_m, err_l;
			int sz_s, sz_m, sz_l;    // blocks size
			int blk_s, blk_m, blk_l; // blocks number
			int stk_s, stk_m, stk_l; // not use in stacks
		};

		void getinfo(t_mem_info *pinfo)
		{
			pinfo->sysblks = _nsys_malloc;
			pinfo->err_s = _uerr_s;
			pinfo->err_m = _uerr_m;
			pinfo->err_l = _uerr_l;
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
		int _uerr_s, _uerr_m, _uerr_l;// free error

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
#ifdef USE_ECMEM_SAFE
			bool bsafe = true;
#else
			bool bsafe = false;
#endif
			size_t pa = (size_t)pmem;
			if (_ps && pa >= (size_t)_ps  && pa < (size_t)_ps + _sz_s * _blk_s) {
				if (!bsafe || !isExist(_stks, pmem))
					_stks.push_back(pmem);
				else _uerr_s++;
			}
			else if (_pm &&  pa >= (size_t)_pm  && pa < (size_t)_pm + _sz_m * _blk_m) {
				if (!bsafe || !isExist(_stkm, pmem))
					_stkm.push_back(pmem);
				else _uerr_m++;
			}
			else if (_pl &&  pa >= (size_t)_pl  && pa < (size_t)_pl + _sz_l * _blk_l) {
				if (!bsafe || !isExist(_stkl, pmem))
					_stkl.push_back(pmem);
				else _uerr_l++;
			}
			else {
				::free(pmem);
				_nsys_malloc--;
			}
		}

		bool isExist(std::vector<void*> &stk, void* p)
		{
			for (auto &i : stk) {
				if (i == p)
					return true;
			}
			return false;
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
