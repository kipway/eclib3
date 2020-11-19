/*!
\file ec_array.h
\author	jiangyong
\email  kipway@outlook.com
\update 2020.9.20

string , bytes and string functions

eclib 3.0 Copyright (c) 2017-2020, kipway
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

#if (0 != USE_EC_STRING)
#define DEFINE_EC_STRING_ALLOCTOR 	ec::spinlock ec_stringspinlock;\
	ec::memory ec_stringallocator(48, 16384, 128, 8192, 512, 2048, &ec_stringspinlock);\
	ec::memory* p_ec_string_allocator = &ec_stringallocator;

#define DEFINE_EC_STRING_ALLOCTOR_ARM 	ec::spinlock ec_stringspinlock;\
	ec::memory ec_stringallocator(48, 1024, 128, 512, 256, 128, &ec_stringspinlock);\
	ec::memory* p_ec_string_allocator = &ec_stringallocator;\

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
#if (0 != USE_EC_STRING)
	using string = vector<char, ec_string_alloctor>;
#endif

	using bytes = vector<unsigned char>;

	using str32 = array<char, 32>;
	using str64 = array<char, 64>;
	using str80 = array<char, 80>;
	using str128 = array<char, 128>;
	using str256 = array<char, 256>;
	using str512 = array<char, 512>;
	using str1k = array<char, 1024>;

	using strargs = array<txt, 128>; //for ec::strsplit  out buffer

	template<typename charT
		, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
		int stricmp(const charT*s1, const charT*s2)
	{
#ifdef _WIN32
		return ::stricmp(s1, s2);
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

	template<typename charT
		, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
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
		memcpy(dest, src, srcsize);
		dest[srcsize] = '\0';
		return dest;
	}

	template<typename charT
		, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
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

	template<typename charT
		, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
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

	template<typename charT
		, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
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

	template<typename charT
		, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
		const char* strnext(const charT cp, const charT* src, size_t srcsize, size_t &pos, charT *sout, size_t outsize)
	{
		char c;
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

	template<typename charT
		, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
		const char* strnext(const charT* split, const charT* src, size_t srcsize, size_t &pos, charT *sout, size_t outsize)
	{
		char c;
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

	template<typename charT
		, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
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

	template<class  _Str>
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

	template<class _Str>
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

	template<typename charT
		, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
		char *strupr(charT *str)
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

	template<typename charT
		, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
		char *strlwr(charT *str)
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
				else if (c >= 0xC0)
					nb = 1;
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

	template <class _Str>
	int gbk2utf8_s(const char* in, size_t sizein, _Str &sout)
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

	template <class _Str>
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
		char* bin2view(const void* pm, size_t size, charT *so, size_t sizeout)
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

	template<class _STR>
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

	template<class _Out>
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
}// namespace ec
