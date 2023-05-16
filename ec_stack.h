/*!
\file ec_stack.h

\author jiangyong
\email  kipway@outlook.com
\update 2023.5.15

stack
	 LIFO stack

eclib 3.0 Copyright (c) 2017-2022, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/
#pragma once
#include "ec_alloctor.h"
namespace ec
{
	template<class _Ty>
	class stack
	{
	public:
		using value_type = _Ty;
		using reference = value_type&;
		using const_reference = const value_type&;
		using size_type = size_t;

		class  t_node {
		public:
			t_node* pNext;
			value_type  value;
		public:
			t_node() : pNext(nullptr) {
			}
			t_node(value_type& v) : pNext(nullptr) {
				value = v;
			}
			t_node(value_type&& v) : pNext(nullptr) {
				value = std::move(v);
			}
			template <typename... Args>
			t_node(Args&&... args) : pNext(nullptr), value(std::forward<Args>(args)...) {
			}
			_USE_EC_OBJ_ALLOCATOR
		};
	protected:
		t_node* _phead;
		size_type _size;
	public:
		stack() :_phead(nullptr), _size(0) {
		}
		~stack() {
			while (_phead) {
				pop();
			}
		}
		inline bool empty() const
		{
			return nullptr == _phead;
		}
		inline size_type size() const
		{
			return _size;
		}
		inline reference& top()
		{
			return _phead->value;
		}
		inline const_reference& top() const
		{
			return _phead->value;
		}
		void push(const value_type& val)
		{
			t_node* pnode = new t_node(val);
			if (!pnode)
				return;
			pnode->pNext = _phead;
			_phead = pnode;
			++_size;
		}
		void push(value_type&& val)
		{
			t_node* pnode = new t_node(std::move(val));
			if (!pnode)
				return;
			pnode->pNext = _phead;
			_phead = pnode;
			++_size;
		}
		void pop()
		{
			t_node* pnode = _phead;
			if (!pnode)
				return;
			_phead = _phead->pNext;
			delete pnode;
			--_size;
		}
		void swap(stack& x) noexcept
		{
			t_node* h = _phead;
			size_type size = _size;

			_phead = x._phead;
			_size = x._size;

			x._phead = h;
			x._size = size;
		}
		template <typename... Args>
		void emplace(Args&&... args)
		{
			t_node* pnode = new t_node(std::forward<Args>(args)...);
			if (!pnode)
				return;
			pnode->pNext = _phead;
			_phead = pnode;
			++_size;
		}
	};
}// namespace ec
