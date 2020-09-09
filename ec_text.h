/*!
\file ec_text.h
\author	jiangyong
\email  kipway@outlook.com
\update 2020.8.31

txt
	const text parse class

eclib 3.0 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/
#pragma once
#include <stdlib.h>
#include <ctype.h>
namespace ec
{
	struct txt {
		const char* _str;
		size_t _size;
		txt() : _str(nullptr), _size(0)
		{
		}
		txt(const char *s) : txt()
		{
			if (s) {
				_str = s;
				_size = strlen(s);
			}
		}
		txt(const char *s, size_t size) : _str(s), _size(size)
		{
		}
		inline void clear()
		{
			_str = nullptr;
			_size = 0;
		}
		inline bool empty() const
		{
			return !_size || !_str;
		}
		void trimleft()
		{
			while (_size > 0 && (*_str == '\x20' || *_str == '\t' || *_str == '\r' || *_str == '\n')) {
				++_str;
				--_size;
			}
		}
		void trimright()
		{
			const char* s = _str + _size - 1;
			while (_size > 0 && (*s == '\x20' || *s == '\t' || *s == '\r' || *s == '\n')) {
				--s;
				--_size;
			}
		}
		inline void trim()
		{
			trimleft();
			trimright();
		}
		bool eq(const txt &v) const // equal
		{
			if (v._size != _size)
				return false;
			size_t i = _size;
			const char *s1 = v._str, *s2 = _str;
			while (i) {
				if (*s1++ != *s2++)
					return false;
				i--;
			}
			return true;
		}
		bool ieq(const txt &v) const // Case insensitive equal
		{
			if (v._size != _size)
				return false;
			size_t i = _size;
			const char *s1 = v._str, *s2 = _str;
			while (i) {
				if (*s1 != *s2 && tolower(*s1) != tolower(*s2))
					return false;
				++s1;
				++s2;
				i--;
			}
			return true;
		}
		bool tochar(char c) // move to c
		{
			while (_size && *_str != c) {
				_str++;
				_size--;
			}
			return _size != 0;
		}
		bool tochar(const char* cs) //move to char oneof cs
		{
			while (_size && !strchr(cs, *_str)) {
				_str++;
				_size--;
			}
			return _size != 0;
		}
		bool skip() // skip space and point to a none space char
		{
			while (_size && (*_str == '\x20' || *_str == '\t' || *_str == '\r' || *_str == '\n')) {
				_str++;
				_size--;
			}
			return _size != 0;
		}
		inline bool skipto(char c) // skip space and check the first none space char is c
		{
			return (skip() && *_str == c);
		}
		inline bool skipto(const char* cs) // skip space and check the first none space char is c
		{
			return (skip() && strchr(cs, *_str));
		}
		bool tonext()
		{
			if (_size && _str) {
				--_size;
				++_str;
				return true;
			}
			return false;
		}
		inline int stoi() const// atoi()
		{
			return (int)stoll();
		}
		long long stoll() const // atoll()
		{
			char s[32];
			if (!_str || !_size)
				return 0;
			size_t n = _size > 31 ? 31 : _size;
			memcpy(s, _str, n);
			s[n] = 0;
			return atoll(s);
		}
		inline double stof() const
		{
			char s[80];
			if (!_str || !_size)
				return 0;
			size_t n = _size > 79 ? 79 : _size;
			memcpy(s, _str, n);
			s[n] = 0;
			return atof(s);
		}
		int get(char *pout, size_t sizeout) const // return get chars
		{
			if (!_str || _size >= sizeout)
				return 0;
			memcpy(pout, _str, _size);
			pout[_size] = 0;
			return (int)_size;
		}
		template<class _Str>
		int get(_Str &sout)
		{
			try {
				sout.assign(_str, _size);
			}
			catch (...) {
				return -1;
			}
			return (int)sout.size();
		}
		bool json_tochar(int c) // for json
		{
			while (_size) {
				if (*_str == c && *(_str - 1) != '\\')
					break;
				_str++;
				_size--;
			}
			return _size != 0;
		}
		bool json_tochar(const char* cs) // for json
		{
			while (_size) {
				if (strchr(cs, *_str) && *(_str - 1) != '\\')
					break;
				++_str;
				--_size;
			}
			return _size != 0;
		}
		inline bool json_toend(char cs, char ce) // for json;  v:[0,1,2,3,4];  _str point to 0, move to ;
		{
			int nk = 1;
			char cp = 0;
			while (_size && nk) {
				if (*_str == '"' && *(_str - 1) != '\\') {
					cp = cp == '"' ? 0 : '"';
				}
				if (*_str == cs && !cp && *(_str - 1) != '\\')
					++nk;
				else if (*_str == ce && !cp && *(_str - 1) != '\\')
					--nk;
				++_str;
				--_size;
			}
			return !nk;
		}
	};
}// ec