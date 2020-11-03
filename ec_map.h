/*!
\file ec_hashmap.h
\author jiangyong
\email  kipway@outlook.com
\update 2020.10.28

hashmap
	A hash map class, incompatible with std::unordered_map.
	iterator:	a forward iterator to value_type(not pair for key-val).

eclib 3.0 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/

#pragma once
#include <functional>
#include "ec_memory.h"
#include "ec_hash.h"

namespace ec
{
	template<class _Kty, class _Ty> // is _Kty is equal to the key in class _Ty
	struct keq_mapnode {
		bool operator()(_Kty key, const _Ty& val)
		{
			return key == val.key;
		}
	};
	template<class _Ty> // for pointer note
	struct del_mapnode {
		typedef _Ty value_type;
		void operator()(_Ty& val)
		{
		}
	};
	template<class _Kty
		, class _Ty
		, class _Keyeq = keq_mapnode<_Kty, _Ty>
		, class _DelVal = del_mapnode<_Ty>
		, class _Hasher = hash<_Kty>>
		class hashmap
	{
	public:
		using value_type = _Ty;
		using reference = value_type & ;
		using const_reference = const value_type &;
		using key_type = _Kty;
		using size_type = size_t;

		struct t_node {
			t_node*     pNext;
			value_type  value;
		};

		class iterator
		{
		public:
			iterator(hashmap *pmap, uint64_t pos) :_pmap(pmap), _pos(pos)
			{
			}

			bool operator == (const iterator &v)
			{
				return _pos == v._pos;
			}

			bool operator != (const iterator &v)
			{
				return _pos != v._pos;
			}

			iterator& operator ++() // ++i
			{
				_pos = _pmap->_nexti(_pos);
				return *this;
			}

			iterator operator ++(int) // i++
			{
				iterator i(_pmap, _pos);
				_pos = _pmap->_nexti(_pos);
				return i;
			}

			reference & operator*()
			{
				value_type* pv = _pmap->atpos(_pos);
				return *pv;
			}
		private:
			hashmap*	_pmap;
			uint64_t	_pos;
		};
	protected:
		t_node**	_ppv;
		size_type   _uhashsize;
		size_type   _usize;
		memory*		_pmem;
	private:
		t_node* new_node()
		{
			t_node* pnode = nullptr;
			if (_pmem)
				pnode = (t_node*)_pmem->mem_malloc(sizeof(t_node));
			else
				pnode = (t_node*)malloc(sizeof(t_node));
			if (pnode)
				new(&pnode->value)value_type();
			return pnode;
		}
		void free_node(t_node* p)
		{
			if (!p)
				return;
			_DelVal()(p->value);
			if (std::is_class<_Ty>::value)
				p->value.~value_type();
			if (_pmem)
				_pmem->mem_free(p);
			else
				free(p);
		}
	public:
		hashmap(const hashmap&) = delete;
		hashmap& operator = (const hashmap&) = delete;

		hashmap(unsigned int uhashsize = 1024)
			: _ppv(nullptr)
			, _uhashsize(uhashsize)
			, _usize(0)
		{
			_pmem = new ec::memory(size_node(), uhashsize * 2);
			_ppv = new t_node*[_uhashsize];
			if (nullptr == _ppv)
				return;
			memset(_ppv, 0, sizeof(t_node*) * _uhashsize);
		}

		~hashmap()
		{
			clear();
			if (_ppv) {
				delete[] _ppv;
				_ppv = nullptr;
			}
			if (_pmem) {
				delete _pmem;
				_pmem = nullptr;
			}
		}

		hashmap& operator = (hashmap&& v) // for move
		{
			this->~hashmap();
			_ppv = v._ppv;
			_uhashsize = v._uhashsize;
			_usize = v._usize;
			_pmem = v._pmem;

			v._ppv = nullptr;
			v._usize = 0;
			v._pmem = nullptr;
			return *this;
		}

		inline static size_t size_node()
		{
			return sizeof(t_node);
		}

		inline size_type size() const noexcept
		{
			return _usize;
		}

		inline bool empty() const noexcept
		{
			return !_ppv || !_usize;
		}

		iterator begin()
		{
			return iterator(this, _begin());
		}

		iterator end()
		{
			return iterator(this, -1);
		}

		bool set(key_type key, value_type& Value) noexcept
		{
			if (nullptr == _ppv)
				return false;
			size_type upos = _Hasher()(key) % _uhashsize;
			t_node* pnode;
			for (pnode = _ppv[upos]; pnode != nullptr; pnode = pnode->pNext) {
				if (_Keyeq()(key, pnode->value)) {
					_DelVal()(pnode->value);
					pnode->value = Value;
					return true;
				}
			}
			pnode = new_node();
			if (pnode == nullptr)
				return false;
			pnode->value = Value;
			pnode->pNext = _ppv[upos];
			_ppv[upos] = pnode;
			_usize++;
			return true;
		}

		bool set(key_type key, value_type&& Value) noexcept
		{
			if (nullptr == _ppv)
				return false;
			size_type upos = _Hasher()(key) % _uhashsize;
			t_node* pnode;
			for (pnode = _ppv[upos]; pnode != nullptr; pnode = pnode->pNext) {
				if (_Keyeq()(key, pnode->value)) {
					_DelVal()(pnode->value);
					pnode->value = std::move(Value);
					return true;
				}
			}
			pnode = new_node();
			if (pnode == nullptr)
				return false;
			pnode->value = std::move(Value);
			pnode->pNext = _ppv[upos];
			_ppv[upos] = pnode;
			_usize++;
			return true;
		}

		value_type* get(key_type key) noexcept
		{
			if (nullptr == _ppv || !_usize)
				return nullptr;
			size_type upos = _Hasher()(key) % _uhashsize;
			t_node* pnode;
			for (pnode = _ppv[upos]; pnode != nullptr; pnode = pnode->pNext) {
				if (_Keyeq()(key, pnode->value))
					return &pnode->value;
			}
			return nullptr;
		}

		bool get(key_type key, value_type& Value) noexcept
		{
			value_type* pv = get(key);
			if (nullptr == pv)
				return false;
			Value = *pv;
			return true;
		}

		inline bool has(key_type key) noexcept
		{
			return nullptr != get(key);
		}

		void clear() noexcept
		{ // if value_type is a pointer, You can release it in fun
			if (!_ppv)
				return;
			if (_usize) {
				t_node* ppre, *pNode;
				for (size_type i = 0; i < _uhashsize; i++) {
					pNode = _ppv[i];
					while (pNode) {
						ppre = pNode;
						pNode = pNode->pNext;
						free_node(ppre);
					}
					if (_ppv[i])
						_ppv[i] = nullptr;
				}
			}
			_usize = 0;
		}

		bool erase(key_type key) noexcept
		{ // if value_type is a pointer, You can release it in fun
			if (nullptr == _ppv || !_usize)
				return false;
			size_type upos = _Hasher()(key) % _uhashsize;
			t_node** ppNodePrev;
			ppNodePrev = &_ppv[upos];
			t_node* pNode;
			for (pNode = *ppNodePrev; pNode != nullptr; pNode = pNode->pNext) {
				if (_Keyeq()(key, pNode->value)) {
					*ppNodePrev = pNode->pNext;
					free_node(pNode);
					_usize--;
					return true;
				}
				ppNodePrev = &pNode->pNext;
			}
			return false;
		}

		value_type* next(uint64_t& i) noexcept
		{
			value_type* pv = nullptr;
			if (nullptr == _ppv || (i >> 32) >= _uhashsize || !_usize) {
				i = -1;
				return pv;
			}
			t_node* pNode = nullptr;
			unsigned int ih = (unsigned int)(i >> 32), il = (unsigned)(i & 0xffffffff);
			unsigned int ul;
			while (ih < _uhashsize) {
				pNode = _ppv[ih];
				ul = 0;
				while (ul < il && pNode) {
					pNode = pNode->pNext;
					ul++;
				}
				if (pNode) {
					pv = &pNode->value;
					if (!pNode->pNext)
						i = _nexti(ih + 1, 0);
					else
						i = _nexti(ih, ul + 1);
					return pv;
				}
				ih++;
				il = 0;
			}
			i = -1;
			return nullptr;
		}

		bool next(uint64_t& i, value_type* &pv) noexcept
		{
			pv = next(i);
			return pv != nullptr;
		}

		bool next(uint64_t& i, value_type &rValue) noexcept
		{
			value_type* pv = nullptr;
			bool bret = next(i, pv);
			if (bret)
				rValue = *pv;
			return bret;
		}

	protected:
		uint64_t _nexti(unsigned int ih, unsigned int il) noexcept
		{
			uint64_t ir = 0;
			unsigned int ul;
			t_node* pNode;
			while (ih < _uhashsize) {
				pNode = _ppv[ih];
				ul = 0;
				while (ul < il && pNode) {
					pNode = pNode->pNext;
					ul++;
				}
				if (pNode) {
					ir = ih;
					ir <<= 32;
					ir += ul;
					return ir;
				}
				ih++;
				il = 0;
			}
			return -1;
		}

		uint64_t _nexti(uint64_t pos) noexcept
		{
			uint64_t ih = pos >> 32, il = pos & 0xffffffff, ul = 0;
			t_node* pNode;
			if (ih < _uhashsize) {
				pNode = _ppv[ih];
				while (ul < il && pNode) {
					pNode = pNode->pNext;
					ul++;
				}
				if (pNode && pNode->pNext) {
					++ul;
					return (ih << 32) + ul;
				}
				else
					++ih;
			}

			while (ih < _uhashsize) {
				if (_ppv[ih])
					return ih << 32;
				ih++;
			}
			return -1;
		}

		value_type* atpos(uint64_t i) noexcept
		{
			unsigned int ih = (unsigned int)(i >> 32), il = (unsigned)(i & 0xffffffff);
			if (nullptr == _ppv || ih >= _uhashsize || !_usize)
				return nullptr;

			t_node* pNode = nullptr;
			unsigned int ul = 0;
			if (ih < _uhashsize) {
				pNode = _ppv[ih];
				while (ul < il && pNode) {
					pNode = pNode->pNext;
					ul++;
				}
				if (ul == il && pNode)
					return &pNode->value;
			}
			return nullptr;
		}

		uint64_t _begin()
		{
			uint64_t ih = 0;
			if (nullptr == _ppv || !_usize)
				return -1;
			while (ih < _uhashsize) {
				if (_ppv[ih])
					return  (ih << 32);
				ih++;
			}
			return -1;
		}
	};
}

/*
class maptest // demo for hashmap
{
public:
	class ctst
	{
	public:
		int _id;
		char _sid[12];
	public:
		ctst()
		{
			_id = 0;
			_sid[0] = 0;
			printf("construct ctst default\n");
		}
		ctst(int nid, const char* sid)
		{
			_id = nid;
			ec::strlcpy(_sid, sid, sizeof(_sid));
			printf("construct(%d,%s)\n", _id, _sid);
		}
		ctst(const ctst &v)
		{
			_id = v._id;
			memcpy(_sid, v._sid, sizeof(_sid));
			printf("construct ctst(%d,%s)\n", _id, _sid);
		}
		void operator=(const ctst &v)
		{
			printf("=\n");
			_id = v._id;
			memcpy(_sid, v._sid, sizeof(_sid));
		}
		void operator=(const ctst &&v)
		{
			printf("move\n");
			_id = v._id;
			memcpy(_sid, v._sid, sizeof(_sid));
		}
		~ctst()
		{
			printf("destruct ctst(%d,%s)\n", _id, _sid);
		}
	};

	struct keq_ctst
	{
		bool operator()(int key, const ctst& val)
		{
			return key == val._id;
		}
	};

public:
	maptest()
	{
	}
	void test()
	{
		ec::hashmap<int, ctst, keq_ctst> map1;

		// insert 100 object
		char s[16] = { 0 };
		for (int i = 0; i < 100; i++) {
			snprintf(s, sizeof(s), "tst%02d", i);
			map1.set(i, ctst(i, s));
		}

		// get the value pointer
		ctst* p = map1.get(8);
		if (p)
			printf("get success, (%d,%s)\n", p->_id, p->_sid);

		// C++ style for loop
		for (auto &i : map1) {
			if (i._id == 0)
				strcpy(i._sid, "modify");
			printf("key=%d,val = %s\n", i._id, i._sid);
		}
	}
};
*/