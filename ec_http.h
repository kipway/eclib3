/*!
\file ec_http.h
\author	jiangyong
\email  kipway@outlook.com
\update 
2023.8.10 add ec::http::package::headinfo()
2023.5.30 update mimecfg
2023.5.13 use zlibe self memory allocator
classes for HTTP protocol parse

eclib 3.0 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/
#pragma once
#include <string.h>
#include <ctype.h>
#ifndef _WIN32
#include <sys/stat.h>
#endif
#include "zlib/zlib.h"
#include "ec_base64.h"
#include "ec_array.h"
#include "ec_config.h"
#include "ec_map.h"

#ifndef MAXSIZE_RCVHTTPBODY
#define MAXSIZE_RCVHTTPBODY (1024 * 1024)
#endif

namespace ec
{
	enum httpstatus {
		he_ok = 0,
		he_waitdata,
		he_failed
	};

	constexpr const char* html_404 = "<!DOCTYPE html><html><body><p>404 not fund</p></body></html>";
	constexpr const char* html_400 = "<!DOCTYPE html><html><body><p>400 Bad Request</p></body></html>";
	constexpr const char* html_413 = "<!DOCTYPE html><html><body><p>413 Request Entity Too Large</p></body></html>";

	/*!
	\brief mimecfg config

	[mime]
	.323 = text/h323
	.3gp = video/3gpp
	.html = text/html
	*/

	class mimecfg
	{
	public:
		struct t_mime {
			char sext[16];
			char stype[80];
		};
		struct t_keq_mine {
			bool operator()(const char* key, const t_mime& val)
			{
				return ec::strieq(key, val.sext);
			}
		};
		mimecfg(const mimecfg&) = delete;
		mimecfg& operator = (const mimecfg&) = delete;
		mimecfg() : _mime(512)
		{
		};
		virtual ~mimecfg()
		{
			_mime.clear();
		};
	public:
		hashmap<const char*, t_mime, t_keq_mine, ec::del_mapnode<t_mime>, ec::hash_istr> _mime;
	public:
		template<class _Str>
		bool getmime(const char* sext, _Str& so) noexcept
		{
			t_mime t;
			if (!_mime.get(sext, t))
				return false;
			try {
				so = t.stype;
			}
			catch (...) {
				return false;
			}
			return true;
		}
		bool Load(const char* sfile)
		{
			_mime.clear();
			ec::config<ec::string>  cfg;
			return cfg.scanfile(sfile, [&](const ec::string & blk, const ec::string & key, const ec::string & val) {
				if (strieq("mime", blk.c_str()) && key.size() && val.size()) {
					t_mime t;
					memset(&t, 0, sizeof(t));
					ec::strlcpy(t.sext, key.c_str(), sizeof(t.sext));
					ec::strlcpy(t.stype, val.c_str(), sizeof(t.stype));
					_mime.set(t.sext, t);
				}
				return 0;
			});
		}
	};

	namespace http
	{
		constexpr int e_wait = 0;  // waiting more data
		constexpr int e_err = -1;  // normal error
		constexpr int e_line = -2; // line not \r\n  end
		constexpr int e_linesize = -3;  // line length over 512
		constexpr int e_method = -4; // method error
		constexpr int e_url = -5;  // URL error
		constexpr int e_ver = -6;  // version error not "http/1.1."
		constexpr int e_head = -7; // head item error
		constexpr int e_bodysize = -8; // body size "Content-Length" error

		template<typename charT
			, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
			bool isdir(const charT* utf8name)
		{
#ifdef _WIN32
			wchar_t wfile[512];
			wfile[0] = 0;
			if (!MultiByteToWideChar(ec::strisutf8(utf8name) ? CP_UTF8 : CP_ACP, 0, utf8name, -1, wfile, sizeof(wfile) / sizeof(wchar_t)))
				return false;
			struct __stat64 st;
			if (_wstat64(wfile, &st))
				return false;
			if (st.st_mode & S_IFDIR)
				return true;
			return false;
#else
			struct stat st;
			if (stat(utf8name, &st))
				return false;
			if (st.st_mode & S_IFDIR)
				return true;
			return false;
#endif
		}

		template<typename charT
			, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
			bool iszipfile(const charT *sext)
		{
			const char* s[] = { ".zip", ".rar", ".tar", ".7z", ".gz", ".jpg", ".jpeg", ".gif", ".arj", ".jar" };
			size_t i = 0;
			for (i = 0; i < sizeof(s) / sizeof(const char*); i++) {
				if (ec::strieq(sext, s[i]))
					return true;
			}
			return false;
		}

		template<typename charT
			, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
			const char *file_extname(const charT*s)
		{
			const char *pr = NULL;
			while (*s) {
				if (*s == '.')
					pr = s;
				s++;
			}
			return pr;
		}

		template<class _Out>
		void outlongstr(long long v, _Out* pout)
		{
			pout->clear();
			char s[64] = { 0 };
			size_t zn = -1;
			long long l = v / 1000, lv = 1;
			while (l) {
				lv *= 1000;
				l /= 1000;
			}
			while (lv > 1) {
				if (pout->size())
					zn = snprintf(s, sizeof(s), ",%03lld", v / lv);
				else
					zn = snprintf(s, sizeof(s), "%lld", v / lv);
				pout->append(s, zn);
				v %= lv;
				lv /= 1000;
			}
			if (pout->size())
				zn = snprintf(s, sizeof(s), ",%03lld", v);
			else
				zn = snprintf(s, sizeof(s), "%lld", v);
			pout->append(s, zn);
			pout->push_back('\0');
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

		template<class _Out>
		void urldecode(const char* url, int size, _Out* pout)
		{
			const char* end = url + size;
			unsigned char h, l;
			while (url < end) {
				if (*url == '%') {
					url++;
					if (url >= end || !char2hex(*url++, &h))
						break;
					if (url >= end || !char2hex(*url++, &l))
						break;
					pout->push_back((h << 4) | l);
				}
				else if (*url == '+') {
					pout->push_back(0x20);
					url++;
				}
				else
					pout->push_back(*url++);
			}
		}

		template<class _Out>
		void urlencode(const char* url, int size, _Out* pout)
		{
			unsigned char h, l, *p = (unsigned char*)url, *end = p + size;
			while (p > end) {
				if (*p == '\x20') {
					pout->push_back('+');
					p++;
				}
				else if (*p & 0x80) {
					pout->push_back('%');
					h = (*p & 0xF0) >> 4;
					l = *p & 0x0F;
					if (h >= 10)
						pout->push_back('A' + h - 10);
					else
						pout->push_back('0' + h);
					if (l >= 10)
						pout->push_back('A' + l - 10);
					else
						pout->push_back('0' + l);
					p++;
				}
				else
					pout->push_back(*p++);
			}
		}

		class ctxt // const text with length
		{
		public:
			const char* _s;
			size_t _size;
		public:
			ctxt() :_s(nullptr), _size(0)
			{
			}
			ctxt(const char* s, size_t size) : _s(s), _size(size)
			{
			}
			ctxt(const char *s) : ctxt()
			{
				if (s) {
					_s = s;
					_size = strlen(s);
				}
			}
			inline void clear()
			{
				_s = nullptr;
				_size = 0;
			}
			inline bool empty()
			{
				return !_size || !_s;
			}
			inline bool is_endline()
			{
				return (_s && _size == 2 && _s[0] == '\r' && _s[1] == '\n');
			}
			inline bool is_multiline()
			{
				return (_s && _size > 0 && (_s[0] == '\x20' || _s[0] == '\t'));
			}
			void trim()
			{
				char c;
				while (_size > 0) { // left
					c = *_s;
					if (c != '\x20' && c != '\t')
						break;
					_s++;
					_size--;
				}
				while (_size > 0) { // right
					c = *(_s + _size - 1);
					if (c != '\r' && c != '\n' && c != '\x20' && c != '\t')
						break;
					_size--;
				}
			}
			bool ieq(const ctxt &v) const // Case insensitive equal
			{
				if (v._size != _size)
					return false;
				size_t i = _size;
				const char *s1 = v._s, *s2 = _s;
				while (i) {
					if (*s1 != *s2 && tolower(*s1) != tolower(*s2))
						return false;
					++s1;
					++s2;
					i--;
				}
				return true;
			}
			int get(char *pout, size_t sizeout) const // return get chars
			{
				if (!_s || _size >= sizeout)
					return 0;
				memcpy(pout, _s, _size);
				pout[_size] = 0;
				return (int)_size;
			}
			template<class _Str>
			int get(_Str &sout) const // return get chars
			{
				if (!_s || !_size) {
					sout.clear();
					return 0;
				}
				try {
					sout.assign(_s, _size);
				}
				catch (...) {
					return 0;
				}
				return (int)sout.size();
			}

			bool get2c(ctxt* pout, const char c) // get to c and skip c
			{
				pout->_s = _s;
				pout->_size = 0;
				while (_size > 0) {
					if (*_s == c) {
						_s++;
						_size--;
						return pout->_size > 0;
					}
					_s++;
					_size--;
					pout->_size++;
				}
				return false;
			}
			bool get2sp(ctxt* pout) // get to space '\x20' or '\t'
			{
				pout->_s = _s;
				pout->_size = 0;
				while (_size > 0) {
					if (*_s == '\x20' || *_s == '\t')
						return pout->_size > 0;
					_s++;
					_size--;
					pout->_size++;
				}
				return pout->_size > 0;
			}
			int getline(ctxt* pout) //return <0 : error ; 0 : e_wait; >0 : line size include line end "\r\n"
			{
				pout->_s = _s;
				pout->_size = 0;
				while (_size > 1) {
					if (*_s == '\r') {
						if (*(_s + 1) != '\n')
							return e_head;
						_s += 2;
						_size -= 2u;
						pout->_size += 2u;
						return (pout->_size > 1024) ? e_linesize : (int)pout->_size;
					}
					_s++;
					_size--;
					pout->_size++;
				}
				return (pout->_size > 1024) ? e_linesize : e_wait;
			}
			int skip()// skip char \x20 , \t , \r, \n
			{
				int n = 0;
				while (_size > 0) {
					if (*_s == '\n')
						n++;
					else if (*_s != '\x20' && *_s != '\t' && *_s != '\r')
						break;
					_s++;
					_size--;
				}
				return n;
			};
			bool headitem(ctxt* key, ctxt *val)//parse to key and value
			{
				if (!get2c(key, ':'))
					return false;
				skip();
				val->_s = _s;
				val->_size = _size;
				key->trim();
				return !(key->empty() || val->empty());
			}

			int long stoi() const // atoi()
			{
				char s[32];
				if (!_s || !_size)
					return 0;
				size_t n = _size > 31 ? 31 : _size;
				memcpy(s, _s, n);
				s[n] = 0;
				return atoi(s);
			}
		};

		/*!
		\brief parse request_line
		*/
		class req_line
		{
		public:
			req_line()
			{
			}
			ctxt _method, _url, _ver;
		public:
			inline void clear()
			{
				_method.clear();
				_url.clear();
				_ver.clear();
			}
			int parse(const char* sl, size_t size)// return <0 : error ; >0 : line size
			{
				if (!sl || !size)
					return e_err;
				ctxt _s(sl, size);
				if (!_s.get2sp(&_method))
					return e_method;
				if (_method.ieq("HTTP/1.1") || _method.ieq("HTTP/1.0")) { // response
					_s.skip();
					if (!_s.get2sp(&_url)) // code
						return e_url;
					_s.skip();
					_s.get2c(&_ver, '\r'); // status message
					return (int)size;
				}
				if ((!_method.ieq("get") && !_method.ieq("head") && !_method.ieq("put") && !_method.ieq("post")))
					return e_method;
				_s.skip();
				if (!_s.get2sp(&_url))
					return e_url;
				_s.skip();
				if (!_s.get2sp(&_ver) || (!_ver.ieq("http/1.1") && !_ver.ieq("http/1.0"))) {
					if (_ver.empty() && (_url.ieq("http/1.1") || _url.ieq("http/1.0")))
						return e_url;
					return e_ver;
				}
				if (!_url._size || !_url._s)
					return e_url;
				return (int)size;
			}
		};
#ifdef _ZLIB_SELF_ALLOC
		inline void* zlib_alloc(void* opaque, uInt items, uInt size)
		{
			return ec_malloc(items * size);
		}
		inline void zlib_free(void* opaque, void* pf)
		{
			ec_free(pf);
		}
#endif
		/*!
		\brief parse http package
		*/
		class package
		{
		public:
			package()
			{
			}
			struct t_i {
				ctxt _key;
				ctxt _val;
			};
			req_line _req; // start line
			array<t_i, 128> _head;//head items
			ctxt _body; // body
		public:
			inline void clear()
			{
				_req.clear();
				_body.clear();
				_head.clear();
			}
			template<class _STR = std::string>
			void headinfo(_STR& vs) //output http heade to string
			{
				vs.reserve(1000);
				for (auto& i : _head) {
					vs.push_back('\t');
					vs.append(i._key._s, i._key._size);
					vs.append(": ");
					vs.append(i._val._s, i._val._size);
					vs.push_back('\n');
				}
			}
			int parse(const char* s, size_t size)// return <0 : error ; 0 : e_wait; >0 : package size
			{
				clear();
				if (!s || !size)
					return e_wait;
				ctxt _s(s, size);
				int ne;
				ctxt l;
				ne = _s.getline(&l); // start line
				if (ne <= 0)
					return ne;
				l.trim();
				ne = _req.parse(l._s, l._size);
				if (ne <= 0)
					return ne;
				t_i i;
				ne = _s.getline(&l);
				while (ne > 0) {
					if (l.is_multiline()) { // multiline continue
						if (!_head.size())
							return e_head;
						_head[_head.size() - 1]._val._size += l._size;
					}
					else if (l.is_endline()) {   // only "\r\n"
						if (_s._size >= _body._size) { // head end
							_body._s = _s._s;
							for (auto &v : _head) { // head item value trim space
								v._val.trim();
							}
							return (int)((_body._s - s) + _body._size);
						}
						return e_wait;
					}
					else {
						if (!l.headitem(&i._key, &i._val))
							return e_head;
						if (_head.size() == _head.capacity())
							return e_head; //add failed ,head item too much
						_head.push_back(i);
						if (i._key.ieq("Content-Length")) {
							int len = i._val.stoi();
							if (len < 0 || len > MAXSIZE_RCVHTTPBODY)
								return e_bodysize;
							_body._size = (size_t)len;
						}
					}
					ne = _s.getline(&l);
				}
				return ne;
			}
			ctxt* getattr(const ctxt &key)
			{
				size_t i;
				for (i = 0; i < _head.size(); i++) {
					if (_head[i]._key.ieq(key))
						return &_head[i]._val;
				}
				return nullptr;
			}

			static const char* serr(int nerr)
			{
				const char* s[] = { "wait", "failed", "line end space", "line size", "method", "url", "ver", "head", "body size" };
				int n = nerr * (-1);
				if (n < 0 || n >= (int)(sizeof(s) / sizeof(void*)))
					return "none";
				return s[n];
			}
			bool GetHeadFiled(const ctxt &key, char sval[], size_t size)
			{
				ctxt* pt = getattr(key);
				if (!pt || pt->_size >= size)
					return false;
				memcpy(sval, pt->_s, pt->_size);
				sval[pt->_size] = 0;
				return true;
			}
			template<class _Str>
			bool GetHeadFiled(const ctxt &key, _Str &sval)
			{
				ctxt* pt = getattr(key);
				if (!pt || !pt->_size)
					return false;
				sval.assign(pt->_s, pt->_size);
				return true;
			}
			bool CheckHeadFiled(const ctxt &key, const char* sval)
			{
				char sv[80];
				ctxt* pt = getattr(key);
				if (!pt)
					return false;
				size_t pos = 0;
				while (strnext(",;", pt->_s, pt->_size, pos, sv, sizeof(sv))) {
					if (!stricmp(sv, sval))
						return true;
				}
				return false;
			}
			inline bool HasKeepAlive()
			{
				return CheckHeadFiled("Connection", "keep-alive");
			}
			inline bool GetWebSocketKey(char *sout, size_t size)
			{
				return (CheckHeadFiled("Connection", "Upgrade") && CheckHeadFiled("Upgrade", "websocket")
					&& GetHeadFiled("Sec-WebSocket-Key", sout, size));
			}
			inline bool ismethod(const char* key)
			{
				return _req._method.ieq(key);
			}
			bool GetMethod(char *sout, size_t sizeout)
			{
				*sout = 0;
				return _req._method.get(sout, sizeout) > 0;
			}
			bool GetUrl(char *sout, size_t size)
			{
				size_t i = 0u;
				while (i + 1 < size && i < _req._url._size) {
					if (_req._url._s[i] == '?')
						break;
					sout[i] = _req._url._s[i];
					i++;
				}
				sout[i] = 0;
				return i > 0;
			}

			template<class _Str>
			bool GetUrl(_Str& sout)
			{
				size_t i = 0u;
				sout.clear();
				while (i < _req._url._size && _req._url._s[i] != '?')
					sout.push_back(_req._url._s[i++]);
				return i > 0;
			}

			template<class _Out>
			int encode_body(const void *pSrc, size_t size_src, _Out* pout, bool gzip = false)
			{
				z_stream stream;
				int err;
				char outbuf[32 * 1024];

				stream.next_in = (z_const Bytef *)pSrc;
				stream.avail_in = (uInt)size_src;

#ifdef _ZLIB_SELF_ALLOC
				stream.zalloc = ec::http::zlib_alloc;
				stream.zfree = ec::http::zlib_free;
#else
				stream.zalloc = (alloc_func)0;
				stream.zfree = (free_func)0;
#endif
				stream.opaque = (voidpf)0;

				err = deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, gzip ? 31 : MAX_WBITS, 8, Z_DEFAULT_STRATEGY); // windowBits <0 deflate;  8~15 zlib format; > 15 gzip format; abs() <=MAX_WBITS
				if (err != Z_OK)
					return err;
				uLong uout = 0;
				stream.avail_out = 0;
				while (!stream.avail_out) {
					stream.next_out = (unsigned char*)outbuf;
					stream.avail_out = (unsigned int)sizeof(outbuf);
					err = deflate(&stream, Z_SYNC_FLUSH);
					if (err != Z_OK)
						break;
					try {
						pout->append(outbuf, stream.total_out - uout);
					}
					catch (...) {
						err = Z_MEM_ERROR;
						break;
					}
					uout += stream.total_out - uout;
				}
				deflateEnd(&stream);
				return err;
			}

			template<class _Out>
			int decode_body(const void *pSrc, size_t size_src, _Out* pout, bool gzip = false)
			{ //return 0:OK; 
				z_stream stream;
				int err;
				char outbuf[32 * 1024];

				stream.next_in = (z_const Bytef *)pSrc;
				stream.avail_in = (uInt)size_src;

#ifdef _ZLIB_SELF_ALLOC
				stream.zalloc = ec::http::zlib_alloc;
				stream.zfree = ec::http::zlib_free;
#else
				stream.zalloc = (alloc_func)0;
				stream.zfree = (free_func)0;
#endif
				stream.opaque = (voidpf)0;

				err = inflateInit2(&stream, gzip ? 31 : MAX_WBITS);// windowBits <0 deflate;  8~15 zlib format; > 15 gzip format; abs() <=MAX_WBITS
				if (err != Z_OK)
					return err;
				uLong uout = 0;
				while (!err && stream.avail_in > 0) {
					stream.next_out = (unsigned char*)outbuf;
					stream.avail_out = (unsigned int)sizeof(outbuf);
					err = inflate(&stream, Z_SYNC_FLUSH);
					if (Z_OK != err && Z_STREAM_END != err)
						break;
					try {
						pout->append(outbuf, stream.total_out - uout);
					}
					catch (...) {
						err = Z_MEM_ERROR;
						break;
					}
					uout += stream.total_out - uout;
				}
				inflateEnd(&stream);
				return err == Z_STREAM_END ? 0 : err;
			}

			template<class _Out>
			bool make(_Out* pout, int statuscode, const char* statusmsg,
				const char* Content_type, const char* headers, const char* pbody, size_t bodysize, bool bzip = true)
			{
				pout->clear();
				str1k stmp;
				if (!stmp.format("HTTP/1.1 %d %s\r\n", statuscode, statusmsg))
					return false;
				pout->append(stmp.data(), stmp.size());
				if (HasKeepAlive())
					pout->append("Connection: keep-alive\r\n");

				if (headers && *headers)
					pout->append(headers);

				if (!pbody || !bodysize) {
					pout->append("\r\n");
					return true;
				}
				if (Content_type && *Content_type) {
					if (!stmp.format("Content-type: %s\r\n", Content_type))
						return false;
					pout->append(stmp.data(), stmp.size());
				}
				else
					pout->append("Content-type: application/octet-stream\r\n");

				int bdeflate = 0;
				if (bzip && bodysize > 512 && GetHeadFiled("Accept-Encoding", stmp)) {
					char sencode[16] = { 0 };
					size_t pos = 0;
					while (strnext(";,", stmp.data(), stmp.size(), pos, sencode, sizeof(sencode))) {
						if (!stricmp("gzip", sencode)) {
							pout->append("Content-Encoding: gzip\r\n");
							bdeflate = 2;
							break;
						}
						if (!stricmp("deflate", sencode) && !bdeflate) {
							pout->append("Content-Encoding: deflate\r\n");
							bdeflate = 1;
						}
					}
				}
				size_t poslen = pout->size(), sizehead;
				if (!stmp.format("Content-Length: %9d\r\n\r\n", (int)bodysize))
					return false;
				pout->append(stmp.data(), stmp.size());
				sizehead = pout->size();
				if (bdeflate) {
					if (Z_OK != encode_body(pbody, bodysize, pout, bdeflate == 2))
						return false;
					if (!stmp.format("Content-Length: %9d\r\n\r\n", (int)(pout->size() - sizehead)))
						return false;
					memcpy((char*)pout->data() + poslen, stmp.data(), stmp.size());
				}
				else
					pout->append(pbody, bodysize);
				return true;
			}

			int get_basic_auth(char *sname, size_t sizename, char* pswd, size_t sizepswd)
			{
				char  smode[32], skp[128], kv[128];
				smode[0] = 0;
				skp[0] = 0;
				kv[0] = 0;
				if (!GetHeadFiled("Authorization", kv, sizeof(kv)))
					return 401;
				int n = (int)strlen(kv);
				if (n >= (int)sizeof(skp))
					return 401;
				size_t pos = 0;
				if (!strnext('\x20', kv, n, pos, smode, sizeof(smode)) || !strnext('\n', kv, n, pos, skp, sizeof(skp)))
					return 401;
				if (!strieq("Basic", smode))
					return 401;
				n = decode_base64(kv, skp, (int)strlen(skp));
				if (n < 0)
					return 401;
				kv[n] = 0;
				pos = 0;
				return (strnext(':', kv, n, pos, sname, sizename) && strnext('\n', kv, n, pos, pswd, sizepswd)) ? 0 : 401;
			}
		};
	}// http
}//ec
