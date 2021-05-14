/*!
\file ec_array.h
\author	jiangyong
\email  kipway@outlook.com
\update 2021.5.5

string , bytes and string functions

eclib 3.0 Copyright (c) 2017-2021, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/

#pragma once
#include <string.h>
#include <string>
#include <ctype.h>
#include <type_traits>

#ifndef _WIN32
#include <iconv.h>
#endif

#include "ec_text.h"
#include "ec_vector.h"
#include "ec_array.h"
namespace std
{
	using bytes = basic_string<uint8_t>; // std::bytes
}

#define WIN_CP_GBK  936

#ifndef EC_STR_SML_SIZE
#define EC_STR_SML_SIZE 80
#endif
#ifndef EC_STR_SML_NUMS
#define EC_STR_SML_NUMS (1024 * 48)
#endif

#ifndef EC_STR_MED_SIZE
#define EC_STR_MED_SIZE 256
#endif
#ifndef EC_STR_MED_NUMS
#define EC_STR_MED_NUMS (1024 * 16)
#endif

#ifndef EC_STR_MED2_SIZE
#define EC_STR_MED2_SIZE 512
#endif
#ifndef EC_STR_MED2_NUMS
#define EC_STR_MED2_NUMS (1024 * 8)
#endif

#ifndef EC_STR_LRG_SIZE
#define EC_STR_LRG_SIZE 1024
#endif
#ifndef EC_STR_LRG_NUMS
#define EC_STR_LRG_NUMS (1024 * 4)
#endif

#if (0 != USE_EC_STRING)
#define DEFINE_EC_STRING_ALLOCTOR ec::spinlock ec_stringspinlock;\
	ec::memory ec_stringallocator(\
	EC_STR_SML_SIZE, EC_STR_SML_NUMS,\
	EC_STR_MED_SIZE, EC_STR_MED_NUMS,\
	EC_STR_MED2_SIZE, EC_STR_MED2_NUMS,\
	EC_STR_LRG_SIZE, EC_STR_LRG_NUMS,\
	&ec_stringspinlock);\
	ec::memory* p_ec_string_allocator = &ec_stringallocator;

extern ec::memory* p_ec_string_allocator;
struct ec_string_alloctor {
	ec::memory* operator()()
	{
		return p_ec_string_allocator;
	}
};
#endif

namespace ec
{
	using bytes = vector<unsigned char>;

	using str32 = array<char, 32>;
	using str64 = array<char, 64>;
	using str80 = array<char, 80>;
	using str128 = array<char, 128>;
	using str256 = array<char, 256>;
	using str512 = array<char, 512>;
	using str1k = array<char, 1024>;

	using strargs = array<txt, 128>; //for ec::strsplit  out buffer

	struct null_stralloctor {
		ec::memory* operator()()
		{
			return nullptr; // user ::malloc() and ::free()
		}
	};

	template<class _Alloctor = null_stralloctor, typename size_type = uint32_t>
	class string_ // basic_string
	{
	public:
		using pointer = char*;
		using const_pointer = char*;
		using iterator = char*;
		using const_iterator = const char *;
		using reference = char&;
		using const_reference = const char&;

		struct t_h {
			size_type sizebuf; // not include head
			size_type sizedata;// not include null
		};
	private:
		char* _pstr;
	private:
		char* smalloc(size_t strsize)
		{
			if (strsize > max_size())
				return nullptr;
			char *pstr = nullptr;
			ec::memory *pmem = _Alloctor()();
			size_t zr = strsize + sizeof(t_h) + 1;
			if (pmem)
				pstr = (char*)pmem->malloc(zr, zr, zr + zr / 2u < max_size());
			else {
				if (zr % 16u)
					zr += 16u - zr % 16u;
				if (zr + zr / 2u < max_size())
					zr += zr / 2u;
				if (zr < 64u)
					zr = 64u;
				pstr = (char*)::malloc(zr);
			}
			if (pstr) {
				t_h* ph = (t_h*)pstr;
				ph->sizebuf = (size_type)(zr - sizeof(t_h));
				ph->sizedata = 0;
				pstr += sizeof(t_h);
			}
			return pstr;
		}

		void sfree(pointer &str)
		{
			if (str) {
				ec::memory *pmem = _Alloctor()();
				if (pmem)
					pmem->mem_free(str - sizeof(t_h));
				else
					::free(str - sizeof(t_h));
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
			if (!_pstr) {
				_pstr = smalloc(strsize);
				return nullptr != _pstr;
			}
			size_t zd = ssize(_pstr);
			char* pnewstr = smalloc(strsize);
			if (!pnewstr)
				return false;
			memcpy(pnewstr, _pstr, zd);
			setsize_(pnewstr, zd);
			sfree(_pstr);
			_pstr = pnewstr;
			return true;
		}

		void setsize_(char* pstr, size_t zlen)
		{
			if (!pstr)
				return;
			t_h* ph = (t_h*)(pstr - sizeof(t_h));
			ph->sizedata = (size_type)zlen;
		}

		size_t ssize(const char* pstr) const
		{
			if (!pstr)
				return 0;
			const t_h* ph = (const t_h*)(pstr - sizeof(t_h));
			return ph->sizedata;
		}

		size_t scapacity(const char* pstr) const
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

		string_(const char* s, size_t size) : _pstr(nullptr)
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

		string_(string_<_Alloctor, size_type>&& str) // move construct
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

		operator pointer()
		{
			if (!_pstr)
				return (char*)"";// never return nullptr
			_pstr[size()] = 0;
			return _pstr;
		}

		void swap(string_<_Alloctor, size_type>& str) //simulate move
		{
			char *stmp = _pstr;
			_pstr = str._pstr;
			str._pstr = stmp;
		}

		template<typename _Str, class = typename std::enable_if<std::is_class<_Str>::value>::type>
		string_& operator= (const _Str &str)
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

		inline ec::memory* get_allocator() const noexcept
		{
			return _Alloctor()();
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

		inline void clear() noexcept
		{
			setsize_(_pstr, 0);
		}

		inline bool empty() const noexcept
		{
			return !size();
		}

	public: //Element access
		inline reference operator[] (size_t pos) noexcept
		{
			return _pstr[pos];
		}
		inline const_reference operator[] (size_t pos) const noexcept
		{
			return _pstr[pos];
		}

	public: // String operations:
		const char *data() const noexcept
		{
			return _pstr;
		}
		const char* c_str() const noexcept
		{
			if (!_pstr)
				return "";// never return nullptr
			_pstr[size()] = 0;
			return _pstr;
		}

	public: //Modifiers
		string_& append(const char* s, size_t n) noexcept
		{
			if (!s || !*s || !n)
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

		string_& append(char c) noexcept
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

		inline string_& assign(const char* s, size_t n) noexcept
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

		inline string_& operator+= (char c) noexcept
		{
			return append(c);
		}

		inline void push_back(char c) noexcept
		{
			append(c);
		}

		void pop_back() noexcept
		{
			size_t zlen = size();
			if (zlen > 0)
				setsize_(_pstr, zlen - 1);
		}

		inline const char& back() const
		{
			return _pstr[size() - 1];
		}

		inline char& back()
		{
			return _pstr[size() - 1];
		}

		void resize(size_t n, char c = 0) noexcept
		{
			size_t zlen = size();
			if (zlen > n)
				setsize_(_pstr, n);
			else if (n < zlen) {
				if (recapacity(n)) {
					memset(_pstr + zlen, c, n - zlen);
					setsize_(_pstr, n);
				}
			}
		}

		string_& insert(size_t pos, const char* s, size_t n) noexcept
		{
			if (!s || !*s || !n)
				return *this;
			size_t zlen = size();
			if (pos >= zlen)
				return append(s, n);
			if (zlen + n > capacity()) {
				char* pnewstr = smalloc(zlen + n);
				if (!pnewstr)
					return *this;
				if (pos)
					memcpy(pnewstr, _pstr, pos);
				memcpy(pnewstr + pos, s, n);
				memcpy(pnewstr + pos + n, _pstr + pos, zlen - pos);
				setsize_(pnewstr, zlen + n);
				sfree(_pstr);
				_pstr = pnewstr;
				return *this;
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

		string_& replace(size_t pos, size_t len, const char* s, size_t n) noexcept
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
		bool printf(const char * format, ...)
#else
		bool printf(const char * format, ...) __attribute__((format(printf, 2, 3)))
#endif
		{
			int n = 0;
			clear();
			if (!recapacity(EC_STR_MED_SIZE - sizeof(t_h) - 1))
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
	}; // string_

#if (0 != USE_EC_STRING)
	using string = string_<ec_string_alloctor>;
#endif

	inline int stricmp(const char*s1, const char*s2)
	{
#ifdef _WIN32
		return ::_stricmp(s1, s2);
#else
		return ::strcasecmp(s1, s2);
#endif
	}

	template<typename charT
		, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
		size_t strlcpy(charT* sd, const charT* ss, size_t count)
	{// like strlcpy for linux,add null to the end of sd,return strlen(ss)
		if (!ss || !(*ss)) {
			if (sd && count)
				*sd = '\0';
			return 0;
		}
		const size_t srclen = strlen(ss);
		if (!sd || !count)
			return srclen;

		if (srclen < count)
			memcpy(sd, ss, srclen + 1);
		else {
			memcpy(sd, ss, count - 1);
			sd[count - 1] = '\0';
		}
		return srclen;
	}

	template<typename charT>
	char *strncpy_s(charT *dest, size_t destsize, const charT* src, size_t srcsize)
	{ // safe copy, always adding a null character at the end of dest
		if (!dest || !destsize)
			return dest;
		if (!src || !srcsize) {
			*dest = '\0';
			return dest;
		}

		if (destsize <= srcsize)
			srcsize = destsize - 1;
		memcpy(dest, src, srcsize * sizeof(charT));
		dest[srcsize] = '\0';
		return dest;
	}

	template<typename charT>
	bool streq(const charT* s1, const charT* s2)
	{ // return true: equal; false not equal
		if (!s1 || !s2)
			return false;
		while (*s1 && *s2) {
			if (*s1++ != *s2++)
				return false;
		}
		return *s1 == '\0' && *s2 == '\0';
	}

	template<typename charT>
	bool strieq(const charT* s1, const charT* s2)
	{ //case insensitive equal. return true: equal; false not equal
		if (!s1 || !s2)
			return false;
		while (*s1 && *s2) {
			if (*s1 != *s2 && tolower(*s1) != tolower(*s2))
				return false;
			++s1;
			++s2;
		}
		return *s1 == '\0' && *s2 == '\0';
	}

	template<typename charT>
	bool strineq(const charT* s1, const charT* s2, size_t s2size, bool balls1 = false)// Judge n characters equal case insensitive
	{
		if (!s1 || !s2)
			return false;
		size_t i = 0;
		while (i < s2size && *s1 && *s2) {
			if (*s1 != *s2 && tolower(*s1) != tolower(*s2))
				return false;
			s1++;
			s2++;
			i++;
		}
		if (balls1)
			return !*s1 && i == s2size;
		return i == s2size;
	}

	template<typename charT
		, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
		void strtrim(charT *s, const charT* flt = "\x20\t\n\r")
	{
		if (!*s)
			return;
		char *sp = s, *s1 = s;
		while (*sp && strchr(flt, *sp))
			sp++;
		if (sp != s) {
			while (*sp)
				*s1++ = *sp++;
			*s1 = '\0';
		}
		else
			while (*s1++);
		while (s1 > s) {
			s1--;
			if (strchr(flt, *s1))
				*s1 = '\0';
			else
				break;
		}
	}

	/*!
		\brief get next string
		\param cp separate character
		\param src source string
		\param srcsize source string length
		\param pos [in/out] current position
		\param sout [out] output buffer
		\param outsize output buffer length
		*/

	template<typename charT>
	const char* strnext(const charT cp, const charT* src, size_t srcsize, size_t &pos, charT *sout, size_t outsize)
	{
		charT c;
		size_t i = 0;
		while (pos < srcsize) {
			c = src[pos++];
			if (c == cp) {
				while (i > 0) { // delete tail space char
					if (sout[i - 1] != '\t' && sout[i - 1] != ' ')
						break;
					i--;
				}
				sout[i] = '\0';
				if (i > 0)
					return sout;
			}
			else if (c != '\n' && c != '\r') {
				if (i == 0 && (c == '\t' || c == ' ')) //delete head space char
					continue;
				sout[i++] = c;
				if (i >= outsize)
					return 0;
			}
		}
		if (i && i < outsize && pos == srcsize) {
			while (i > 0) { //delete tail space char
				if (sout[i - 1] != '\t' && sout[i - 1] != ' ')
					break;
				i--;
			}
			sout[i] = '\0';
			if (i > 0)
				return sout;
		}
		return 0;
	}

	/*!
	\brief get next string
	\param split separate characters
	\param src source string
	\param srcsize source string length
	\param pos [in/out] current position
	\param sout [out] output buffer
	\param outsize output buffer length
	*/

	template<typename charT>
	const char* strnext(const charT* split, const charT* src, size_t srcsize, size_t &pos, charT *sout, size_t outsize)
	{
		charT c;
		size_t i = 0;
		while (pos < srcsize) {
			c = src[pos++];
			if (strchr(split, c)) {
				while (i > 0) { // delete tail space char
					if (sout[i - 1] != '\t' && sout[i - 1] != ' ')
						break;
					i--;
				}
				sout[i] = '\0';
				if (i > 0)
					return sout;
			}
			else if (c != '\n' && c != '\r') {
				if (i == 0 && (c == '\t' || c == ' ')) //delete head space char
					continue;
				sout[i++] = c;
				if (i >= outsize)
					return 0;
			}
		}
		if (i && i < outsize && pos == srcsize) {
			while (i > 0) { //delete tail space char
				if (sout[i - 1] != '\t' && sout[i - 1] != ' ')
					break;
				i--;
			}
			sout[i] = '\0';
			if (i > 0)
				return sout;
		}
		return 0;
	}

	template<typename charT>
	bool char2hex(charT c, unsigned char *pout)
	{
		if (c >= 'a' && c <= 'f')
			*pout = 0x0a + (c - 'a');
		else if (c >= 'A' && c <= 'F')
			*pout = 0x0a + (c - 'A');
		else if (c >= '0' && c <= '9')
			*pout = c - '0';
		else
			return false;
		return true;
	}

	template<class  _Str = std::string>
	int url2utf8(const char* url, _Str &so)
	{ //utf8 fomat url translate to utf8 string
		unsigned char h, l;
		so.clear();
		while (*url) {
			if (*url == '%') {
				url++;
				if (!char2hex(*url++, &h))
					break;
				if (!char2hex(*url++, &l))
					break;
				so += (char)((h << 4) | l);
			}
			else if (*url == '+') {
				so += '\x20';
				url++;
			}
			else
				so += *url++;
		}
		return (int)so.size();
	}

	template<class _Str = std::string>
	int utf82url(const char* url, _Str &so)
	{ //utf8 string -> url
		unsigned char h, l, *p = (unsigned char*)url;
		so.clear();
		while (*p) {
			if (*p == '\x20') {
				so += '+';
				p++;
			}
			else if (*p & 0x80) {
				so += '%';
				h = (*p & 0xF0) >> 4;
				l = *p & 0x0F;
				if (h >= 10)
					so += ('A' + h - 10);
				else
					so += ('0' + h);
				if (l >= 10)
					so += ('A' + l - 10);
				else
					so += ('0' + l);
				p++;
			}
			else
				so += (char)* p++;
		}
		return (int)so.size();
	}

	inline 	char* strupr(char *str)
	{
#ifdef _WIN32
		return _strupr(str);
#else
		char *ptr = str;
		while (*ptr) {
			if (*ptr >= 'a' && *ptr <= 'z')
				*ptr -= 'a' - 'A';
			ptr++;
		}
		return str;
#endif
	}

	inline char* strlwr(char *str)
	{
#ifdef _WIN32
		return _strlwr(str);
#else
		char *ptr = str;
		while (*ptr) {
			if (*ptr >= 'A' && *ptr <= 'Z')
				*ptr += 'a' - 'A';
			ptr++;
		}
		return str;
#endif
	}

	template<typename charT
		, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
		bool strisutf8(const charT* s, size_t size = 0)
	{ //return true if s is utf8 string
		if (!s)
			return true;
		uint8_t c;
		int nb = 0;
		const char* pend = s + (size ? size : strlen(s));
		while (s < pend) {
			c = *s++;
			if (!nb) {
				if (!(c & 0x80))
					continue;
				if (c >= 0xFC && c <= 0xFD)
					nb = 5;
				else if (c >= 0xF8)
					nb = 4;
				else if (c >= 0xF0)
					nb = 3;
				else if (c >= 0xE0)
					nb = 2;
				//else if (c >= 0xC0) // GBK -> UTF8  > 2bytes
				//nb = 1;
				else
					return false;
				continue;
			}
			if ((c & 0xC0) != 0x80)
				return false;
			nb--;
		}
		return !nb;
	}

	template<typename charT
		, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
		bool strisascii(const charT* s, size_t size = 0)
	{// If s is a pure ASCII code or a utf8 string containing only ASCII, it will return true
		const char* pend = s + (size ? size : strlen(s));
		while (s < pend) {
			if (*s < 0)
				return false;
			++s;
		}
		return true;
	}

	template<typename charT
		, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
		int utf82gbk(const charT* in, size_t sizein, char *out, size_t sizeout)
	{ //return number bytes write to out or -1 error
		*out = 0;
		if (!in || !(*in))
			return 0;
#ifdef _WIN32
		wchar_t wtmp[16384];
		wchar_t* sUnicode = wtmp;
		int sizetmp = 16383, nret = -1;
		for (;;) {
			if (sizein > (size_t)sizetmp) {
				sizetmp = MultiByteToWideChar(CP_UTF8, 0, in, (int)sizein, NULL, 0);
				if (!sizetmp)
					break;
				sUnicode = (wchar_t*)malloc(sizeof(wchar_t) * (sizetmp + 1));
				if (!sUnicode)
					break;
			}
			sizetmp = MultiByteToWideChar(CP_UTF8, 0, in, (int)sizein, sUnicode, sizetmp); //utf8 -> unicode wchar
			if (!sizetmp)
				break;
			sizetmp = WideCharToMultiByte(WIN_CP_GBK, 0, sUnicode, sizetmp, out, (int)sizeout - 1, NULL, NULL); //unicode wchar -> gbk
			if (sizetmp) {
				out[sizetmp] = 0;
				nret = (int)sizetmp;
			}
			break;
		}
		if (sUnicode != wtmp)
			free(sUnicode);
		return nret;
#else
		iconv_t cd;
		char **pin = (char**)&in;
		char **pout = &out;

		cd = iconv_open("GBK//IGNORE", "UTF-8");
		if (cd == (iconv_t)-1)
			return -1;

		size_t inlen = sizein;
		size_t outlen = sizeout - 1;
		if (iconv(cd, pin, &inlen, pout, &outlen) == (size_t)(-1)) {
			iconv_close(cd);
			return -1;
		}
		iconv_close(cd);
		*out = 0;
		return (int)(sizeout - outlen - 1);
#endif
	}

	template<typename charT
		, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
		int gbk2utf8(const charT* in, size_t sizein, char *out, size_t sizeout)
	{ //return number bytes write to out or -1 error
		*out = 0;
		if (!in || !(*in))
			return 0;
#ifdef _WIN32
		wchar_t wtmp[16384];
		wchar_t* sUnicode = wtmp;
		int sizetmp = 16383, nret = -1;
		for (;;) {
			if (sizein > (size_t)sizetmp) {
				sizetmp = MultiByteToWideChar(WIN_CP_GBK, 0, in, (int)sizein, NULL, 0);
				if (!sizetmp)
					break;
				sUnicode = (wchar_t*)malloc(sizeof(wchar_t) * (sizetmp + 1));
				if (!sUnicode)
					break;
			}
			sizetmp = MultiByteToWideChar(WIN_CP_GBK, 0, in, (int)sizein, sUnicode, sizetmp); //gbk -> unicode wchar
			if (!sizetmp)
				break;
			sizetmp = WideCharToMultiByte(CP_UTF8, 0, sUnicode, sizetmp, out, (int)sizeout - 1, NULL, NULL); //unicode wchar -> utf-8
			if (sizetmp) {
				out[sizetmp] = 0;
				nret = (int)sizetmp;
			}
			break;
		}
		if (sUnicode != wtmp)
			free(sUnicode);
		return nret;
#else
		iconv_t cd;
		char **pin = (char**)&in;
		char **pout = &out;

		cd = iconv_open("UTF-8//IGNORE", "GBK");
		if (cd == (iconv_t)-1)
			return -1;
		size_t inlen = sizein;
		size_t outlen = sizeout - 1;
		if (iconv(cd, pin, &inlen, pout, &outlen) == (size_t)(-1)) {
			iconv_close(cd);
			return -1;
		}
		iconv_close(cd);
		*out = 0;
		return (int)(sizeout - outlen - 1);
#endif
	}

	template <class _Str = std::string>
	int gbk2utf8_s(const char* in, size_t sizein, _Str &sout) // return sout.zize() or -1 error;
	{
		if (sizein * 3 >= 16384)
			return -1;
		char tmp[16384];
		int n = gbk2utf8(in, sizein, tmp, sizeof(tmp));
		if (n < 0)
			return -1;
		try {
			sout.append(tmp, n);
		}
		catch (...) {
			return -1;
		}
		return n;
	}

	template <class _Str = std::string>
	int gbk2utf8_s(_Str &s) // return s.zize() or -1 error;
	{
		if (s.empty() || strisutf8(s.data(), s.size()))
			return (int)s.size();
		if (s.size() * 3 >= 16384)
			return -1;
		char tmp[16384];
		int n = gbk2utf8(s.data(), s.size(), tmp, sizeof(tmp));
		if (n < 0)
			return -1;
		try {
			s.assign(tmp, n);
		}
		catch (...) {
			return -1;
		}
		return n;
	}

	template <class _Str = std::string>
	int utf82gbk_s(const char* in, size_t sizein, _Str &sout)
	{
		if (sizein >= 16384)
			return -1;
		char tmp[16384];
		int n = utf82gbk(in, sizein, tmp, sizeof(tmp));
		if (n < 0)
			return -1;
		try {
			sout.append(tmp, n);
		}
		catch (...) {
			return -1;
		}
		return n;
	}

	template<typename charT
		, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
		void hex2str(const void* psrc, size_t sizesrc, charT *sout, size_t outsize)
	{
		unsigned char uc;
		size_t i;
		const unsigned char* pu = (const unsigned char*)psrc;
		for (i = 0; i < sizesrc && 2 * i + 1 < outsize; i++) {
			uc = pu[i] >> 4;
			sout[i * 2] = (uc >= 0x0A) ? 'A' + (uc - 0x0A) : '0' + uc;
			uc = pu[i] & 0x0F;
			sout[i * 2 + 1] = (uc >= 0x0A) ? 'A' + (uc - 0x0A) : '0' + uc;
		}
		sout[2 * i] = 0;
	}

	template<typename ucharT
		, class = typename std::enable_if<std::is_same<ucharT, unsigned char>::value>::type>
		void xor_le(ucharT* pd, int size, unsigned int umask)
	{ // little endian fast XOR,4x faster than byte-by-byte XOR
		if (!size)
			return;
		int i = 0, nl = 0, nu;
		unsigned int um = umask;
		if ((size_t)pd % 4) {
			nl = 4 - ((size_t)pd % 4);
			um = umask >> nl * 8;
			um |= umask << (4 - nl) * 8;
		}
		nu = (size - nl) / 4;
		for (i = 0; i < nl && i < size; i++)
			pd[i] ^= (umask >> ((i % 4) * 8)) & 0xFF;

		unsigned int *puint = (unsigned int*)(pd + i);
		for (i = 0; i < nu; i++)
			puint[i] ^= um;

		for (i = nl + nu * 4; i < size; i++)
			pd[i] ^= (umask >> ((i % 4) * 8)) & 0xFF;
	}

	template<typename charT
		, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
		int hexview16(const void* psrc, int srclen, charT * sout, size_t sizeout)//view 16 bytes，return do bytes
	{
		*sout = 0;
		if (srclen <= 0 || sizeout < 88u)
			return -1;
		int i, k = 0, n = srclen > 16 ? 16 : srclen;
		unsigned char ul, uh;
		const unsigned char* s = (const unsigned char*)psrc;
		sout[k++] = '\t';
		for (i = 0; i < 16; i++) {
			if (i < n) {
				uh = (s[i] & 0xF0) >> 4;
				ul = (s[i] & 0x0F);

				if (uh < 10)
					sout[k++] = '0' + uh;
				else
					sout[k++] = 'A' + uh - 10;
				if (ul < 10)
					sout[k++] = '0' + ul;
				else
					sout[k++] = 'A' + ul - 10;
			}
			else {
				sout[k++] = '\x20';
				sout[k++] = '\x20';
			}
			sout[k++] = '\x20';
			if (i == 7 || i == 15) {
				sout[k++] = '\x20';
				sout[k++] = '\x20';
				sout[k++] = '\x20';
			}
		}
		for (i = 0; i < n; i++) {
			if (isprint(s[i]))
				sout[k++] = s[i];
			else
				sout[k++] = '.';
		}
		sout[k++] = '\n';
		sout[k] = '\0';
		return n;
	}

	/*!
	\brief view bytes
	like this
	16 03 03 00 33 01 00 00    2F 03 03 39 7F 29 AE 20    ....3.../..9.).
	8D 03 12 61 52 0A 2E 02    86 13 66 CA 3C 7E 6A 54    ...aR.....f.<~jT
	39 D2 CD 22 D6 A7 2C 08    EF F4 BC 00 00 08 00 3D    9.."..,........=
	00 3C 00 35 00 2F 01 00                               .<.5./..
	*/
	template<typename charT
		, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
		const char* bin2view(const void* pm, size_t size, charT *so, size_t sizeout)
	{
		if (!so)
			return nullptr;
		*so = 0;
		if (sizeout < 6 || !size)
			return so;
		char stmp[128];
		int ndo = 0, n = (int)size, nd;
		const uint8_t* p = (uint8_t*)pm;
		while (n - ndo > 0) {
			nd = ec::hexview16(p + ndo, n - ndo, stmp, sizeof(stmp));
			if (nd <= 0)
				break;
			ndo += nd;
			if (strlen(so) + strlen(stmp) + 1 > sizeout)
				return so;
			strcat(so, stmp);
		}
		return so;
	}

	template<class _STR = std::string>
	void formatpath(_STR &s)
	{
		if (s.empty())
			return;
		for (auto &i : s) {
			if (i == '\\')
				i = '/';
		}

		if (s.back() != '/')
			s.push_back('/');
	}

	template<class _Out = strargs>
	int strsplit(const char* split, const char* src, size_t srcsize, _Out& out, int maxitems = 0)
	{ // return	number of t_str in out
		out.clear();
		txt t = { src,0 };
		const char* send = src + srcsize;
		while (src < send) {
			if (strchr(split, *src)) {
				if ((size_t)src > (size_t)t._str) {
					t._size = src - t._str;
					out.push_back(t);
					if (maxitems && (int)out.size() >= maxitems)
						return (int)out.size();
				}
				t._str = ++src;
				t._size = 0;
			}
			else
				++src;
		}
		if ((size_t)src > (size_t)t._str && (!maxitems || (int)out.size() < maxitems)) {
			t._size = src - t._str;
			out.push_back(t);
		}
		return (int)out.size();
	}

	/*!
		\brief filter string

		sfliter support *?
		\param ssrc [in] src
		\param sfliter [in] filter str
		\return true success
		*/
	template<typename charT
		, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
		bool strfilter(const charT *ssrc, const charT *sfliter)
	{
		char ssub[512], cp = 0;
		char *ps = ssub, *ss = (char *)ssrc, *sf = (char *)sfliter;
		if (!ss || !sf || *sf == 0)
			return true;
		if ((*sf == '*') && (*(sf + 1) == 0))
			return true;
		while ((*sf) && (*ss)) {
			if (*sf == '*') {
				if (ps != ssub) {
					*ps = 0;
					ss = strstr(ss, ssub);
					if (!ss)
						return false;
					ss += (ps - ssub);
					ps = ssub;
				}
				cp = '*';
				sf++;
			}
			else if (*sf == '?') {
				if (ps != ssub) {
					*ps = 0;
					ss = strstr(ss, ssub);
					if (!ss)
						return false;
					ss += (ps - ssub);
					ps = ssub;
				}
				ps = ssub;
				cp = '?';
				ss++;
				sf++;
			}
			else {
				if (cp == '*')
					*ps++ = *sf++;
				else {
					if (*sf != *ss)
						return false;
					sf++;
					ss++;
				}
			}
		}//while
		if (cp != '*') {
			if (*ss == *sf)
				return true;
			if (*sf == '*') {
				sf++;
				if (*sf == 0)
					return true;
			}
			return false;
		}
		if (ps != ssub) {
			*ps = 0;
			ss = strstr(ss, ssub);
			if (!ss)
				return false;
			ss += (ps - ssub);
			if (!*ss)
				return true;
			return false;
		}
		return true;
	}

	template<class _STR = std::string>
	size_t utf8_substr(_STR &s, size_t sublen) // return substr size
	{
		if (s.size() <= sublen)
			return s.size();
		if (s.empty())
			return 0;
		uint8_t uc;
		size_t pos = s.size() - 1;
		while (pos > 0) {
			uc = s[pos];
			if ((uc < 0x80 || uc >= 0xC0) && pos <= sublen)
				break;
			--pos;
		}
		s.resize(pos);
		return pos;
	}

	template<typename charT, class = typename std::enable_if<sizeof(charT) == 1>::type>
	size_t utf8_substr(charT *s, size_t size, size_t sublen) // return substr size
	{
		if (size <= sublen)
			return size;
		if (!s || !*s || !size)
			return 0;
		uint8_t uc;
		size_t pos = size - 1;
		while (pos > 0) {
			uc = s[pos];
			if ((uc < 0x80 || uc >= 0xC0) && pos <= sublen)
				break;
			--pos;
		}
		s[pos] = 0;
		return pos;
	}

	template<typename charT, class = typename std::enable_if<sizeof(charT) == 1>::type>
	size_t utf8_strlcpy(charT* sd, const charT* ss, size_t count)
	{// like strlcpy for linux,add null to the end of sd,return strlen(ss), count is sd size
		if (!ss || !(*ss)) {
			if (sd && count)
				*sd = '\0';
			return 0;
		}
		const size_t srclen = strlen(ss);
		if (!sd || !count)
			return srclen;
		if (srclen < count) {
			memcpy(sd, ss, srclen + 1);
			return srclen;
		}
		size_t pos = 0;
		uint8_t uc;
		int i, nb;
		while ((uc = *ss) && pos + 1 < count) {
			if (uc < 0x80 || (uc >> 6) == 0x10) {
				*sd++ = *ss++;
				++pos;
				continue;
			}
			nb = 0;
			for (i = 2; i < 6; i++) {
				if ((uc >> i) == (0xFF >> i)) {
					nb = 8 - i;
					break;
				}
			}
			if (!nb || pos + nb >= count || pos + nb > srclen)
				break;
			memcpy(sd, ss, nb);
			sd += nb;
			ss += nb;
			pos += nb;
		}
		*sd = '\0';
		return srclen;
	}

	template<typename charT
		, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
		bool jstr_needesc(const charT* src, size_t srcsize)
	{
		bool besc = false;
		for (auto i = 0u; i < srcsize; i++) {
			if (src[i] == '\\' || src[i] == '\"') {
				besc = true;
				break;
			}
		}
		return besc;
	}

	template<typename _Str>
	const char* jstr_toesc(const char* s, size_t srcsize, _Str &so) //escape  '\' -> '\\', '"' -> '\"'
	{
		so.clear();
		const char *se = s + srcsize;
		while (s < se) {
			if (*s == '\\' || *s == '\"')
				so += '\\';
			so += *s++;
		}
		return so.c_str();
	}

	template<typename _Str>
	const char* jstr_fromesc(const char* s, size_t srcsize, _Str &so) // delete escape, "\\" -> '\', ""\'" -> '"'  so
	{
		so.clear();
		if (!s || !srcsize)
			return so.c_str();
		bool besc = false;
		for (auto i = 0u; i < srcsize; i++) {
			if (s[i] == '\\') {
				besc = true;
				break;
			}
		}
		if (!besc) {
			so.append(s, srcsize);
			return so.c_str();
		}

		const char *se = s + srcsize;
		while (s < se) {
			if (*s == '\\' && s + 1 < se && (*(s + 1) == '\"' || *(s + 1) == '\\'))
				s++;
			so += *s++;
		}
		return so.c_str();
	}

	template<typename charT>
	bool strneq(const charT* s1, const charT* s2, size_t  n)
	{
		if (!s1 || !s2)
			return false;
		size_t i = 0;
		for (; i < n; i++) {
			if (!s1[i] || !s2[i])
				break;
			if (s1[i] != s2[i])
				return false;
		}
		return i == n;
	}

	template<typename charT>
	bool strnieq(const charT* s1, const charT* s2, size_t  n)
	{
		if (!s1 || !s2)
			return false;
		size_t i = 0;
		for (; i < n; i++) {
			if (!s1[i] || !s2[i])
				break;
			if (tolower(s1[i]) != tolower(s2[i]))
				return false;
		}
		return i == n;
	}

	template<typename _Str>
	void out_jstr(const char* s, size_t srcsize, _Str &sout) //escape and append  to so,  escape  '\' -> '\\', '"' -> '\"' in s
	{
		if (!s || !srcsize)
			return;
		if (!jstr_needesc(s, srcsize)) {
			sout.append(s, srcsize);
			return;
		}
		const char *se = s + srcsize;
		while (s < se) {
			if (*s == '\\' || *s == '\"')
				sout += '\\';
			sout += *s++;
		}
	}

	template<typename _Str>
	void from_jstr(const char* s, size_t srcsize, _Str &sout) // delete escape, "\\" -> '\', ""\'" -> '"' and set to sout
	{
		sout.clear();
		if (!s || !srcsize)
			return;
		bool besc = false;
		for (auto i = 0u; i < srcsize; i++) {
			if (s[i] == '\\') {
				besc = true;
				break;
			}
		}
		if (!besc) {
			sout.append(s, srcsize);
			return;
		}

		const char *se = s + srcsize;
		while (s < se) {
			if (*s == '\\' && s + 1 < se && (*(s + 1) == '\"' || *(s + 1) == '\\'))
				s++;
			sout += *s++;
		}
	}
}// namespace ec
