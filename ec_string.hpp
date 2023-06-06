/*!
\file ec_string.hpp
\author	jiangyong
\email  kipway@outlook.com
\update 
2023.5.25 add fixstring_
2023.5.13 use ec_malloc

ec basic string

eclib 3.0 Copyright (c) 2017-2023, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/

#pragma once
#include <memory.h>
#include <malloc.h>
#include <string>
#include <ctype.h>
#include <type_traits>
#include "ec_alloctor.h"
namespace ec
{
	struct null_stralloctor { // use C malloc
		void* realloc_(void* ptr, size_t zr, size_t* poutsize) {
			if (zr % 16u)
				zr += 16u - zr % 16u;
			if (zr + zr / 2u < UINT32_MAX / 2u - 1024u)
				zr += zr / 2u; // 1.5x size
			if (zr < 32u)
				zr = 32u;
			if (poutsize)
				*poutsize = zr;
			return ::realloc(ptr, zr);
		}
		void free_(void* p) {
			::free(p);
		}
	};
	struct ec_string_alloctor { // use ec_alloctor
		void* realloc_(void* ptr, size_t size, size_t* poutsize)
		{
			if (size > ec_maxblksize()) { // use ::malloc, 1.5x size
				size = (size % 8) ? (size + 8u - size % 8u) : size;
				return ec_realloc(ptr, size + size / 2u, poutsize);
			}
			return ec_realloc(ptr, size, poutsize);
		}

		inline void free_(void* p) {
			ec_free(p);
		}
	};
	template<class _Alloctor = null_stralloctor,
		typename size_type = uint32_t,
		typename chart = char,
		class = typename std::enable_if<sizeof(chart) == 1>::type> // just support char and unsigned char
	class string_ // basic_string
	{
	public:
		using value_type = chart;
		using pointer = chart*;
		using const_pointer = const chart*;
		using iterator = chart*;
		using const_iterator = const chart*;
		using reference = chart&;
		using const_reference = const chart&;

		struct t_h {
			size_type sizebuf; // not include head
			size_type sizedata;// not include null
		};
		static const size_t npos = -1;
	private:
		pointer _pstr; // point to data
	private:
		pointer srealloc(size_t strsize)
		{
			if (strsize > max_size())
				return nullptr;
			pointer pold = _pstr ? _pstr - sizeof(t_h) : nullptr;
			size_t zr = strsize + sizeof(t_h) + 1;
			pointer pstr = (pointer)_Alloctor().realloc_(pold, zr, &zr);
			if (pstr) {
				t_h* ph = (t_h*)pstr;
				ph->sizebuf = (size_type)(zr - sizeof(t_h));
				if (!pold)
					ph->sizedata = 0;
				pstr += sizeof(t_h);
			}
			return pstr;
		}
		void sfree(pointer& str)
		{
			if (str) {
				_Alloctor().free_(str - sizeof(t_h));
				str = nullptr;
			}
		}
		bool recapacity(size_t strsize)
		{
			if (!strsize) {
				sfree(_pstr);
				return true;
			}
			if (strsize <= capacity())
				return true;
			pointer pnew = srealloc(strsize);
			if (!pnew)
				return false;
			_pstr = pnew;
			return true;
		}
		void setsize_(pointer pstr, size_t zlen)
		{
			if (!pstr)
				return;
			t_h* ph = (t_h*)(pstr - sizeof(t_h));
			ph->sizedata = (size_type)zlen;
		}
		size_t ssize(const_pointer pstr) const
		{
			if (!pstr)
				return 0;
			const t_h* ph = (const t_h*)(pstr - sizeof(t_h));
			return ph->sizedata;
		}
		size_t scapacity(const_pointer pstr) const
		{
			if (!pstr)
				return 0;
			const t_h* ph = (const t_h*)(pstr - sizeof(t_h));
			return ph->sizebuf - 1;
		}
	public:
		string_() : _pstr(nullptr) {
		}
		string_(const char* s) : _pstr(nullptr)
		{
			if (!s || !*s)
				return;
			append(s, strlen(s));
		}
		string_(const_pointer s, size_t size) : _pstr(nullptr)
		{
			append(s, size);
		}
		string_(const string_& str) : _pstr(nullptr)
		{
			append(str.data(), str.size());
		}
		template<typename _Str, class = typename std::enable_if<std::is_class<_Str>::value>::type>
		string_(const _Str& str) : _pstr(nullptr)
		{
			append(str.data(), str.size());
		}
		~string_() {
			sfree(_pstr);
		}
		string_(string_<_Alloctor, size_type>&& str) noexcept // move construct
		{
			_pstr = str._pstr;
			str._pstr = nullptr;
		}
		string_& operator= (string_<_Alloctor, size_type>&& v) // for move
		{
			sfree(_pstr);
			_pstr = v._pstr;
			v._pstr = nullptr;
			return *this;
		}
		void swap(string_<_Alloctor, size_type>& str) //simulate move
		{
			pointer stmp = _pstr;
			_pstr = str._pstr;
			str._pstr = stmp;
		}
		template<typename _Str, class = typename std::enable_if<std::is_class<_Str>::value>::type>
		string_& operator= (const _Str& str)
		{
			clear();
			return append(str.data(), str.size());
		}
		inline string_& operator= (const string_& str)
		{
			clear();
			return append(str.data(), str.size());
		}
		inline string_& operator= (const char* s)
		{
			clear();
			return append(s);
		}
		string_& operator= (char c)
		{
			clear();
			if (recapacity(1)) {
				_pstr[0] = c;
				setsize_(_pstr, 1);
			}
			return *this;
		}
	public: //Iterators
		inline iterator begin() noexcept
		{
			return _pstr;
		}
		inline const_iterator begin() const noexcept
		{
			return _pstr;
		}
		inline iterator end() noexcept
		{
			return _pstr ? _pstr + size() : nullptr;
		}
		inline const_iterator end() const noexcept
		{
			return _pstr ? _pstr + size() : nullptr;
		}
		inline const_iterator cbegin() const noexcept
		{
			return _pstr;
		}
		inline const_iterator cend() const noexcept
		{
			return _pstr ? _pstr + size() : nullptr;
		}
	public: // Capacity
		static inline size_t max_size()
		{
			return static_cast<size_type>(-1) - 32u;
		}
		inline size_t size() const noexcept
		{
			return ssize(_pstr);
		}
		inline size_t length() const noexcept
		{
			return ssize(_pstr);
		}
		inline size_t capacity() const noexcept
		{
			return scapacity(_pstr);
		}
		inline void reserve(size_t n = 0) noexcept
		{
			recapacity(n);
		}
		void shrink_to_fit()
		{
			size_t zlen = ssize(_pstr);
			if (!zlen) {
				sfree(_pstr);
				return;
			}
			size_t zcap = capacity();
			if (zlen > zcap / 2 || zcap < 1024 * 18) // No need to adjust
				return;
			// todo 
		}
		inline void clear() noexcept
		{
			setsize_(_pstr, 0);
		}
		inline bool empty() const noexcept
		{
			return !size();
		}
	public: //Element access
		reference operator[] (size_t pos) noexcept
		{
			return _pstr[pos];
		}
		const_reference operator[] (size_t pos) const noexcept
		{
			return _pstr[pos];
		}
		reference at(size_t pos) noexcept
		{
			return _pstr[pos];
		}
		const_reference at(size_t pos) const noexcept
		{
			return _pstr[pos];
		}
	public: // String operations:
		const_pointer data() const noexcept
		{
			return _pstr;
		}
		pointer data() noexcept
		{
			return _pstr;
		}
		const_pointer c_str() const noexcept
		{
			if (!_pstr)
				return "";// never return nullptr
			_pstr[size()] = 0;
			return (const char*)_pstr;
		}
	public: //Modifiers
		string_& append(const_pointer s, size_t n) noexcept
		{
			if (!s || !n)
				return *this;
			size_t zs = size();
			if (recapacity(zs + n)) {
				memcpy(_pstr + zs, s, n);
				setsize_(_pstr, zs + n);
			}
			return *this;
		}
		string_& append(const void* s, size_t n) noexcept
		{
			if (!s || !n)
				return *this;
			size_t zs = size();
			if (recapacity(zs + n)) {
				memcpy(_pstr + zs, s, n);
				setsize_(_pstr, zs + n);
			}
			return *this;
		}
		template<typename _Str, class = typename std::enable_if<std::is_class<_Str>::value>::type>
		inline string_& append(const _Str& str) noexcept
		{
			return append(str.data(), str.size());
		}
		string_& append(const char* s) noexcept
		{
			if (!s || !*s)
				return *this;
			return append(s, strlen(s));
		}
		string_& append(value_type c) noexcept
		{
			size_t zs = size();
			if (recapacity(zs + 1)) {
				_pstr[zs] = c;
				setsize_(_pstr, zs + 1);
			}
			return *this;
		}
		string_& assign(string_<_Alloctor, size_type>&& v) noexcept
		{
			sfree(_pstr);
			_pstr = v._pstr;
			v._pstr = nullptr;
			return *this;
		}
		inline string_& assign(const_pointer s, size_t n) noexcept
		{
			clear();
			return append(s, n);
		}
		string_& assign(const char* s) noexcept
		{
			clear();
			if (!s || !*s)
				return *this;
			return append(s, strlen(s));
		}
		template<typename _Str, class = typename std::enable_if<std::is_class<_Str>::value>::type>
		inline string_& assign(const _Str& str)
		{
			clear();
			return append(str.data(), str.size());
		}
		template<typename _Str, class = typename std::enable_if<std::is_class<_Str>::value>::type>
		inline string_& operator+= (const _Str& str)
		{
			return append(str.data(), str.size());
		}
		inline string_& operator+= (const char* s) noexcept
		{
			return append(s);
		}
		inline string_& operator+= (value_type c) noexcept
		{
			return append(c);
		}
		inline void push_back(value_type c) noexcept
		{
			append(c);
		}
		void pop_back() noexcept
		{
			size_t zlen = size();
			if (zlen > 0)
				setsize_(_pstr, zlen - 1);
		}
		inline const_reference back() const
		{
			return _pstr[size() - 1];
		}
		inline reference back()
		{
			return _pstr[size() - 1];
		}
		void resize(size_t n) noexcept
		{
			size_t zlen = size();
			if (n < zlen)
				setsize_(_pstr, n);
			else if (n > zlen) {
				if (recapacity(n)) {
					setsize_(_pstr, n);
				}
			}
		}
		void resize(size_t n, value_type c) noexcept
		{
			size_t zlen = size();
			if (n < zlen)
				setsize_(_pstr, n);
			else if (n > zlen) {
				if (recapacity(n)) {
					memset(_pstr + zlen, c, n - zlen);
					setsize_(_pstr, n);
				}
			}
		}
		string_& insert(size_t pos, size_t n, value_type c)
		{
			if (!n)
				return *this;
			size_t zlen = size();
			if (pos >= zlen)
				return append(c);
			if (zlen + n > capacity()) {
				pointer pnewstr = srealloc(zlen + n);
				if (!pnewstr)
					return *this;
				_pstr = pnewstr;
			}
			memmove(_pstr + pos + n, _pstr + pos, zlen - pos);
			if (1 == n) {
				*(_pstr + pos) = c;
			}
			else {
				pointer p = _pstr + pos, pend = _pstr + pos + n;
				while (p < pend)
					*p++ = c;
			}
			setsize_(_pstr, zlen + n);
			return *this;
		}

		string_& insert(size_t pos, const_pointer s, size_t n) noexcept
		{
			if (!s || !*s || !n)
				return *this;
			size_t zlen = size();
			if (pos >= zlen)
				return append(s, n);
			if (zlen + n > capacity()) {
				pointer pnewstr = srealloc(zlen + n);
				if (!pnewstr)
					return *this;
				_pstr = pnewstr;
			}
			memmove(_pstr + pos + n, _pstr + pos, zlen - pos);
			memcpy(_pstr + pos, s, n);
			setsize_(_pstr, zlen + n);
			return *this;
		}
		string_& insert(size_t pos, const char* s) noexcept
		{
			if (!s || !*s)
				return *this;
			return insert(pos, s, strlen(s));
		}
		template<typename _Str, class = typename std::enable_if<std::is_class<_Str>::value>::type>
		inline string_& insert(size_t pos, const _Str& str) noexcept
		{
			return insert(pos, str.data(), str.size());
		}
		template<typename _Str, class = typename std::enable_if<std::is_class<_Str>::value>::type>
		string_& insert(size_t pos, const _Str& str,
			size_t subpos, size_t sublen) noexcept
		{
			if (subpos >= str.size() || !sublen)
				return *this;
			if (sublen > str.size())
				sublen = str.size();
			if (subpos + sublen > str.size())
				sublen = str.size() - subpos;
			return insert(pos, str.data() + subpos, sublen);
		}
		string_& erase(size_t pos = 0, size_t len = (size_t)(-1)) noexcept
		{
			if (!pos && len == (size_t)(-1)) {
				clear();
				return *this;
			}
			size_t datasize = size();
			if (!len || empty() || pos >= datasize)
				return *this;
			if (len == (size_t)(-1) || pos + len >= datasize) {
				setsize_(_pstr, pos);
				return *this;
			}
			memmove(_pstr + pos, _pstr + pos + len, datasize - pos - len);
			setsize_(_pstr, datasize - len);
			return *this;
		}
		string_& replace(size_t pos, size_t len, const_pointer s, size_t n) noexcept
		{
			if (!s || !*s || !n)
				return erase(pos, len);
			if (!len)
				return insert(pos, s, n);
			erase(pos, len);
			return insert(pos, s, n);
		}
		string_& replace(size_t pos, size_t len, const char* s) noexcept
		{
			if (!s || !*s)
				return erase(pos, len);
			return replace(pos, len, s, strlen(s));
		}
		template<typename _Str, class = typename std::enable_if<std::is_class<_Str>::value>::type>
		inline string_& replace(size_t pos, size_t len, const _Str& str) noexcept
		{
			return replace(pos, len, str.data(), str.size());
		}
		template<typename _Str, class = typename std::enable_if<std::is_class<_Str>::value>::type>
		inline int compare(const _Str& str) const  noexcept
		{
			return ::strcmp(c_str(), str.c_str());
		}
		int compare(const char* s) const  noexcept
		{
			if (!s)
				return false;
			return ::strcmp(c_str(), s);
		}
		template<typename _Str, class = typename std::enable_if<std::is_class<_Str>::value>::type>
		bool operator== (const _Str& str)
		{
			return (size() == str.size() && size() && !memcmp(_pstr, str.data(), str.size()));
		}
#ifdef _WIN32
		bool format(const char* format, ...)
#else
		bool format(const char* format, ...) __attribute__((format(printf, 2, 3)))
#endif
		{
			int n = 0;
			clear();
			if (!recapacity(240 - sizeof(t_h) - 1))
				return false;
			else {
				va_list arg_ptr;
				va_start(arg_ptr, format);
				n = vsnprintf(_pstr, capacity(), format, arg_ptr);
				va_end(arg_ptr);
			}
			if (n < 0)
				return false;
			if (n <= (int)capacity()) {
				setsize_(_pstr, n);
				return true;
			}

			if (!recapacity(n))
				return false;
			else {
				va_list arg_ptr;
				va_start(arg_ptr, format);
				n = vsnprintf(_pstr, capacity(), format, arg_ptr);
				va_end(arg_ptr);
			}
			if (n >= 0 && n <= (int)capacity()) {
				setsize_(_pstr, n);
				return true;
			}
			return false;
	}
	public:
		size_t find_first_of(const string_& str, size_t pos = 0) const noexcept
		{
			if (pos >= size())
				return -1;
			const char* pt = ::strstr(c_str() + pos, str.c_str());
			if (!pt)
				return -1;
			return pt - (const char*)data();
		}
		size_t find_first_of(const char* s, size_t pos = 0) const noexcept
		{
			if (pos >= size())
				return -1;
			const char* pt = ::strstr(c_str() + pos, s);
			if (!pt)
				return -1;
			return pt - (const char*)data();
		}
		size_t find_first_of(char c, size_t pos = 0) const noexcept
		{
			if (pos >= size())
				return -1;
			const char* pt = ::strchr(c_str() + pos, c);
			if (!pt)
				return -1;
			return pt - (const char*)data();
		}
		size_t find_last_not_of(const char* s, size_t pos = npos) const noexcept
		{
			if (pos == npos)
				pos = size();
			while (pos > 0) {
				if (!strchr(s, _pstr[pos - 1]))
					break;
				--pos;
			};
			return pos - 1;
		}
	}; // string_

	using string = string_<ec_string_alloctor>;
	using bytes = string_<ec_string_alloctor, uint32_t, uint8_t>;
	
	inline string to_string(int _Val)
	{
		string str;
		str.format("%d", _Val);
		return str;
	}

	inline string to_string(unsigned int _Val)
	{
		string str;
		str.format("%u", _Val);
		return str;
	}

	inline string to_string(long _Val)
	{
		string str;
		str.format("%ld", _Val);
		return str;
	}

	inline string to_string(unsigned long _Val)
	{
		string str;
		str.format("%lu", _Val);
		return str;
	}

	inline string to_string(long long _Val)
	{
		string str;
		str.format("%lld", _Val);
		return str;
	}

	inline string to_string(unsigned long long _Val)
	{
		string str;
		str.format("%llu", _Val);
		return str;
	}

	inline string to_string(float _Val)
	{
		string str;
		str.format("%f", _Val);
		return str;
	}

	inline string to_string(double _Val)
	{
		string str;
		str.format("%f", _Val);
		return str;
	}

	inline string to_string(long double _Val)
	{
		string str;
		str.format("%Lf", _Val);
		return str;
	}

	template<class _Alo, typename _SizeType, typename _Elem>
	inline string_<_Alo, _SizeType, _Elem> operator+(
		const string_<_Alo, _SizeType, _Elem>& _Left,
		const string_<_Alo, _SizeType, _Elem>& _Right)
	{ // return string  + string 
		string_<_Alo, _SizeType, _Elem>  _Ans;
		_Ans.reserve(_Left.size() + _Right.size());
		_Ans += _Left;
		_Ans += _Right;
		return (_Ans);
	}

	template<class _Alo, typename _SizeType, typename _Elem>
	inline string_<_Alo, _SizeType, _Elem>  operator+(
		const char* _Left,
		const string_<_Alo, _SizeType, _Elem>& _Right)
	{ // return const char * + string
		string_<_Alo, _SizeType, _Elem> _Ans;
		_Ans.reserve(strlen(_Left) + _Right.size());
		_Ans += _Left;
		_Ans += _Right;
		return (_Ans);
	}

	template<class _Alo, typename _SizeType, typename _Elem>
	inline string_<_Alo, _SizeType, _Elem> operator+(
		const _Elem _Left,
		const string_<_Alo, _SizeType, _Elem>& _Right)
	{ // return char + string
		string_<_Alo, _SizeType, _Elem> _Ans;
		_Ans.reserve(1 + _Right.size());
		_Ans += _Left;
		_Ans += _Right;
		return (_Ans);
	}

	template<class _Alo, typename _SizeType, typename _Elem>
	inline string_<_Alo, _SizeType, _Elem>  operator+(
		const string_<_Alo, _SizeType, _Elem>& _Left,
		const char* _Right)
	{ // return string + const char* s
		string_<_Alo, _SizeType, _Elem> _Ans;
		_Ans.reserve(_Left.size() + strlen(_Right));
		_Ans += _Left;
		_Ans += _Right;
		return (_Ans);
	}

	template<class _Alo, typename _SizeType, typename _Elem>
	inline string_<_Alo, _SizeType, _Elem>  operator+(
		const string_<_Alo, _SizeType, _Elem>& _Left,
		const _Elem _Right)
	{	// return string + character
		string_<_Alo, _SizeType, _Elem> _Ans;
		_Ans.reserve(_Left.size() + 1);
		_Ans += _Left;
		_Ans += _Right;
		return (_Ans);
	}

	template<class _Alo, typename _SizeType, typename _Elem>
	inline string_<_Alo, _SizeType, _Elem> operator+(
		const string_<_Alo, _SizeType, _Elem>& _Left,
		string_<_Alo, _SizeType, _Elem>&& _Right)
	{	// return string + string
		return (std::move(_Right.insert(0, _Left)));
	}

	template<class _Alo, typename _SizeType, typename _Elem>
	inline string_<_Alo, _SizeType, _Elem>  operator+(
		string_<_Alo, _SizeType, _Elem>&& _Left,
		const string_<_Alo, _SizeType, _Elem>& _Right)
	{	// return string + string
		return (std::move(_Left.append(_Right)));
	}

	template<class _Alo, typename _SizeType, typename _Elem>
	inline string_<_Alo, _SizeType, _Elem> operator+(
		string_<_Alo, _SizeType, _Elem>&& _Left,
		string_<_Alo, _SizeType, _Elem>&& _Right)
	{	// return string + string
		if (_Right.size() <= _Left.capacity() - _Left.size()
			|| _Right.capacity() - _Right.size() < _Left.size())
			return (std::move(_Left.append(_Right)));
		else
			return (std::move(_Right.insert(0, _Left)));
	}

	template<class _Alo, typename _SizeType, typename _Elem>
	inline string_<_Alo, _SizeType, _Elem> operator+(
		const char* _Left,
		string_<_Alo, _SizeType, _Elem>&& _Right)
	{	// return const char* + string
		return (std::move(_Right.insert(0, _Left)));
	}

	template<class _Alo, typename _SizeType, typename _Elem>
	inline string_<_Alo, _SizeType, _Elem> operator+(
		const _Elem _Left,
		string_<_Alo, _SizeType, _Elem>&& _Right)
	{	// return character + string
		return (std::move(_Right.insert(0, 1, _Left)));
	}

	template<class _Alo, typename _SizeType, typename _Elem>
	inline string_<_Alo, _SizeType, _Elem> operator+(
		string_<_Alo, _SizeType, _Elem>&& _Left,
		const char* _Right)
	{	// return string + const char*
		return (std::move(_Left.append(_Right)));
	}

	template<class _Alo, typename _SizeType, typename _Elem>
	inline string_<_Alo, _SizeType, _Elem> operator+(
		string_<_Alo, _SizeType, _Elem>&& _Left,
		const _Elem _Right)
	{	// return string + character
		_Left.push_back(_Right);
		return (std::move(_Left));
	}

	/**
	 * @brief fixed length string
	 * @tparam chart : char or unsigned char
	*/
	template<typename chart = char,
		class = typename std::enable_if<sizeof(chart) == 1>::type >
	class fixstring_
	{
	public:
		using value_type = chart;
		using pointer = chart*;
		using const_pointer = const chart*;
		using iterator = chart*;
		using const_iterator = const chart*;
		using reference = chart&;
		using const_reference = const chart&;
	private:
		uint32_t _size, _bufsize;
		pointer _buf;
	public:		
		fixstring_(void *pbuf, size_t bufsize) : _size(0)
		{			
			_bufsize = (uint32_t)bufsize;
			_buf = (pointer)pbuf;
			_buf[0] = 0;
		}
		~fixstring_() {
			endstr();
		}
		inline iterator begin() noexcept
		{
			return _buf;
		}
		inline const_iterator begin() const noexcept
		{
			return _buf;
		}
		inline iterator end() noexcept
		{
			return _buf + _size;
		}
		inline const_iterator end() const noexcept
		{
			return _buf + _size;
		}
		inline size_t size() const noexcept
		{
			return _size;
		}
		inline size_t length() const noexcept
		{
			return _size;
		}
		inline size_t max_size() const noexcept
		{
			return _bufsize - 1;
		}
		inline size_t capacity() const noexcept
		{
			return _bufsize - 1;
		}
		inline void clear() noexcept
		{
			_size = 0;
		}
		inline bool empty() const noexcept
		{
			return 0 == _size;
		}
		inline void reserve(size_t n)
		{
		}
	public: //Element access
		inline reference operator[] (size_t pos)
		{
			return (pos < _bufsize) ? _buf[pos] : _buf[0];
		}
		inline const_reference operator[] (size_t pos) const
		{
			return (pos < _bufsize) ? _buf[pos] : _buf[0];
		}
		inline reference back() noexcept
		{
			if (!_size)
				return _buf[0];
			return _buf[_size - 1];
		}
		const_reference back() const noexcept
		{
			if (!_size)
				return _buf[0];
			return _buf[_size - 1];
		}
	public: //Modifiers
		fixstring_& append(const_pointer s, size_t n)
		{
			if (_size + (uint32_t)n < capacity()) {
				memcpy(_buf + _size, s, n);
				_size += (uint32_t)n;
			}
			return *this;
		}
		fixstring_& append(const char* s)
		{
			size_t zn = strlen(s);
			if (_size + (uint32_t)zn < capacity()) {
				memcpy(_buf + _size, s, zn);
				_size += (uint32_t)zn;
			}
			return *this;
		}
		inline void push_back(value_type c)
		{
			if (_size + 1 < capacity()) {
				_buf[_size++] = c;
			}
		}
		fixstring_& operator+= (value_type c)
		{
			if (_size + 1 < capacity()) {
				_buf[_size++] = c;
			}
			return *this;
		}
	public: //operations
		inline const_pointer c_str() const noexcept
		{
			_buf[_size] = 0;
			return _buf;
		}
		inline const_pointer data() const noexcept
		{
			return _buf;
		}
		inline void endstr() noexcept
		{
			if (!_buf || !_bufsize)
				return;
			if (_size >= _bufsize)
				_size = _bufsize - 1;
			_buf[_size] = 0;
		}
	};

	using fixstring = fixstring_<char>;
}// namespace ec
