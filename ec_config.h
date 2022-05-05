/*!
\file ec_config.h
\author	jiangyong
\email  kipway@outlook.com
\update 2021.10.12

namespace cfg
	tools for ini, config file.
namespace csv
	tools csv file.

eclib 3.0 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/
#pragma once

#ifdef _WIN32
#pragma warning (disable : 4996)
#endif // _WIN32

#include <functional>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include "ec_string.h"
namespace ec
{
	class rstream_str // read only string stream
	{
	public:
		rstream_str(const char* pstr, size_t zlen) :_s(pstr), _size(zlen), _pos(0) {
		}
		bool available()
		{
			return nullptr != _s;
		}
		int getc()
		{
			if (_pos >= _size)
				return EOF;
			return _s[_pos++];
		}
		void seek(long pos, int wh)
		{
			switch (wh) {
			case SEEK_SET:
				_pos = pos;
				break;
			case SEEK_CUR:
				_pos += pos;
				break;
			case SEEK_END:
				_pos = _size + pos;
				break;
			}
			if (_pos > _size)
				_pos = _size;
		}
		long tell()
		{
			return (long)_pos;
		}

	private:
		const char* _s;
		size_t _size, _pos;
	};

	class rstream_file // read only file stream
	{
	public:
		rstream_file(const char* sfilename) :_pf(nullptr) {
			if (!sfilename || !*sfilename)
				return;
#ifdef _WIN32
			UINT codepage = ec::strisutf8(sfilename) ? CP_UTF8 : CP_ACP;
			wchar_t sfile[_MAX_PATH];
			sfile[0] = 0;
			_pf = MultiByteToWideChar(codepage, 0, sfilename, -1, sfile, sizeof(sfile) / sizeof(wchar_t)) ? _wfopen(sfile, L"rt") : nullptr;
#else
			_pf = ::fopen(sfilename, "rt");
#endif
		}
		~rstream_file() {
			if (_pf) {
				fclose(_pf);
				_pf = nullptr;
			}
		}
		bool available()
		{
			return nullptr != _pf;
		}
		int getc()
		{
			if (!_pf)
				return EOF;
			return fgetc(_pf);
		}
		void seek(long pos, int wh)
		{
			fseek(_pf, (long)pos, wh);
		}
		long tell()
		{
			return ftell(_pf);
		}
	private:
		FILE* _pf;
	};

	namespace csv
	{
		template<class rstream>
		int scan(rstream* pf, std::function<int(int nrow, int ncol, const char* stxt, bool bendline)>fun)
		{ // fun return 0: continue; Non-zero: stop scan
			if (!pf)
				return -1;
			int c = pf->getc(), c2 = pf->getc(), c3 = pf->getc();
			if (!(c == 0xef && c2 == 0xbb && c3 == 0xbf)) // not utf8 with bom
				pf->seek(0, SEEK_SET);

			char stmp[4096];
			int nr = 0, nc = 0, nstr = 0, nerr = 0, cnext;
			unsigned int np = 0;

			while ((c = pf->getc()) != EOF) {
				if (c == ',') {
					if (!nstr) {
						stmp[np] = 0;
						if (0 != (nerr = fun(nr, nc, stmp, false)))
							break;
						nc++;   np = 0;
					}
					else {
						if (np < sizeof(stmp) - 1)
							stmp[np++] = c;
					}
				}
				else if (c == '\n') {
					stmp[np] = 0;
					if (0 != (nerr = fun(nr, nc, stmp, true)))
						break;
					nr++; nc = 0; np = 0;
				}
				else if (c == '"') {
					cnext = pf->getc();
					if (cnext == EOF)
						break;
					if (cnext == '"') {
						if (np < sizeof(stmp) - 1)
							stmp[np++] = c;
					}
					else {
						pf->seek(-1, SEEK_CUR);
						if (nstr)
							nstr = 0;
						else
							nstr++;
					}
				}
				else {
					if (c != '\r' && c != '\t' && np < sizeof(stmp) - 1)
						stmp[np++] = c;
				}
			}
			if (!nerr && np > 0) {
				stmp[np] = 0;
				nerr = fun(nr, nc, stmp, true);
			}
			return nerr;
		}

		inline int scanstring(const char* str, size_t strsize, std::function<int(int nrow, int ncol, const char* stxt, bool bendline)>fun)
		{
			rstream_str fs(str, strsize);
			return !fs.available() ? -1 : scan(&fs, fun);
		}

		inline int scanfile(const char* sfile, std::function<int(int nrow, int ncol, const char* stxt, bool bendline)>fun)
		{
			rstream_file fs(sfile);
			return !fs.available() ? -1 : scan(&fs, fun);
		}
	} //namespace csv

	namespace cfg
	{
		template <class rstream, class _Str>
		bool setval(rstream* pf,
			std::function<int(const std::string& blk, const std::string& key, std::string& newv)>fun,
			_Str& so)
		{ // fun return Non-zero replace
			so.clear();
			so.reserve(1024 * 8);
			if (!pf)
				return false;
			int c = pf->getc(), c2 = pf->getc(), c3 = pf->getc();
			if (!(c == 0xef && c2 == 0xbb && c3 == 0xbf)) // not utf8 with bom
				pf->seek(0, SEEK_SET);
			else {
				so += c;
				so += c2;
				so += c3;
			}
			std::string blk, key, newv;
			while ((c = pf->getc()) != EOF) {
				so += c;
				switch (c) {
				case '#':
				case ';':
					while ((c = pf->getc()) != EOF) {
						so += c;
						if ('\n' == c || '\r' == c)
							break;
					}
					key.clear();
					break;
				case  '[':
					blk.clear();
					while ((c = pf->getc()) != EOF) {
						so += c;
						if (']' == c || '\n' == c || '\r' == c)
							break;
						blk += c;
					}
					break;
				case '=':
					newv.clear();
					if (!key.empty() && fun(blk, key, newv)) {
						so += newv;
						while ((c = pf->getc()) != EOF) {
							if ('#' == c || ';' == c || '\n' == c || '\r' == c)
								break;
						}
					}
					else
						so.pop_back();
					while (EOF != c) {
						so += c;
						if ('\n' == c || '\r' == c)
							break;
						c = pf->getc();
					}
					key.clear();
					break;
				default:
					if ('\x20' != c && '\t' != c && '\r' != c && '\n' != c)
						key += c;
					break;
				}
			}
			return true;
		}

		template <class _Str>
		bool setval(const char* sfile,
			std::function<int(const std::string& blk, const std::string& key, std::string& newv)>fun,
			_Str& so)
		{
			rstream_file fs(sfile);
			if (!fs.available())
				return false;
			return setval(&fs, fun, so);
		}

		template <class _Str>
		bool setval(const _Str& instr,
			std::function<int(const std::string& blk, const std::string& key, std::string& newv)>fun,
			_Str& so)
		{
			rstream_str fs(instr.data(), instr.size());
			if (!fs.available())
				return false;
			return setval(&fs, fun, so);
		}
		inline bool iscommentchar(int c, int commentchar = 0) {
			return ('#' == c || ';' == c) && (0 == commentchar || c == commentchar);
		}
		template<class rstream>
		bool scan(rstream* pf, std::function<int(const std::string& blk, const std::string& key, const std::string& val)>fun, int commentchar = 0)
		{ // fun return 0: continue; Non-zero: stop scan
			if (!pf)
				return false;
			int c = pf->getc(), c2 = pf->getc(), c3 = pf->getc();
			if (!(c == 0xef && c2 == 0xbb && c3 == 0xbf)) // not utf8 with bom
				pf->seek(0, SEEK_SET);

			std::string blk, key, val;
			while ((c = pf->getc()) != EOF) {
				switch (c) {
				case '#':
				case ';':
					if (iscommentchar(c, commentchar)) {
						while ((c = pf->getc()) != EOF) {
							if ('\n' == c || '\r' == c)
								break;
						}
						key.clear();
					}
					else
						key += c;
					break;
				case  '[':
					blk.clear();
					while ((c = pf->getc()) != EOF) {
						if (']' == c || '\n' == c || '\r' == c) {
							key.clear();
							val.clear();
							if (fun(blk, key, val))
								return true;
							break;
						}
						blk += c;
					}
					break;
				case '=':
					val.clear();
					while ((c = pf->getc()) != EOF) {
						if (iscommentchar(c, commentchar) || '\n' == c || '\r' == c)
							break;
						if (!val.empty() || ('\x20' != c && '\t' != c))
							val += c;
					}
					val.erase(val.find_last_not_of("\x20\t") + 1);
					if (fun(blk, key, val))
						return true;
					while (EOF != c) {
						if ('\n' == c)
							break;
						c = pf->getc();
					}
					key.clear();
					break;
				default:
					if ('\x20' != c && '\t' != c && '\r' != c && '\n' != c)
						key += c;
					break;
				}
			}
			return true;
		}

		inline bool scanstring(const char* str, size_t zlen, std::function<int(const std::string& blk, const std::string& key, const std::string& val)>fun, int commentchar = 0)
		{
			rstream_str fs(str, zlen);
			return !fs.available() ? false : scan(&fs, fun, commentchar);
		}

		inline bool scanfile(const char* sfile, std::function<int(const std::string& blk, const std::string& key, const std::string& val)>fun, int commentchar = 0)
		{
			rstream_file fs(sfile);
			return !fs.available() ? false : scan(&fs, fun, commentchar);
		}
	}// namespace cfg
}; // ec

/*
void tstcfg()
{
	ec::cfg::scanfile("./scenter.ini", [](const std::string &blk, const std::string &key, const std::string &val) {
		printf("[%s] %s = %s\n",blk.c_str(),key.c_str(),val.c_str());
		return 0;
	});

	ec::csv::scanfile("./123.csv", [](int nrow, int ncol, const char* stxt, bool bendline) {
		if(bendline)
			printf("%s\n", stxt);
		else
			printf("%s,", stxt);
		return 0;
	});
}
*/