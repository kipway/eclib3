/*!
\file ec_handle.h
\author	jiangyong
\email  kipway@outlook.com
\update 2020.11.10

class ec::handle

a handle template class

eclib 3.0 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/

#pragma once

#ifndef SIZE_MAXECHANDLES
#define SIZE_MAXECHANDLES	1024 // Maximum number of handles per process
#endif

#include "ec_map.h"
#include "ec_mutex.h"

#ifndef EC_HANDLE_UVSIZE
#define EC_HANDLE_UVSIZE 4  // user data
#endif
namespace ec
{
	template<class _Ty>
	class handle
	{
	public:
		typedef _Ty value_type;
		struct t_i {
			int key;
			value_type *pcls;
			union {
				void* pv;
				int32_t iv;
				int64_t lv;
			}uv[EC_HANDLE_UVSIZE];//app data
		};

	private:
		int _next;
		std::mutex _cs;
		hashmap<int, t_i> _map;

	private:
		void nexthv()
		{
			++_next;
			if (_next > 2 * SIZE_MAXECHANDLES)
				_next = 1;
		}

	public:
		handle(const handle&) = delete;
		handle& operator = (const handle&) = delete;

		handle() :_next(-1), _map(SIZE_MAXECHANDLES)
		{
		};

		~handle()
		{
			for (auto &i : _map) {
				if (i.pcls) {
					delete i.pcls;
					i.pcls = nullptr;
				}
			}
			_map.clear();
		}

		int  create()
		{
			unique_lock lck(&_cs);
			if (_map.size() >= SIZE_MAXECHANDLES)
				return -1;
			nexthv();
			while (_map.get(_next))
				nexthv();
			t_i tmp;
			memset(&tmp, 0, sizeof(tmp));
			tmp.key = _next;
			tmp.pcls = new value_type();
			if (!tmp.pcls)
				return -1;
			_map.set(tmp.key, tmp);
			return tmp.key;
		}

		int del(int h)
		{
			unique_lock lck(&_cs);
			t_i* pv = _map.get(h);
			if (!pv)
				return -1;
			if (pv->pcls) {
				delete pv->pcls;
				pv->pcls = nullptr;
			}
			_map.erase(h);
			return 0;
		}

		value_type* getobjptr(int h)
		{
			unique_lock lck(&_cs);
			t_i* p = _map.get(h);
			if (p)
				return p->pcls;
			return nullptr;
		}

		void* getpv(int h, int nindex)
		{
			unique_lock lck(&_cs);
			t_i* p = _map.get(h);
			if (p) {
				if (nindex < 0 || nindex >= EC_HANDLE_UVSIZE)
					return nullptr;
				return p->uv[nindex].pv;
			}
			return nullptr;
		}

		int32_t getiv(int h, int nindex)
		{
			unique_lock lck(&_cs);
			t_i* p = _map.get(h);
			if (p) {
				if (nindex < 0 || nindex >= EC_HANDLE_UVSIZE)
					return 0;
				return p->uv[nindex].iv;
			}
			return 0;
		}

		int64_t getlv(int h, int nindex)
		{
			unique_lock lck(&_cs);
			t_i* p = _map.get(h);
			if (p) {
				if (nindex < 0 || nindex >= EC_HANDLE_UVSIZE)
					return 0;
				return p->uv[nindex].lv;
			}
			return 0;
		}

		bool setpv(int h, int nindex, void* v)
		{
			unique_lock lck(&_cs);
			t_i* p = _map.get(h);
			if (p) {
				if (nindex < 0 || nindex >= EC_HANDLE_UVSIZE)
					return false;
				p->uv[nindex].pv = v;
				return true;
			}
			return false;
		}

		bool setiv(int h, int nindex, int32_t v)
		{
			unique_lock lck(&_cs);
			t_i* p = _map.get(h);
			if (p) {
				if (nindex < 0 || nindex >= EC_HANDLE_UVSIZE)
					return false;
				p->uv[nindex].iv = v;
				return true;
			}
			return false;
		}
		bool setlv(int h, int nindex, int64_t v)
		{
			unique_lock lck(&_cs);
			t_i* p = _map.get(h);
			if (p) {
				if (nindex < 0 || nindex >= EC_HANDLE_UVSIZE)
					return false;
				p->uv[nindex].lv = v;
				return true;
			}
			return false;
		}
	};// class handle
} //namespace ec