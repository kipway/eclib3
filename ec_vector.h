/*!
\file ec_vector.h
\author	jiangyong
\email  kipway@outlook.com
\update 2020.11.29

vector
	a extend vector class for trivially copyable type, and expanded some functions, can be used as string, stack, stream

eclib 3.0 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/
#pragma once
#include <stdint.h>
#include <memory.h>
#include <type_traits>
#include <stdexcept>
#include <stdarg.h>
#include "ec_memory.h"
namespace ec
{
	struct null_alloctor {
		ec::memory* operator()()
		{
			return nullptr;
		}
	};

	template<typename _Tp, class _Alloctor = null_alloctor>
	class vector
	{
	public:
#if __GNUG__ && __GNUC__ < 5
		using value_type = typename std::enable_if<__has_trivial_copy(_Tp), _Tp>::type;
#else
		using value_type = typename std::enable_if<std::is_trivially_copyable<_Tp>::value, _Tp>::type;
#endif
		using size_type = size_t;

		using pointer = value_type * ;
		using const_pointer = const value_type *;

		using iterator = _Tp * ;
		using const_iterator = const _Tp *;

		using reference = value_type & ;
		using const_reference = const value_type &;

		vector(ec::memory* pmem = nullptr) : _pbuf(nullptr)
			, _pos(0)
			, _usize(0)
			, _ubufsize(0)
			, _pmem(pmem)
		{
			if (!pmem)
				_pmem = _Alloctor()();
		}

		vector(const value_type* s, size_type size, ec::memory* pmem = nullptr) : vector(pmem)
		{
			append(s, size);
		}

		template<typename T
			, class = typename std::enable_if<sizeof(_Tp) == 1 && std::is_same<T, char>::value>::type>
			vector(const T* s, ec::memory* pmem = nullptr) : vector(pmem)
		{
			if (s)
				append(s, strlen(s));
		}

		vector(const vector &v) : vector(v._pmem)
		{
			clear();
			append(v.data(), v.size());
		}

		vector(vector &&v) // for move
		{
			_pbuf = v._pbuf;
			_pos = v._pos;
			_usize = v._usize;
			_ubufsize = v._ubufsize;
			_pmem = v._pmem;

			v._pbuf = nullptr;
			v._pos = 0;
			v._usize = 0;
			v._ubufsize = 0;
			v._pmem = nullptr;
		}

		~vector()
		{
			if (_pbuf != nullptr) {
				mem_free(_pbuf);
				_pbuf = nullptr;
				_pos = 0;
				_usize = 0;
				_ubufsize = 0;
			}
		};

		vector& operator = (const vector& v)
		{
			clear();
			append(v.data(), v.size());
			return *this;
		}

		vector& operator = (vector&& v) // for move
		{
			this->~vector();

			_pbuf = v._pbuf;
			_pos = v._pos;
			_usize = v._usize;
			_ubufsize = v._ubufsize;
			_pmem = v._pmem;

			v._pbuf = nullptr;
			v._pos = 0;
			v._usize = 0;
			v._ubufsize = 0;
			v._pmem = nullptr;

			return *this;
		}

		inline ec::memory* get_allocator()
		{
			return _pmem;
		}
	protected:
		value_type * _pbuf; // buffer
		size_t	_pos;  // read write as stream
		size_type	_usize; // itemsize
		size_type	_ubufsize;// buffer size
		ec::memory *_pmem;

		inline void *mem_malloc(size_t size, size_t &sizeout)
		{
			if (_pmem)
				return _pmem->malloc(size, sizeout);
			sizeout = size;
			return ::malloc(size);
		}

		inline void mem_free(void* p)
		{
			if (_pmem)
				_pmem->mem_free(p);
			else
				::free(p);
		}

		void _grown(size_type usize = 1)
		{
			if (_usize + usize <= _ubufsize || !usize)
				return;
			size_type usizet;

			usizet = _ubufsize;
			if (!usizet)
				usizet = 4;
			while (usizet < _usize + usize)
				usizet *= 2;

			if (usizet > max_size())
				throw std::range_error("range error");

			value_type	*pt = nullptr;
			size_t sizeout = 0;
			pt = (value_type*)mem_malloc(usizet * sizeof(value_type), sizeout);
			if (!pt)
				throw std::bad_alloc();
			if (_pbuf) {
				if (_usize)
					memcpy(pt, _pbuf, _usize * sizeof(value_type));
				mem_free(_pbuf);
			}
			_ubufsize = sizeout / sizeof(value_type);
			_pbuf = pt;
		}

		iterator _insert(size_type pos, const value_type *pdatas, size_type n) // insert before
		{
			if (!pdatas || !n)
				return begin();
			if (pos >= _usize) {
				append(pdatas, n);
				return _pbuf + pos;
			}
			_grown(n);
			memmove(_pbuf + pos + n, _pbuf + pos, (_usize - pos) * sizeof(value_type));
			memcpy(_pbuf + pos, pdatas, n * sizeof(value_type));
			_usize += n;
			return _pbuf + pos;
		}

	public: //Iterators

		iterator begin()
		{
			return _usize ? _pbuf : nullptr;
		}

		const_iterator begin() const
		{
			return _usize ? _pbuf : nullptr;
		}

		iterator end()
		{
			return _usize ? _pbuf + _usize : nullptr;
		}

		const_iterator end() const
		{
			return _usize ? _pbuf + _usize : nullptr;
		}

		const_iterator cbegin() const noexcept
		{
			return _usize ? _pbuf : nullptr;
		}

		const_iterator cend() const noexcept
		{
			return _usize ? _pbuf + _usize : nullptr;
		}

	public: // Capacity

		inline size_type max_size() const noexcept
		{
			return SIZE_MAX / sizeof(value_type);
		}

		inline size_type size() const noexcept
		{
			return _usize;
		}

		inline size_type capacity() const noexcept
		{
			return _ubufsize;
		}

		void resize(size_type n)
		{
			if (n <= _ubufsize)
				_usize = n;
			else {
				_grown(n - _usize);
				_usize = n;
			}
		}

		inline bool empty() const
		{
			return !_pbuf || !_usize;
		}

		void reserve(size_type n)
		{
			if (!n && !_usize) { // free memory
				if (_pbuf)
					mem_free(_pbuf);
				_pbuf = nullptr;
				_pos = 0;
				_ubufsize = 0;
				return;
			}
			if (n <= _ubufsize)
				return;

			value_type	*pt = nullptr;
			size_t sizeout = 0;
			pt = (value_type*)mem_malloc(n * sizeof(value_type), sizeout);
			if (!pt)
				throw std::bad_alloc();
			if (_pbuf) {
				if (_usize)
					memcpy(pt, _pbuf, _usize * sizeof(value_type));
				mem_free(_pbuf);
			}
			_pbuf = pt;
			_ubufsize = sizeout / sizeof(value_type);
		}

		void shrink_to_fit()
		{
			if (!_usize) {
				if (_pbuf)
					mem_free(_pbuf);
				_pbuf = nullptr;
				_pos = 0;
				_ubufsize = 0;
				return;
			}
			if (_usize > _ubufsize / 2 || _ubufsize < 16) // less 1/2 shrink to fit
				return;
			if (_pmem && _ubufsize <= _pmem->blksize_s())
				return;
			size_t sizeout = 0, sizenew = _usize + ((_usize % 8) ? (8u - _usize % 8) : 0);
			value_type* pnew = (value_type*)mem_malloc(sizenew * sizeof(value_type), sizeout);
			if (!pnew)
				return;
			memcpy(pnew, _pbuf, _usize * sizeof(value_type));
			mem_free(_pbuf);
			_pbuf = pnew;
			_ubufsize = sizeout / sizeof(value_type);
		}

	public: // Element access
		reference operator[] (size_type n) noexcept
		{
			return _pbuf[n];
		}

		const_reference operator[] (size_type n) const noexcept
		{
			return _pbuf[n];
		}

		reference at(size_type n)
		{
			if (n >= _usize)
				throw std::range_error("range error");
			return _pbuf[n];
		}
		const_reference at(size_type n) const
		{
			if (n >= _usize)
				throw std::range_error("range error");
			return _pbuf[n];
		}

		reference front() noexcept
		{
			return *_pbuf;
		}

		const_reference front() const noexcept
		{
			return *_pbuf;
		}

		reference back() noexcept
		{
			return _pbuf[_usize - 1];
		}

		const_reference back() const noexcept
		{
			return _pbuf[_usize - 1];
		}

		value_type* data() noexcept
		{
			return _pbuf;
		}

		const value_type* data() const noexcept
		{
			return _pbuf;
		}

	public: // Modifiers
		template<class _Iter
			, class = typename std::enable_if<std::is_same<_Iter, iterator>::value>::type>
			void assign(_Iter first, _Iter last)
		{
			_usize = 0;
			if (first == last)
				return;
			append(first, last - first);
		}

		void push_back(const value_type& val)
		{
			_grown();
			*(_pbuf + _usize) = val;
			_usize += 1;
		}

		void pop_back() noexcept
		{
			if (_usize)
				--_usize;
		}

		iterator insert(iterator position, const value_type& val)
		{
			return _insert(position - begin(), &val, 1);
		}

		template<class _Iter
			, class = typename std::enable_if<std::is_same<_Iter, iterator>::value>::type>
			void insert(iterator position, _Iter first, _Iter last)
		{
			return _insert(position - begin(), first, last - first);
		}

		vector& erase(size_type pos, size_type size)
		{
			if (!size || !_pbuf || pos >= _usize)
				return *this;
			if (pos + size >= _usize)
				_usize = pos;
			else {
				memmove(_pbuf + pos, _pbuf + pos + size, (_usize - (pos + size)) * sizeof(value_type));
				_usize -= size;
			}
			return *this;
		}

		inline iterator erase(iterator position)
		{
			return myerase((size_type)(position - begin()), 1u);
		}

		inline iterator erase(iterator first, iterator last)
		{
			return myerase((size_type)(first - begin()), (size_type)(last - first));
		}

		inline void clear() noexcept
		{
			_usize = 0;
		}

	public: //extend
		vector& append(const value_type* pdata, size_type n)
		{ // like std::string::append
			if (!n || !pdata)
				return *this;
			_grown(n);
			memcpy(_pbuf + _usize, pdata, n * sizeof(value_type));
			_usize += n;
			return *this;
		}

		vector& replace(size_type pos, size_type rsize, const value_type *pbuf, size_type usize)
		{
			if (!rsize) {
				insert(pos, pbuf, usize);  // insert
				return *this;
			}
			if (!pbuf || !usize) { //delete
				if (pos + rsize >= _usize) {
					_usize = pos;
					return *this;
				}
				memmove(_pbuf + pos, _pbuf + pos + rsize, (_usize - (pos + rsize)) * sizeof(value_type));
				_usize = _usize - rsize;
				return *this;
			}
			if (pos >= _usize) // add
				return append(pbuf, usize);
			if (pos + rsize >= _usize) {//outof end
				_usize = pos;
				return append(pbuf, usize);
			}
			if (usize > rsize)
				_grown(usize - rsize);

			if (rsize != usize)
				memmove(_pbuf + pos + usize, _pbuf + pos + rsize, (_usize - (pos + rsize)) * sizeof(value_type));
			memcpy(_pbuf + pos, pbuf, usize * sizeof(value_type));
			_usize = _usize + usize - rsize;
			return  *this;
		}

		bool insert(size_type pos, const value_type *pdatas, size_type n)
		{
			try {
				_insert(pos, pdatas, n);
			}
			catch (...) {
				return false;
			}
			return true;
		}

		iterator myerase(size_type pos, size_type size = 1)
		{
			if (!size || !_pbuf || pos >= _usize)
				return end();
			if (pos + size >= _usize)
				_usize = pos;
			else {
				memmove(_pbuf + pos, _pbuf + pos + size, (_usize - (pos + size)) * sizeof(value_type));
				_usize -= size;
			}
			return _pbuf + pos;
		}

		value_type& top() noexcept
		{
			return _pbuf[_usize - 1];
		}

		const value_type& top() const noexcept
		{
			return _pbuf[_usize - 1];
		}

		inline void push(const value_type& val)
		{
			push_back(val);
		}

		inline void pop() noexcept
		{
			pop_back();
		}

		const char* c_str() noexcept
		{
			if (_usize == _ubufsize) {
				value_type v = 0;
				push_back(v);
				pop_back();
			}
			*((char*)_pbuf + _usize * sizeof(value_type)) = '\0';
			return (const char*)_pbuf;
		}

		template<typename T
			, class = typename std::enable_if<sizeof(_Tp) == 1 && std::is_same<T, char>::value>::type>
			inline vector &append(const T* s)
		{
			if (!s)
				return *this;
			return append((const value_type*)s, strlen(s));
		}

		template<typename T
			, class = typename std::enable_if<sizeof(_Tp) == 1 && (std::is_arithmetic<T>::value || std::is_void<T>::value)>::type >
			inline vector &append(const T* p, size_t size)
		{
			return append((const value_type*)p, size);
		}

		vector &assign(const value_type* p, size_t size)
		{
			clear();
			return append(p, size);
		}

		template<typename T
			, class = typename std::enable_if<sizeof(_Tp) == 1 && std::is_same<T, char>::value>::type>
			vector &assign(const T* s)
		{
			clear();
			if (!s)
				return *this;
			return append((const value_type*)s, strlen(s));
		}

		template<typename T
			, class = typename std::enable_if<sizeof(_Tp) == 1 && std::is_same<T, char>::value>::type>
			vector& operator= (const T* s)
		{
			clear();
			if (!s)
				return *this;
			return append((const value_type*)s, strlen(s));
		}

		template<typename T
			, class = typename std::enable_if<sizeof(_Tp) == 1 && std::is_same<T, char>::value>::type>
			vector& operator+= (const T* s)
		{
			if (!s)
				return *this;
			return append((const value_type*)s, strlen(s));
		}

		inline vector& operator+= (value_type c)
		{
			push_back(c);
			return *this;
		}

		inline vector& operator+= (const vector &v)
		{
			return append(v.data(), v.size());
		}

#ifdef _WIN32
		bool printf(const char * format, ...)
#else
		bool printf(const char * format, ...) __attribute__((format(printf, 2, 3)))
#endif
		{
			_pos = 0;
			_usize = 0;
			try {
				if (!_ubufsize)
					reserve(128);
			}
			catch (...) {
				return false;
			}
			int n = 0;
			{
				va_list arg_ptr;
				va_start(arg_ptr, format);
				n = vsnprintf(_pbuf, _ubufsize, format, arg_ptr);
				va_end(arg_ptr);
			}
			if (n < 0)
				return false;
			if (n < (int)_ubufsize) {
				_usize = n;
				return true;
			}

			try {
				reserve(n + 1);
			}
			catch (...) {
				return false;
			}
			{
				va_list arg_ptr;
				va_start(arg_ptr, format);
				n = vsnprintf(_pbuf, _ubufsize, format, arg_ptr);
				va_end(arg_ptr);
			}
			if (n > 0 && n < (int)_ubufsize) {
				_usize = n;
				return true;
			}
			return false;
		}
	public: //extend stream for sizeof(value_type) == 1
		static bool is_be() // is big endian
		{
			union {
				uint32_t u32;
				uint8_t u8;
			} ua;
			ua.u32 = 0x01020304;
			return ua.u8 == 0x01;
		}

		vector & setpos(size_t pos) noexcept
		{
			_pos = pos > _usize ? _usize : pos;
			return *this;
		}

		inline void postoend() noexcept
		{
			_pos = _usize;
		}

		inline size_t getpos() const noexcept
		{
			return _pos;
		}

		template < typename T
			, class = typename std::enable_if<sizeof(_Tp) == 1 && (std::is_arithmetic<T>::value || std::is_void<T>::value)>::type>
			vector & read(T* pdata, size_t size)
		{ // read block from current positiion
			if (_pos + size > _usize)
				throw std::range_error("oversize");
			memcpy(pdata, (const char*)_pbuf + _pos, size);
			_pos += size;
			return *this;
		}

		template < typename T
			, class = typename std::enable_if<sizeof(_Tp) == 1 && (std::is_arithmetic<T>::value || std::is_void<T>::value)>::type>
			vector & write(const T* pdata, size_t size)
		{ // write block to current positiion
			if (_pos + size > _usize)
				_grown(_pos + size - _usize);
			memcpy((char*)_pbuf + _pos, pdata, size);
			_pos += size;
			if (_usize < _pos)
				_usize = _pos;
			return *this;
		}

		template < typename T
			, class = typename std::enable_if<sizeof(_Tp) == 1 && std::is_arithmetic<T>::value>::type>
			vector & operator >> (T& v)
		{ // read as little_endian from current positiion
			if (_pos + sizeof(T) > _usize * sizeof(value_type))
				throw std::range_error("oversize");
			if (!is_be())
				memcpy(&v, (const char*)_pbuf + _pos, sizeof(T));
			else {
				int n = sizeof(T) - 1;
				uint8_t *ps = (uint8_t *)_pbuf + _pos, *pd = ((uint8_t*)&v) + n;
				while (n >= 0) {
					*pd-- = *ps++;
					n--;
				}
			}
			_pos += sizeof(T);
			return *this;
		}

		template < typename T
			, class = typename std::enable_if<sizeof(_Tp) == 1 && std::is_arithmetic<T>::value>::type>
			vector & operator > (T & v)
		{ // read as big_endian from current positiion
			if (_pos + sizeof(T) > _usize)
				throw std::range_error("oversize");
			if (is_be())
				memcpy(&v, (const char*)_pbuf + _pos, sizeof(T));
			else {
				int n = sizeof(T) - 1;
				uint8_t *ps = (uint8_t *)_pbuf + _pos, *pd = ((uint8_t*)&v) + n;
				while (n >= 0) {
					*pd-- = *ps++;
					n--;
				}
			}
			_pos += sizeof(T);
			return *this;
		}

		template < typename T
			, class = typename std::enable_if<sizeof(_Tp) == 1 && std::is_arithmetic<T>::value>::type>
			vector & operator << (T v)
		{ // write as little_endian to current positiion
			if (_pos + sizeof(T) > _usize)
				_grown(_pos + sizeof(T) - _usize);

			if (!is_be())
				memcpy((char*)_pbuf + _pos, &v, sizeof(T));
			else {
				int n = sizeof(T) - 1;
				uint8_t *pd = (uint8_t *)_pbuf + _pos, *ps = ((uint8_t*)&v) + n;
				while (n >= 0) {
					*pd++ = *ps--;
					n--;
				}
			}
			_pos += sizeof(T);
			if (_usize < _pos)
				_usize = _pos;
			return *this;
		}

		template < typename T
			, class = typename std::enable_if<sizeof(_Tp) == 1 && std::is_arithmetic<T>::value>::type>
			vector & operator < (T v)
		{ // write as big_endian to current positiion
			if (_pos + sizeof(T) > _usize)
				_grown(_pos + sizeof(T) - _usize);

			if (is_be())
				memcpy((char*)_pbuf + _pos, &v, sizeof(T));
			else {
				int n = sizeof(T) - 1;
				uint8_t *pd = (uint8_t *)_pbuf + _pos, *ps = ((uint8_t*)&v) + n;
				while (n >= 0) {
					*pd++ = *ps--;
					n--;
				}
			}
			_pos += sizeof(T);
			if (_usize < _pos)
				_usize = _pos;
			return *this;
		}
	};
}
