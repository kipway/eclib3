/*!
\file ec_jsonx.h
\author	jiangyong
\email  kipway@outlook.com
\update 2020.9.19

json
	a fast json parse class

eclib 3.0 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/
#pragma once
#include <string.h>
#include <string>
#include <vector>
#ifndef MAXSIZE_JSONX_KEY
#	define MAXSIZE_JSONX_KEY 63
#endif
#include "ec_text.h"
namespace ec
{
	class json // parse json object, fast no copy
	{
	public:
		enum jtype {
			jstring = 0,
			jobj = 1,
			jarray = 2,
			jnumber = 3
		};

		struct t_kv {
			t_kv() :_type(jstring) {
			}
			txt _k, _v;
			int _type;
		};

	public:
		std::vector<t_kv> _kvs;
	public:
		json(const json&) = delete;
		json& operator = (const json&) = delete;
		json()
		{
			_kvs.reserve(128);
		}
		~json()
		{
		}
		inline size_t size()
		{
			return _kvs.size();
		}

		const t_kv* at(size_t i)
		{
			if (i < _kvs.size())
				return &_kvs[i];
			return nullptr;
		}
		const t_kv* getkv(const txt &key)
		{
			for (const auto &i : _kvs) {
				if (i._k.ieq(key))
					return &i;
			}
			return nullptr;
		}

		const t_kv& operator [](size_t pos)
		{
			return _kvs[pos];
		}

		const txt* getval(const txt &key)
		{
			for (const auto &i : _kvs) {
				if (i._k.ieq(key))
					return &i._v;
			}
			return nullptr;
		}

		template<class _Str>
		bool getstr(const txt &key, _Str &sout)
		{
			const txt* pv = getval(key);
			if (!pv || pv->empty()) {
				sout.clear();
				return false;
			}
			try {
				sout.assign(pv->_str, pv->_size);
			}
			catch (...) {
				return false;
			}
			return true;
		}

		bool getstr(const txt &key, char *sout, size_t outsize)
		{
			const txt* pv = getval(key);
			if (!pv || pv->empty() || pv->_size >= outsize) {
				*sout = '\0';
				return false;
			}
			memcpy(sout, pv->_str, pv->_size);
			sout[pv->_size] = 0;
			return true;
		}

		bool from_str(txt &s)
		{
			_kvs.clear();
			if (s.empty())
				return false;
			if (!s.skip())
				return false;
			if (*s._str == '[')
				return from_array(s);
			else if (*s._str == '{')
				return from_obj(s);
			return false;
		}

		inline bool from_str(const char* s, size_t size)
		{
			txt t(s, size);
			return from_str(t);
		}

		static bool load_file(const char *sfile, std::string &sout)
		{
			if (!sfile)
				return false;
			FILE *pf = fopen(sfile, "rt");
			if (!pf)
				return false;
			int c = fgetc(pf), c2 = fgetc(pf), c3 = fgetc(pf);
			if (!(c == 0xef && c2 == 0xbb && c3 == 0xbf)) // not utf8 with bom
				fseek(pf, 0, SEEK_SET);
			char s[1024 * 8];

			sout.reserve(1024 * 16);
			size_t sz;
			while ((sz = fread(s, 1, sizeof(s), pf)) > 0)
				sout.append(s, sz);
			fclose(pf);
			return true;
		}

		static void del_comment(const char* pin, size_t inlen, std::string &sout)
		{
			size_t n = inlen;
			const char* s = pin, *sp = s;
			while (n) {
				if (*s == '/') {
					if (s != pin && *(s - 1) == '*' && !sp)  // */
						sp = s + 1;
				}
				else if (*s == '*') {
					if (s != pin && *(s - 1) == '/') { // /*
						if (sp && s > sp + 1)
							sout.append(sp, s - sp - 1);
						sp = nullptr;
					}
				}
				s++;
				n--;
			}
			if (sp && s > sp + 1)
				sout.append(sp, s - sp);
		}
	private:
		bool from_obj(txt &s)
		{
			t_kv  it;
			if (*s._str != '{')
				return false;
			s.tonext();
			while (!s.empty()) {
				if (!s.skip()) // to key start
					return false;
				if (*s._str == ',') {
					s.tonext();
					continue;
				}
				else if (*s._str == '}')
					return true;
				if (*s._str != '"')
					return false;
				it._k.clear();
				it._v.clear();
				s.tonext();
				it._k = s; // key
				if (!s.json_tochar('"'))  //to key end
					return false;
				it._k._size = s._str - it._k._str;
				if (!it._k._size || it._k._size > MAXSIZE_JSONX_KEY) //check key
					return false;
				if (!s.json_tochar(':')) // move to valuse
					return false;
				s.tonext();
				if (!s.skip())
					return false;
				if (*s._str == '"') { //string
					s.tonext();
					it._type = jstring;
					it._v = s;
					if (!s.json_tochar('"'))
						return false;
					it._v._size = s._str - it._v._str;
					s.tonext();
				}
				else if (*s._str == '{') { // object
					it._v = s;
					it._type = jobj;
					s.tonext();
					if (!s.json_toend('{', '}'))
						return false;
					it._v._size = s._str - it._v._str;
				}
				else if (*s._str == '[') { // array
					it._v = s;
					it._type = jarray;
					s.tonext();
					if (!s.json_toend('[', ']'))
						return false;
					it._v._size = s._str - it._v._str;
				}
				else { //number
					it._v = s;
					it._type = jnumber;
					if (!s.json_tochar(",}"))
						return false;
					it._v._size = s._str - it._v._str;
				}
				_kvs.push_back(it);
			}
			return false;
		}
		bool from_array(txt &s)
		{
			t_kv  it;
			if (*s._str != '[')
				return false;
			s.tonext();
			while (!s.empty()) {
				if (!s.skip())
					return false;
				it._v.clear();
				if (*s._str == ']') // end
					return true;
				else if (*s._str == ',') {
					s.tonext();
					continue;
				}
				else if (*s._str == '"') { //string
					s.tonext();
					it._type = jstring;
					it._v = s;
					if (!s.json_tochar('"'))
						return false;
					it._v._size = s._str - it._v._str;
					s.tonext();
				}
				else if (*s._str == '{') { // object
					it._v = s;
					it._type = jobj;
					s.tonext();
					if (!s.json_toend('{', '}'))
						return false;
					it._v._size = s._str - it._v._str;
				}
				else if (*s._str == '[') { // array
					it._v = s;
					it._type = jarray;
					s.tonext();
					if (!s.json_toend('[', ']'))
						return false;
					it._v._size = s._str - it._v._str;
				}
				else { // number valuse
					it._v = s;
					it._type = jnumber;
					if (!s.json_tochar(",]"))
						return false;
					it._v._size = s._str - it._v._str;
				}
				if (!it._v.empty())
					_kvs.push_back(it);
			}
			return false;
		}
	};//json
}// ec