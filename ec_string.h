/*!
\file ec_string.h
\author	jiangyong
\email  kipway@outlook.com
\update 
2023.5.15 move ec::string to ec_string.hpp

string tips

eclib 3.0 Copyright (c) 2017-2023, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/

#pragma once
#include <memory.h>
#include <string>
#include <ctype.h>
#include <type_traits>

#ifndef _WIN32
#include <iconv.h>
#endif

#define _to_upper(a) (((a) >= 'a' && (a) <= 'z') ? (a)-32 : (a))	// ('a'-'A') = 32
#define _to_lower(a) (((a) >= 'A' && (a) <= 'Z') ? (a)+32 : (a))	// ('a'-'A') = 32

#include "ec_text.h"
#include "ec_array.h"
#include "ec_alloctor.h"
#include "ec_string.hpp"
namespace std
{
	using bytes = basic_string<uint8_t, char_traits<uint8_t>, ec::std_allocator<uint8_t>>; // std::bytes
	using mstring = basic_string<char, char_traits<char>, ec::std_allocator<char>>; //self allocator std::string
}

#define WIN_CP_GBK  936

namespace ec
{
	using str32 = array<char, 32>;
	using str64 = array<char, 64>;
	using str80 = array<char, 80>;
	using str128 = array<char, 128>;
	using str256 = array<char, 256>;
	using str512 = array<char, 512>;
	using str1k = array<char, 1024>;

	using strargs = array<txt, 128>; //for ec::strsplit  out buffer
	
	inline int stricmp(const char*s1, const char*s2)
	{
		if (s1 == s2)
			return 0;
#ifdef _WIN32
		return ::_stricmp(s1, s2);
#else
		return ::strcasecmp(s1, s2);
#endif
	}

	inline 	size_t strlcpy(char* sd, const char* ss, size_t count)
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

	inline char *strncpy_s(char *dest, size_t destsize, const char* src, size_t srcsize)
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

	inline bool streq(const char* s1, const char* s2)
	{ // return true: equal; false not equal
		if (!s1 || !s2)
			return false;
		while (*s1 && *s2) {
			if (*s1++ != *s2++)
				return false;
		}
		return *s1 == '\0' && *s2 == '\0';
	}

	inline bool strieq(const char* s1, const char* s2)
	{ //case insensitive equal. return true: equal; false not equal
		if (s1 == s2)
			return true;
		if (!s1 || !s2)
			return false;

#ifdef _WIN32
			return (stricmp(s1, s2) == 0);
#else
			return (strcasecmp(s1, s2) == 0);
#endif
	}

	inline bool strineq(const char* s1, const char* s2, size_t s2size, bool balls1 = false)// Judge n characters equal case insensitive
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
	
	inline void strtrim(char *s, const char* flt = "\x20\t\n\r")
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
	inline const char* strnext(const char cp, const char* src, size_t srcsize, size_t &pos, char *sout, size_t outsize)
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
					return nullptr;
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
		return nullptr;
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

	inline const char* strnext(const char* split, const char* src, size_t srcsize, size_t &pos, char *sout, size_t outsize)
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
					return nullptr;
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
		return nullptr;
	}

	inline bool char2hex(char c, unsigned char *pout)
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
	
	inline size_t struppercpy(char* sd, const char* ss, size_t count)
	{// like strlcpy for linux,add null to the end of sd,return strlen(ss)
		char* d = sd;
		const char* s = ss;
		size_t n = count;
		char ch;

		if (!d)
			return 0;

		if (!s) {
			*d = 0;
			return 0;
		}
		/* Copy as many bytes as will fit */
		if (n != 0 && --n != 0) {
			do {
				ch = *s++;
				if ((*d++ = _to_upper(ch)) == 0)
					break;
			} while (--n != 0);
		}

		/* Not enough room in dst, add NUL and traverse rest of src */
		if (n == 0) {
			if (count != 0)
				*d = '\0';  /* NUL-terminate dst */
			while (*s++)
				;
		}

		return(s - ss - 1); /* count does not include NUL */
	}

	inline size_t strlowercpy(char* sd, const char* ss, size_t count)
	{// like strlcpy for linux,add null to the end of sd,return strlen(ss)
		char* d = sd;
		const char* s = ss;
		size_t n = count;
		char ch;

		if (!d)
			return 0;

		if (!s) {
			*d = 0;
			return 0;
		}

		/* Copy as many bytes as will fit */
		if (n != 0 && --n != 0) {
			do {
				ch = *s++;
				if ((*d++ = _to_lower(ch)) == 0)
					break;
			} while (--n != 0);
		}

		/* Not enough room in dst, add NUL and traverse rest of src */
		if (n == 0) {
			if (count != 0)
				*d = '\0';   /* NUL-terminate dst */
			while (*s++)
				;
		}
		return(s - ss - 1);  /* count does not include NUL */
	}

	inline bool strisutf8(const char* s, size_t size = 0)
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
				if (0xc0 == c || 0xc1 == c || c > 0xF4) // RFC 3629
					return false;
				if ((c & 0xFC) == 0xFC) // 1111 1100
					nb = 5;
				else if ((c & 0xF8) == 0xF8) // 1111 1000
					nb = 4;
				else if ((c & 0xF0) == 0xF0) // 1111 0000
					nb = 3;
				else if ((c & 0xE0) == 0xE0) // 1110 1000
					nb = 2;
				else if ((c & 0xC0) == 0xC0) // 1100 0000
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
	
	inline bool strisascii(const char* s, size_t size = 0)
	{// If s is a pure ASCII code or a utf8 string containing only ASCII, it will return true
		if (!s)
			return true;
		const char* pend = s + (size ? size : strlen(s));
		while (s < pend) {
			if (*s & 0x80)
				return false;
			++s;
		}
		return true;
	}

	inline int utf82gbk(const char* in, size_t sizein, char *out, size_t sizeout)
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

	
	inline int gbk2utf8(const char* in, size_t sizein, char *out, size_t sizeout)
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
	int gbk2utf8_s(const char* in, size_t sizein, _Str &sout)
	{ // return sout.zize() or -1 error;
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

	inline void hex2str(const void* psrc, size_t sizesrc, char *sout, size_t outsize)
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

	inline void xor_le(unsigned char* pd, int size, unsigned int umask)
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

	inline int hexview16(const void* psrc, int srclen, char * sout, size_t sizeout, size_t *pzoutsize = nullptr)
	{ //view 16 bytes，return do bytes
		if (pzoutsize)
			*pzoutsize = 0;
		*sout = 0;
		if (srclen <= 0 || sizeout < 88u)
			return -1;
		int i, k = 0, n = srclen > 16 ? 16 : srclen;
		unsigned char ul, uh;
		const unsigned char* s = (const unsigned char*)psrc;
		sout[k++] = '\x20';
		sout[k++] = '\x20';
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
		if (pzoutsize)
			*pzoutsize = k;
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
	inline const char* bin2view(const void* pm, size_t size, char *so, size_t sizeout)
	{
		if (!so)
			return nullptr;
		*so = 0;
		if (sizeout < 6 || !size)
			return so;
		char stmp[256];
		int ndo = 0, n = (int)size, nd;
		size_t zlen = 0, zall = 0;
		const uint8_t* p = (uint8_t*)pm;
		while (n - ndo > 0) {
			nd = ec::hexview16(p + ndo, n - ndo, stmp, sizeof(stmp), &zlen);
			if (nd <= 0)
				break;
			if (zall + zlen >= sizeout)
				break;
			memcpy(so + zall, stmp, zlen);
			zall += zlen;
			ndo += nd;
		}
		*(so + zall) = 0;
		return so;
	}

	/*!
	\brief view bytes
	like this
	0000-000F 16 03 03 00 33 01 00 00    2F 03 03 39 7F 29 AE 20    ....3.../..9.).
	0010-001F 8D 03 12 61 52 0A 2E 02    86 13 66 CA 3C 7E 6A 54    ...aR.....f.<~jT
	0020-002F 39 D2 CD 22 D6 A7 2C 08    EF F4 BC 00 00 08 00 3D    9.."..,........=
	0030-003F 00 3C 00 35 00 2F 01 00                               .<.5./..
	*/
	template<class _STR = std::string>
	void bin2view(const void* pm, size_t size, _STR& sout) {
		int ndo = 0, nall = size, n;
		size_t zr;
		const uint8_t* pu = (const uint8_t*)pm;
		char stmp[256];
		while (ndo < nall) {
			zr = snprintf(stmp, sizeof(stmp), "%04X-%04X", ndo, ndo + 15);
			sout.append(stmp, zr);
			n = ec::hexview16(pu + ndo, nall - ndo, stmp, sizeof(stmp), &zr);
			if (n <= 0)
				break;
			sout.append(stmp, zr);
			ndo += n;
		}
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
	inline bool strfilter(const char *ssrc, const char *sfliter)
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

	inline size_t utf8_substr(char *s, size_t size, size_t sublen)
	{ // truncate string no greater than sublen, return substr size
		if (size <= sublen)
			return size;
		if (!s || !*s || !size)
			return 0;
		uint8_t uc;
		size_t pos = size - 1;
		while (pos > 0) {
			uc = s[pos];
			if ((uc < 0x80 || uc >= 0xC0) && pos <= sublen)// uc >= 0xC0 mean the first utf8 byte
				break;
			--pos;
		}
		s[pos] = 0;
		return pos;
	}

	inline size_t utf8_sizesubstr(const char* s, size_t size, size_t sublen)
	{ // return substr size no greater than sublen
		if (size < sublen)
			return size;
		if (!s || !*s || !size)
			return 0;
		uint8_t uc;
		size_t pos = size - 1;
		while (pos > 0) {
			uc = s[pos];
			if ((uc < 0x80 || uc >= 0xC0) && pos <= sublen) // uc >= 0xC0 mean the first utf8 byte
				break;
			--pos;
		}
		return pos;
	}

	inline size_t utf8cpy(char* sd, size_t sized, const char* ss, size_t sizes)
	{ //add null to end, return copy size
		if (!ss || !(*ss)) {
			if (sd && sized)
				*sd = '\0';
			return 0;
		}
		if (!sd || !sized)
			return 0;
		size_t zcp = utf8_sizesubstr(ss, sizes, sized);
		if (zcp)
			memcpy(sd, ss, zcp);
		sd[zcp] = 0;
		return zcp;
	}

	inline size_t utf8_strlcpy(char* sd, const char* ss, size_t count)
	{// like strlcpy for linux,add null to the end of sd,return strlen(ss), count is sd size
		if (!ss || !(*ss)) {
			if (sd && count)
				*sd = '\0';
			return 0;
		}
		const size_t srclen = strlen(ss);
		if (!sd || !count)
			return srclen;

		size_t zcp = utf8_sizesubstr(ss, srclen, count);
		if (zcp)
			memcpy(sd, ss, zcp);
		sd[zcp] = 0;
		return srclen;
	}

	inline bool jstr_needesc(const char* src, size_t srcsize)
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
	const char* jstr_toesc(const char* s, size_t srcsize, _Str &so)
	{ //escape  '\' -> '\\', '"' -> '\"'
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
	const char* jstr_fromesc(const char* s, size_t srcsize, _Str &so)
	{ // delete escape, "\\" -> '\', ""\'" -> '"'  so
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

	inline bool strneq(const char* s1, const char* s2, size_t  n)
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

	inline bool strnieq(const char* s1, const char* s2, size_t  n)
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
	void out_jstr(const char* s, size_t srcsize, _Str &sout)
	{ //escape and append  to so,  escape  '\' -> '\\', '"' -> '\"' in s
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
	void from_jstr(const char* s, size_t srcsize, _Str &sout)
	{ // delete escape, "\\" -> '\', ""\'" -> '"' and set to sout
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
