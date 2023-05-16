/*!
\file ec_stream.h
\author	jiangyong
\email  kipway@outlook.com
\update 
2023.5.15 add vstream

stream
	memery stream class

eclib 3.0 Copyright (c) 2017-2023, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/

#pragma once
#include <stdint.h>
#include <memory.h>
#include <type_traits>
#include "ec_string.hpp"
namespace ec
{
	/*!
	*Note: overloaded "<,>" and "<<, >>" do not mix in one line because the priority is different
	*/
	class stream
	{
	public:
		stream() : _pos(0), _size(0), _ps(nullptr)
		{
		};
		stream(void* p, size_t size) : stream()
		{
			attach(p, size);
		};
		~stream() {};

		inline bool is_be()
		{
			union {
				uint32_t u32;
				uint8_t u8;
			} ua;
			ua.u32 = 0x01020304;
			return ua.u8 == 0x01;
		}
	public:
		void attach(void* p, size_t size)
		{
			_ps = (uint8_t*)p;
			_size = size;
			_pos = 0;
		}
		template < typename T,
			class = typename std::enable_if<std::is_arithmetic<T>::value>::type>
			stream & operator >> (T& v) // read as little_endian
		{
			if (_pos + sizeof(T) > _size)
				throw (int)1;
			if (!is_be())
				memcpy(&v, _ps + _pos, sizeof(T));
			else {
				int n = sizeof(T) - 1;
				uint8_t *ps = _ps + _pos, *pd = ((uint8_t*)&v) + n;
				while (n >= 0) {
					*pd-- = *ps++;
					n--;
				}
			}
			_pos += sizeof(T);
			return *this;
		}

		template < typename T,
			class = typename std::enable_if<std::is_arithmetic<T>::value>::type>
			stream & operator << (T v) // write as little_endian
		{
			if (_pos + sizeof(T) > _size)
				throw (int)1;
			if (!is_be())
				memcpy(_ps + _pos, &v, sizeof(T));
			else {
				int n = sizeof(T) - 1;
				uint8_t *pd = _ps + _pos, *ps = ((uint8_t*)&v) + n;
				while (n >= 0) {
					*pd++ = *ps--;
					n--;
				}
			}
			_pos += sizeof(T);
			return *this;
		}

		template < typename T,
			class = typename std::enable_if<std::is_arithmetic<T>::value>::type>
			stream & operator > (T& v) // read as big_endian
		{
			if (_pos + sizeof(T) > _size)
				throw (int)1;
			if (is_be())
				memcpy(&v, _ps + _pos, sizeof(T));
			else {
				int n = sizeof(T) - 1;
				uint8_t *ps = _ps + _pos, *pd = ((uint8_t*)&v) + n;
				while (n >= 0) {
					*pd-- = *ps++;
					n--;
				}
			}
			_pos += sizeof(T);
			return *this;
		}

		template < typename T,
			class = typename std::enable_if<std::is_arithmetic<T>::value>::type>
			stream & operator < (T v)  // write as big_endian
		{
			if (_pos + sizeof(T) > _size)
				throw (int)1;
			if (is_be())
				memcpy(_ps + _pos, &v, sizeof(T));
			else {
				int n = sizeof(T) - 1;
				uint8_t *pd = _ps + _pos, *ps = ((uint8_t*)&v) + n;
				while (n >= 0) {
					*pd++ = *ps--;
					n--;
				}
			}
			_pos += sizeof(T);
			return *this;
		}

		stream & read(void* pbuf, size_t size)
		{
			if (_pos + size > _size)
				throw (int)1;
			memcpy(pbuf, _ps + _pos, size);
			_pos += size;
			return *this;
		};

		stream & write(const void* pbuf, size_t size)
		{
			if(!pbuf || !size)
				return *this;
			if (_pos + size > _size)
				throw (int)1;
			memcpy(_ps + _pos, pbuf, size);
			_pos += size;
			return *this;
		};

		stream & readstr(char* pbuf, size_t size)
		{
			if (!size)
				throw (int)2;
			size_t n = 0;
			while (_pos < _size && _ps[_pos]) {
				if (n + 1 < size) {
					pbuf[n] = _ps[_pos];
					n++;
				}
				_pos++;
			}
			pbuf[n] = 0;
			_pos++;
			return *this;
		};

		stream & writestr(const char* pbuf)
		{
			if (!pbuf || !*pbuf)
				return *this;
			size_t n = 0;
			if (pbuf)
				n = strlen(pbuf);
			if (_pos + n + 1 >= _size)
				throw (int)1;
			if (pbuf && n > 0) {
				memcpy(_ps + _pos, pbuf, n);
				_pos += n;
			}
			_ps[_pos] = 0;
			_pos++;
			return *this;
		};

		stream & setpos(size_t pos)
		{
			if (pos > _size)
				throw (int)1;
			_pos = pos;
			return *this;
		};

		inline size_t getpos() const
		{
			return _pos;
		};
		inline size_t leftsize()
		{
			return _size - _pos;
		}
		inline void* getp()
		{
			return _ps;
		};
		inline bool iseof()
		{
			return _pos == _size;
		}
		inline size_t size() const
		{
			return _size;
		}
	protected:
		size_t	_pos;
		size_t	_size;
		uint8_t* _ps;
	};

	class vstream : public string_< ec_string_alloctor, uint32_t, uint8_t>
	{
	protected:
		size_t	_pos;  // read write as stream
	public:
		static bool is_be() // is big endian
		{
			union {
				uint32_t u32;
				uint8_t u8;
			} ua;
			ua.u32 = 0x01020304;
			return ua.u8 == 0x01;
		}
		vstream() : _pos(0) {
		}
		vstream& setpos(size_t pos) noexcept
		{
			size_t usize = size();
			_pos = pos > usize ? usize : pos;
			return *this;
		}

		inline void postoend() noexcept
		{
			_pos = size();
		}

		inline size_t getpos() const noexcept
		{
			return _pos;
		}

		vstream& read(void* pdata, size_t rsize)
		{ // read block from current positiion
			if (_pos + rsize > size()) {
				throw std::range_error("oversize");
				return *this;
			}
			memcpy(pdata, data() + _pos, rsize);
			_pos += rsize;
			return *this;
		}

		vstream& write(const void *pdata, size_t wsize)
		{ // write block to current positiion
			if (_pos + wsize > size()) {
				resize(_pos + wsize);
			}
			memcpy(data() + _pos, pdata, wsize);
			_pos += wsize;
			return *this;
		}

		template < typename T>
		vstream& operator >> (T& v)
		{ // read as little_endian from current positiion
			if (_pos + sizeof(T) > size())
				throw std::range_error("oversize");
			if (!is_be())
				memcpy(&v, data() + _pos, sizeof(T));
			else {
				int n = sizeof(T) - 1;
				uint8_t* ps = (uint8_t*)data() + _pos, * pd = ((uint8_t*)&v) + n;
				while (n >= 0) {
					*pd-- = *ps++;
					n--;
				}
			}
			_pos += sizeof(T);
			return *this;
		}

		template < typename T>
		vstream& operator > (T& v)
		{ // read as big_endian from current positiion
			if (_pos + sizeof(T) > size())
				throw std::range_error("oversize");
			if (is_be())
				memcpy(&v, data() + _pos, sizeof(T));
			else {
				int n = sizeof(T) - 1;
				uint8_t* ps = (uint8_t*)data() + _pos, * pd = ((uint8_t*)&v) + n;
				while (n >= 0) {
					*pd-- = *ps++;
					n--;
				}
			}
			_pos += sizeof(T);
			return *this;
		}

		template < typename T>
		vstream& operator << (T v)
		{ // write as little_endian to current positiion
			if (_pos + sizeof(T) > size())
				resize(_pos + sizeof(T));
			if (!is_be())
				memcpy(data() + _pos, &v, sizeof(T));
			else {
				int n = sizeof(T) - 1;
				uint8_t* pd = (uint8_t*)data() + _pos, * ps = ((uint8_t*)&v) + n;
				while (n >= 0) {
					*pd++ = *ps--;
					n--;
				}
			}
			_pos += sizeof(T);
			return *this;
		}

		template < typename T>
		vstream& operator < (T v)
		{ // write as big_endian to current positiion
			if (_pos + sizeof(T) > size())
				resize(_pos + sizeof(T));
			if (is_be())
				memcpy(data() + _pos, &v, sizeof(T));
			else {
				int n = sizeof(T) - 1;
				uint8_t* pd = (uint8_t*)data() + _pos, * ps = ((uint8_t*)&v) + n;
				while (n >= 0) {
					*pd++ = *ps--;
					n--;
				}
			}
			_pos += sizeof(T);
			return *this;
		}
	};
};
