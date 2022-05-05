/*!
\file ec_jsonx.h
\author	jiangyong
\email  kipway@outlook.com
\update 2022.2.8

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
#include "ec_time.h"
#include "ec_base64.h"
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

		struct t_keys {
			const char* _ks[4] = { nullptr };
			t_keys(const char* s1)
			{
				_ks[0] = s1;
			}
			t_keys(const char* s1, const char* s2)
			{
				_ks[0] = s1;
				_ks[1] = s2;
			}
			t_keys(const char* s1, const char* s2, const char* s3)
			{
				_ks[0] = s1;
				_ks[1] = s2;
				_ks[2] = s3;
			}
			t_keys(const char* s1, const char* s2, const char* s3, const char* s4)
			{
				_ks[0] = s1;
				_ks[1] = s2;
				_ks[2] = s3;
				_ks[3] = s4;
			}
			const char** operator()()
			{
				return _ks;
			}
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

		const txt* getval(const char **keys, size_t zsize)
		{
			size_t i, n = _kvs.size(), j;
			for (i = 0; i < n; i++) {
				for (j = 0; j < zsize; j++) {
					if (_kvs[i]._k.ieq(keys[j]))
						return &_kvs[i]._v;
				}
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

		template<typename _Str>
		void fromjstr(const char* s, size_t srcsize, _Str &sout) // delete escape, "\\" -> '\', ""\'" -> '"' and set to sout
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

		template<typename _VAL>
		bool get_jnumber(const char* key, _VAL &val)
		{
			const ec::json::t_kv *pkv;
			pkv = getkv(key);
			val = 0;
			if (!pkv || pkv->_v.empty())
				return false;
			if (std::is_integral<_VAL>::value)
				val = (_VAL)pkv->_v.stoll();
			else if (std::is_floating_point<_VAL>::value)
				val = (_VAL)pkv->_v.stof();
			return true;
		}

		template<typename _STR>
		bool get_jstring(const char* key, _STR &val)
		{
			const ec::json::t_kv *pkv;
			pkv = getkv(key);
			val.clear();
			if (!pkv)
				return false;
			fromjstr(pkv->_v._str, pkv->_v._size, val);
			return true;
		}

		template<typename _STR>
		bool get_jstr_array(const char* key, std::vector<_STR>& vals)
		{
			const ec::json::t_kv *pkv;
			pkv = getkv(key);
			if (!pkv || pkv->_v.empty())
				return true;
			if (pkv->_type != ec::json::jarray)
				return false;

			ec::json jvs;
			if (!jvs.from_str(pkv->_v._str, pkv->_v._size))
				return false;

			for (auto i = 0u; i < jvs.size(); i++) {
				pkv = jvs.at(i);
				if (pkv && !pkv->_v.empty()) {
					_STR v;
					fromjstr(pkv->_v._str, pkv->_v._size, v);
					vals.push_back(std::move(v));
				}
			}
			return true;
		}

		template<typename _VAL>
		bool get_jnumber_array(const char* key, std::vector<_VAL>& vals)
		{
			const ec::json::t_kv *pkv;
			pkv = getkv(key);
			if (!pkv || pkv->_v.empty())
				return true;
			if (pkv->_type != ec::json::jarray)
				return false;

			ec::json jvs;
			if (!jvs.from_str(pkv->_v._str, pkv->_v._size))
				return false;

			for (auto i = 0u; i < jvs.size(); i++) {
				pkv = jvs.at(i);
				if (pkv && !pkv->_v.empty()) {
					if (std::is_integral<_VAL>::value)
						vals.push_back((_VAL)pkv->_v.stoll());
					else if (std::is_floating_point<_VAL>::value)
						vals.push_back((_VAL)pkv->_v.stof());
				}
			}
			return true;
		}

		template<typename _CLS>
		bool get_jobj_array(const char* key, std::vector<_CLS>& vals)
		{
			const ec::json::t_kv *pkv;
			pkv = getkv(key);
			if (!pkv || pkv->_v.empty())
				return true;
			if (pkv->_type != ec::json::jarray)
				return false;

			ec::json jvs, jv;
			if (!jvs.from_str(pkv->_v._str, pkv->_v._size))
				return false;

			for (auto i = 0u; i < jvs.size(); i++) {
				_CLS v;
				pkv = jvs.at(i);
				if (pkv && !pkv->_v.empty() && jv.from_str(pkv->_v._str, pkv->_v._size) && v.fromjson(jv))
					vals.push_back(std::move(v));
			}
			return true;
		}

		template<typename _STR>
		bool get_jb64(const char* key, _STR& val)
		{
			const ec::json::t_kv* pkv;
			pkv = getkv(key);
			val.clear();
			if (!pkv || pkv->_v.empty())
				return true;

			ec::autobuf<> b64(modp_b64_decode_len(pkv->_v._size));
			int nc = ec::decode_base64(b64.data(), pkv->_v._str, (int)pkv->_v._size);
			if (nc < 0)
				return false;
			val.assign(b64.data(), nc);
			return true;
		}

		template<typename _VAL>
		void get_jipv4(const char* key, _VAL& hostv)
		{
			char sip[40] = { 0 };
			hostv = 0;
			if (!getstr(key, sip, sizeof(sip)))
				return;
			if (!sip[0])
				return;
#ifdef _WIN32
			hostv = (_VAL)ntohl(inet_addr(sip));
#else
			hostv = (_VAL)inet_network(sip);
#endif
		}

		template<typename _VAL>
		void get_jtime(const char* key, _VAL& val)
		{
			const ec::json::t_kv* pkv;
			pkv = getkv(key);
			val = 0;
			if (!pkv || pkv->_v.empty())
				return;
			val = (_VAL)ec::string2jstime(pkv->_v._str, pkv->_v._size);
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

	/*!
	\brief update json string
	\param sjs, in/out JSON string.
	\param key, keyname; for example "submit"
	\param val, value; for example "1"
	\param ty , value type
	\return 0:success; -1: error;
	\remark inert if key is not exist
	*/
	template<class _Str = std::string>
	int updatejson(_Str &sjs, const char* key, const char* val, ec::json::jtype ty)
	{
		_Str jso; //temp for out
		ec::json js;
		if (!js.from_str(sjs.data(), sjs.size()))
			return -1;
		jso.reserve(1024);
		const ec::json::t_kv* pkv;

		bool bupdate = false;
		jso += '{';
		for (auto i = 0u; i < js.size(); i++) {
			pkv = js.at(i);
			if (!pkv || pkv->_k.empty())
				continue;
			if (jso.size() > 1)
				jso += ',';
			jso += '"';
			jso.append(pkv->_k._str, pkv->_k._size);
			jso += "\":";
			if (pkv->_k.ieq(key)) { // replace
				switch (ty) {
				case ec::json::jstring:
					jso += '"';
					jso.append(val);
					jso += '"';
					break;
				default:
					jso.append(val);
					break;
				}
				bupdate = true;
			}
			else {
				switch (pkv->_type) { //copy
				case ec::json::jstring:
					jso += '"';
					jso.append(pkv->_v._str, pkv->_v._size);
					jso += '"';
					break;
				default:
					jso.append(pkv->_v._str, pkv->_v._size);
					break;
				}
			}
		}
		if (!bupdate) { // insert
			if (jso.size() > 1)
				jso += ',';
			jso += '"';
			jso.append(key);
			jso += "\":";
			switch (ty) {
			case ec::json::jstring:
				jso += '"';
				jso.append(val);
				jso += '"';
				break;
			default:
				jso.append(val);
				break;
			}
		}
		jso += '}';
		sjs.swap(jso);
		return 0;
	}
	namespace js {
		template<typename _STRVAL, typename _STROUT>
		void out_jstring(int &nf, const char* key, const _STRVAL& val, _STROUT & sout)
		{
			if (val.empty())
				return;
			if (nf)
				sout.push_back(',');
			++nf;
			sout.push_back('"');
			sout.append(key).append("\":\"");
			if (!ec::jstr_needesc(val.data(), val.size())) {
				sout.append(val.data(), val.size()).push_back('"');
				return;
			}

			const char *s = val.data(), *se = s + val.size();
			while (s < se) {
				if (*s == '\\' || *s == '\"')
					sout += '\\';
				sout += *s++;
			}
			sout.push_back('"');
		}

		template<typename _STROUT>
		void out_jstring(int& nf, const char* key, const char* val, size_t sizeval, _STROUT& sout)
		{
			if (!val || !sizeval || !*val)
				return;
			if (nf)
				sout.push_back(',');
			++nf;
			sout.push_back('"');
			sout.append(key).append("\":\"");
			if (!ec::jstr_needesc(val, sizeval)) {
				sout.append(val, sizeval).push_back('"');
				return;
			}

			const char* s = val, * se = s + sizeval;
			while (s < se) {
				if (*s == '\\' || *s == '\"')
					sout += '\\';
				sout += *s++;
			}
			sout.push_back('"');
		}

		template<typename _VAL, typename _STROUT>
		void out_jnumber(int &nf, const char* key, _VAL val, _STROUT & sout)
		{
			if (std::is_integral<_VAL>::value && !val)
				return; //整数0为默认值不输出
			if (nf)
				sout.push_back(',');
			++nf;
			sout.push_back('"');
			sout.append(key).append("\":");
			sout.append(std::to_string(val).c_str());
		}

		template<typename _CLS, typename _STROUT>
		void out_jobj_array(int &nf, const char* key, _CLS& vals, _STROUT & sout)
		{
			if (nf)
				sout.push_back(',');
			++nf;
			sout.push_back('"');
			sout.append(key).append("\":[");

			int n = 0;
			for (auto &obj : vals) {
				if (n)
					sout.push_back(',');
				obj.tojson(sout);
				++n;
			}
			sout.push_back(']');
		}

		template<typename _STR, typename _STROUT>
		void out_jstr_array(int &nf, const char* key, std::vector<_STR>& vals, _STROUT & sout)
		{
			if (nf)
				sout.push_back(',');
			++nf;
			sout.push_back('"');
			sout.append(key).append("\":[");

			int n = 0;
			for (auto &obj : vals) {
				if (n)
					sout.push_back(',');
				sout.push_back('"');
				ec::out_jstr(obj.data(), obj.size(), sout);
				sout.push_back('"');
				++n;
			}
			sout.push_back(']');
		}

		template<typename _VAL, typename _STROUT>
		void out_jnumber_array(int &nf, const char* key, std::vector<_VAL>& vals, _STROUT & sout)
		{
			if (nf)
				sout.push_back(',');
			++nf;
			sout.push_back('"');
			sout.append(key).append("\":[");

			int n = 0;
			for (auto &v : vals) {
				if (n)
					sout.push_back(',');
				sout.append(std::to_string(v));
				++n;
			}
			sout.push_back(']');
		}
		template<typename _STROUT>
		void out_jtime(int& nf, const char* key, int64_t val, _STROUT& sout)
		{
			if (!val)
				return;
			if (nf)
				sout.push_back(',');
			++nf;
			sout.push_back('"');
			sout.append(key).append("\":\"");
			jstime2localstring(val, sout);
			sout.push_back('"');
		}

		template<typename _STROUT>
		void out_jipv4(int& nf, const char* key, uint32_t hostv, _STROUT& sout)
		{
			if (!hostv)
				return;
			if (nf)
				sout.push_back(',');
			++nf;
			sout.push_back('"');
			sout.append(key).append("\":\"");
			char sip[80];
			snprintf(sip, sizeof(sip), "%u.%u.%u.%u", hostv >> 24, (hostv >> 16) & 0xFF,
				(hostv >> 8) & 0xFF, hostv & 0xFF);
			sout.append(sip);
			sout.push_back('"');
		}

		template<typename _STRVAL, typename _STROUT>
		void out_jb64(int& nf, const char* key, const _STRVAL& val, _STROUT& sout)
		{
			if (val.empty())
				return;
			if (nf)
				sout.push_back(',');
			++nf;
			sout.push_back('"');
			sout.append(key).append("\":\"");

			ec::autobuf<> b64(modp_b64_encode_len(val.size()));//转换为base64;
			int nc = ec::encode_base64(b64.data(), (const char*)val.data(), (int)val.size());
			sout.append(b64.data(), nc);
			sout.push_back('"');
		}
	}//js
}// ec