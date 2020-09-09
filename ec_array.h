/*!
\file ec_array.h
\author	jiangyong
\email  kipway@outlook.com
\update 2020.9.6

array
	a extended array class for trivially copyable type

eclib 3.0 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

Description£º
ec::array is not same as std::array. it's like a vector with fixed-size capacity.And expanded some functions, can be used as string, stack, stream

iterator: a random access iterator to value_type

remark£º

size_type size() const;
Returns the number of elements in the array.
This is the number of actual objects held in the array, which is not necessarily equal to its storage capacity.

size_type max_size() noexcept;
Return maximum size
Returns the maximum number of elements that the array container can hold.
The max_size of an array object, just like its size, is always equal to the second template parameter used to instantiate the array template class.

size_type capacity() const;
same as max_size();

demo:
using modbusfrm = ec::array<uint8_t,264>;
using str256 = ec::array<char,256>;
*/
#pragma once
#include <stdint.h>
#include <memory.h>
#include <type_traits>
#include <stdexcept>
namespace ec {
	template<typename _Tp, size_t _Num>
	class array {
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
		array() : _size(0), _pos(0) {
		}

		array(const value_type* s, size_type size) : array()
		{
			if (s && size && size < _Num)
				_append(s, size);
		}

		template<typename T
			, class = typename std::enable_if<sizeof(_Tp) == 1 && std::is_same<T, char>::value>::type>
			array(const T* s)
		{
			clear();
			if (s)
				_append((const value_type*)s, strlen(s));
		}

		array& operator = (const array& v)
		{
			clear();
			_append(v.data(), v.size());
			return *this;
		}
	protected:
		size_type _size; // current items
		size_type _pos;    // stream byte position for read/write
		value_type _data[_Num]; // buffer, _Num is buffer items

	public: // Iterators
		inline iterator begin() noexcept
		{
			return &_data[0];
		}
		inline iterator end() noexcept
		{
			return &_data[0] + _size;
		}

		inline const_iterator cbegin() const
		{
			return (const_iterator)&_data[0];
		}

		inline const_iterator cend() const
		{
			return (const_iterator)&_data[0] + _size;
		}

	public: // Capacity
		inline size_type size() const noexcept
		{
			return _size;
		}

		inline size_type capacity() const noexcept
		{
			return _Num;
		}

		inline size_type max_size() const noexcept
		{
			return _Num;
		}

		inline bool empty() const noexcept
		{
			return !_size;
		}

		inline bool full() const noexcept
		{
			return _size >= _Num;
		}

		void reserve(size_type n)
		{
		}

		void resize(size_type n)
		{
			if (n <= _Num)
				_size = n;
		}
	public: // Element access

		reference operator [](size_type pos)
		{
			return _data[pos];
		}

		const_reference operator [](size_type pos) const
		{
			return _data[pos];
		}

		reference at(size_type pos)
		{
			if (pos >= _Num)
				throw std::range_error("range error");
			return _data[pos];
		}

		const_reference at(size_type pos) const noexcept
		{
			if (pos >= _Num)
				throw std::range_error("range error");
			return _data[pos];
		}

		inline reference front() noexcept
		{
			return _data[0];
		}

		inline const_reference front() const noexcept
		{
			return _data[0];
		}

		inline reference back() noexcept
		{
			return _data[_size - 1];
		}

		const_reference back() const noexcept
		{
			return _data[_size - 1];
		}

		inline pointer data() noexcept
		{
			return &_data[0];
		}
		inline const_pointer data() const noexcept
		{
			return &_data[0];
		}

	public: //Modifiers:
		inline void clear() noexcept
		{
			_size = 0;
			_pos = 0;
		}

		void fill(const value_type &v) noexcept
		{
			for (auto i = i; i < _Num; i++)
				_data[i] = v;
			_size = _Num;
		}

	public: // extend
		bool _append(const value_type *pbuf, size_type usize)
		{
			if (!usize || !pbuf)
				return true;
			if (_size + usize > _Num)
				return false;
			memcpy(&_data[_size], pbuf, usize * sizeof(value_type));
			_size += usize;
			return true;
		}

		array& append(const value_type *pbuf, size_type usize)
		{ //like std::string::append
			if (_append(pbuf, usize))
				return *this;
			throw std::range_error("range error");
		};

		void push_back(const value_type& val) noexcept
		{ //like std::vector::push_back
			if (_size < _Num)
				_data[_size++] = val;
		}

		void pop_back() noexcept
		{ //like std::vector::pop_back
			if (_size > 0)
				_size--;
		}

		inline void push(const value_type& val) noexcept
		{ // like std::stack::push
			push_back(val);
		}

		void pop() noexcept
		{ // like std::stack::pop
			if (_size > 0)
				_size--;
		}

		inline reference top() noexcept
		{ // like std::stack::top
			return back();
		}

		const_reference top() const noexcept
		{ // like std::stack::top
			return back();
		}

		void erase(size_type pos, size_type size = 1) noexcept
		{
			if (!size || pos >= _size)
				return;
			if (pos + size >= _size) {
				_size = pos;
				return;
			}
			memmove(&_data[pos], &_data[pos + size], (_size - (pos + size)) * sizeof(value_type));
			_size -= size;
		}

		void setsize(size_t size)
		{
			resize(size);
		}

		bool insert(size_type pos, const value_type *pval, size_t insize = 1) noexcept // insert before
		{
			if (_size + insize > _Num || !insize || !pval)
				return false;
			if (pos >= _size)
				memcpy(&_data[_size], pval, insize * sizeof(value_type));
			else {
				memmove(&_data[pos] + insize, &_data[pos], (_size - pos) * sizeof(value_type));
				memcpy(&_data[pos], pval, insize * sizeof(value_type));
			}
			_size += insize;
			return true;
		}

		const char* c_str() noexcept
		{ // like std::string::c_str
			if (_size == _Num)
				pop_back();
			*((char*)_data + _size * sizeof(value_type)) = '\0';
			return (const char*)_data;
		}

		template<typename T
			, class = typename std::enable_if<sizeof(_Tp) == 1 && std::is_same<T, char>::value>::type>
			array &append(const T* s)
		{
			return append((const value_type*)s, strlen(s));
		}

		template<typename T
			, class = typename std::enable_if<sizeof(_Tp) == 1 && (std::is_arithmetic<T>::value || std::is_void<T>::value)>::type >
			array &append(const T * s, size_type size)
		{
			return append((const value_type*)s, size);
		}

		array &assign(const value_type* p, size_t size)
		{
			clear();
			return append(p, size);
		}

		template<typename T
			, class = typename std::enable_if<sizeof(_Tp) == 1 && std::is_same<T, char>::value>::type>
			array &assign(const T* s)
		{
			clear();
			if (!s)
				return *this;
			return append((const value_type*)s, strlen(s));
		}

		template<typename T
			, class = typename std::enable_if<sizeof(_Tp) == 1 && std::is_same<T, char>::value>::type>
			array& operator= (const T* s)
		{
			clear();
			if (!s)
				return *this;
			return append((const value_type*)s, strlen(s));
		}

		template<typename T
			, class = typename std::enable_if<sizeof(_Tp) == 1 && std::is_same<T, char>::value>::type>
			array& operator+= (const T* s)
		{
			if (!s)
				return *this;
			return append((const value_type*)s, strlen(s));
		}

		array& operator+= (value_type c)
		{
			push_back(c);
			return *this;
		}

	public: // use as stream for sizeof(value_type) == 1

		static bool is_be() // is big endian
		{
			union {
				uint32_t u32;
				uint8_t u8;
			} ua;
			ua.u32 = 0x01020304;
			return ua.u8 == 0x01;
		}

		array & setpos(size_type pos)
		{
			_pos = pos > _size ? _size : pos;
			return *this;
		};

		array &postoend()
		{
			_pos = _size;
			return *this;
		};

		inline size_type getpos() const
		{
			return _pos;
		};

		template < typename T
			, class = typename std::enable_if<sizeof(_Tp) == 1 && (std::is_arithmetic<T>::value || std::is_void<T>::value)>::type>
			array & read(T* pbuf, size_type size)
		{
			if (_pos + size > _size)
				throw std::range_error("oversize");
			memcpy(pbuf, (const uint8_t*)_data + _pos, size);
			_pos += size;
			return *this;
		};

		template < typename T
			, class = typename std::enable_if<sizeof(_Tp) == 1 && (std::is_arithmetic<T>::value || std::is_void<T>::value)>::type>
			array & write(const T* pbuf, size_type size)
		{
			if (_pos + size > _Num)
				throw std::range_error("oversize");
			memcpy((uint8_t*)_data + _pos, pbuf, size);
			_pos += size;
			if (_size < _pos)
				_size = _pos;
			return *this;
		};

		template < typename T
			, class = typename std::enable_if<sizeof(_Tp) == 1 && std::is_arithmetic<T>::value>::type>
			array & operator >> (T& v)
		{ // read as little_endian
			if (_pos + sizeof(T) > _size)
				throw std::range_error("oversize");
			if (!is_be())
				memcpy(&v, (const uint8_t*)_data + _pos, sizeof(T));
			else {
				int n = sizeof(T) - 1;
				uint8_t *ps = (uint8_t*)_data + _pos, *pd = ((uint8_t*)&v) + n;
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
			array & operator > (T & v)
		{  // read as big_endian
			if (_pos + sizeof(T) > _size)
				throw std::range_error("oversize");
			if (is_be())
				memcpy(&v, (const uint8_t*)_data + _pos, sizeof(T));
			else {
				int n = sizeof(T) - 1;
				uint8_t *ps = (uint8_t*)_data + _pos, *pd = ((uint8_t*)&v) + n;
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
			array & operator << (T v)
		{  // write as little_endian
			if (_pos + sizeof(T) > _Num)
				throw std::range_error("oversize");

			if (!is_be())
				memcpy((uint8_t*)_data + _pos, &v, sizeof(T));
			else {
				int n = sizeof(T) - 1;
				uint8_t *pd = (uint8_t*)_data + _pos, *ps = ((uint8_t*)&v) + n;
				while (n >= 0) {
					*pd++ = *ps--;
					n--;
				}
			}
			_pos += sizeof(T);
			if (_size < _pos)
				_size = _pos;
			return *this;
		}

		template < typename T
			, class = typename std::enable_if<sizeof(_Tp) == 1 && std::is_arithmetic<T>::value>::type>
			array & operator < (T v)
		{   // write as big_endian
			if (_pos + sizeof(T) > _Num)
				throw std::range_error("oversize");
			if (is_be())
				memcpy((uint8_t*)_data + _pos, &v, sizeof(T));
			else {
				int n = sizeof(T) - 1;
				uint8_t *pd = (uint8_t*)_data + _pos, *ps = ((uint8_t*)&v) + n;
				while (n >= 0) {
					*pd++ = *ps--;
					n--;
				}
			}
			_pos += sizeof(T);
			if (_size < _pos)
				_size = _pos;
			return *this;
		}
	};
}
