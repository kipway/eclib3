/*!
\file ec_httpsrv.h

\author	jiangyong
\email  kipway@outlook.com
\update 2020.12.26

httpsrv
	class for http/https server

eclib 3.0 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/
#pragma once

#include <stdint.h>
#include "ec_diskio.h"
#include "ec_string.h"
#include "ec_log.h"

#include "ec_netsrv.h"
#include "ec_http.h"

#ifndef MAXSIZE_HTTP_DOWNFILE
#ifdef _ARM_LINUX
#define MAXSIZE_HTTP_DOWNFILE (2 * 1024 * 1024)
#else
#define MAXSIZE_HTTP_DOWNFILE (32 * 1024 * 1024)
#endif
#endif
namespace ec
{
	namespace net
	{
		class httpsrv : public server  // http/https server
		{
		public:
			httpsrv(ilog* plog, memory* pmem, mimecfg* pmine) : server(pmem, plog),
				_pmine(pmine), _pathhttp{ 0 }
			{
			}
			void initpath(const char* pathttp)
			{
				strlcpy(_pathhttp, pathttp, sizeof(_pathhttp));
			}

		protected:
			ec::mimecfg* _pmine;
			char _pathhttp[512];//utf8, http documents root path. The last character is '/'
		protected:
			virtual const char* srvname(uint32_t protoc)
			{
				if (EC_NET_SS_WS == protoc)
					return "WS";
				else if (EC_NET_SS_WSS == protoc)
					return "WSS";
				else if (EC_NET_SS_HTTP == protoc)
					return "HTTP";
				else if (EC_NET_SS_HTTPS == protoc)
					return "HTTPS";
				return "TCP";
			}

			bool httpreterr(unsigned int ucid, const char* sret, int errcode)
			{
				int nret = sendbyucid(ucid, sret, strlen(sret));
				if (_plog) {
					if (nret >= 0)
						_plog->add(CLOG_DEFAULT_DBG, "http write ucid(%u): error %d", ucid, errcode);
					else
						_plog->add(CLOG_DEFAULT_DBG, "http write ucid(%u) failed.", ucid);
				}
				return nret > 0;
			}

			bool httpwrite_401(uint32_t ucid, http::package* pPkg, const char* html = nullptr, size_t htmlsize = 0, const char* stype = nullptr)
			{
				try {
					bytes vs(_pmem);
					vs.reserve(1024 * 4);
					if (!pPkg->make(&vs, 401, "Unauthorized", stype, "WWW-Authenticate: Basic\r\n", html, htmlsize))
						return false;
					return sendbyucid(ucid, vs.data(), vs.size()) >= 0;
				}
				catch (...) {
					if (_plog)
						_plog->add(CLOG_DEFAULT_ERR, "ucid(%u) httpwrite_401 exception", ucid);
					return false;
				}
			}

			bool httpwrite(uint32_t ucid, http::package* pPkg, const char* html, size_t size, const char* stype)
			{
				try {
					bytes vs(_pmem);
					vs.reserve(1024 * 32);
					str128 content_type;
					bool bzip = true;
					if (stype && *stype) {
						_pmine->getmime(stype, content_type);
						bzip = !http::iszipfile(stype);
					}
					if (!pPkg->make(&vs, 200, "ok", content_type.c_str(), "Accept-Ranges: bytes\r\n", html, size, bzip))
						return false;
					return sendbyucid(ucid, vs.data(), vs.size()) >= 0;
				}
				catch (...) {
					if (_plog)
						_plog->add(CLOG_DEFAULT_ERR, "ucid(%u) httpwrite bytes %zu exception", ucid, size);
					return false;
				}
			}

			void loghttphead(int loglevel, const char* sinfo, ilog* plog, http::package* ph) //output http heade to log
			{
				if (plog->getlevel() < loglevel)
					return;
				try {
					vector<char> vs(_pmem);
					vs.reserve(4096);
					for (auto &i : ph->_head) {
						vs += '\t';
						vs.append(i._key._s, i._key._size);
						vs += ':';
						vs.append(i._val._s, i._val._size);
						vs += '\n';
					}
					plog->add(loglevel, "%s\n%s", sinfo, vs.c_str());
				}
				catch (...) {
				}
			}

			bool parserange(const char* stxt, size_t txtlen, int64_t &lpos, int64_t &lsize)
			{
				char suints[8] = { 0 }, spos[16] = { 0 }, ssize[8] = { 0 };
				size_t pos = 0;
				if (!strnext('=', stxt, txtlen, pos, suints, sizeof(suints)) || !strieq("bytes", suints))
					return false;
				if (!strnext('-', stxt, txtlen, pos, spos, sizeof(spos)))
					return false;
				lpos = atoll(spos);
				if (!strnext(',', stxt, txtlen, pos, ssize, sizeof(ssize)))
					lsize = 0;
				else
					lsize = atoll(ssize);
				return true;
			}

			bool DoHead(uint32_t ucid, const char* sfile, http::package* pPkg)
			{
				try {
					str1k tmp;
					long long flen = ec::io::filesize(sfile);
					if (flen < 0) {
						httpreterr(ucid, http_sret404, 404);
						return pPkg->HasKeepAlive();
					}
					vector<char> answer(_pmem);
					answer.reserve(1024 * 4);

					answer += "HTTP/1.1 200 ok\r\nServer: eclib web server\r\n";
					if (pPkg->HasKeepAlive())
						answer += "Connection: keep-alive\r\n";

					answer += "Accept-Ranges: bytes\r\n";

					if (!tmp.printf("Content-Length: %lld\r\n\r\n", flen))
						return false;
					answer.append(tmp.data(), tmp.size());
					if (_plog)
						_plog->add(CLOG_DEFAULT_DBG, "http head write ucid(%u) size %zu :\n%s", ucid, answer.size(), answer.c_str());
					return sendbyucid(ucid, answer.data(), answer.size()) >= 0;
				}
				catch (...) {
					if (_plog)
						_plog->add(CLOG_DEFAULT_ERR, "ucid(%u) head file %s exception", ucid, sfile);
					return false;
				}
			}

			bool downfile(uint32_t ucid, http::package* pPkg, const char* sfile)
			{
				try {
					vector<char> data(_pmem);
					data.reserve(1024 * 32);
					if (!ec::io::lckread(sfile, &data) || !data.size()) {
						httpreterr(ucid, ec::http_sret404, 404);
						return pPkg->HasKeepAlive();
					}
					const char* sext = ec::http::file_extname(sfile);
					return httpwrite(ucid, pPkg, data.data(), data.size(), sext);
				}
				catch (...) {
					if (_plog)
						_plog->add(CLOG_DEFAULT_ERR, "ucid(%u) download file %s exception", ucid, sfile);
					return false;
				}
			}

			bool DoGetRang(uint32_t ucid, const char* sfile, ec::http::package* pPkg, int64_t lpos, int64_t lsize, int64_t lfilesize)
			{
				try {
					str1k tmp;
					vector<char> answer(_pmem);
					answer.reserve(1024 * 32);

					answer += "HTTP/1.1 206 Partial Content\r\nServer: eclib web server\r\n";
					if (pPkg->HasKeepAlive())
						answer += "Connection: keep-alive\r\n";
					answer += "Accept-Ranges: bytes\r\n";

					const char* sext = http::file_extname(sfile);
					if (sext && *sext && _pmine->getmime(sext, tmp)) {
						answer += "Content-type: ";
						answer += tmp.c_str();
						answer += "\r\n";
					}
					else
						answer += "Content-type: application/octet-stream\r\n";

					vector<char> filetmp(_pmem);
					filetmp.reserve(1024 * 32);
					if (!io::lckread(sfile, &filetmp, lpos, lsize)) {
						httpreterr(ucid, ec::http_sret404, 404);
						return pPkg->HasKeepAlive();
					}

					if (!tmp.printf("Content-Range: bytes %lld-%lld/%lld\r\n", (long long)lpos, (long long)(lpos + filetmp.size() - 1), (long long)lfilesize))
						return false;
					answer += tmp.c_str();
					if (!tmp.printf("Content-Length: %zu\r\n\r\n", filetmp.size()))
						return false;
					answer += tmp.c_str();

					answer.append(filetmp.data(), filetmp.size());
					if (_plog)
						_plog->add(CLOG_DEFAULT_DBG, "http write rang rang %lld-%lld/%lld", (long long)lpos, (long long)(lpos + filetmp.size() - 1), (long long)lfilesize);
					return sendbyucid(ucid, answer.data(), answer.size()) >= 0;
				}
				catch (...) {
					if (_plog)
						_plog->add(CLOG_DEFAULT_ERR, "ucid(%u) DoGetRang file %s pos= %lld,size=%lld exception", ucid, sfile, (long long)lpos, (long long)lsize);
					return false;
				}
			}

			void loghttpstartline(int nlevel, uint32_t ucid, const char* s, size_t size) //output http start line to log
			{
				if (!_plog || nlevel > _plog->getlevel())
					return;
				http::ctxt src(s, size);
				http::ctxt sl;
				int ne = src.getline(&sl); // start line
				if (ne > 2)
					_plog->add(nlevel, "ucid(%u) %.*s", ucid, ne - 2, sl._s);
			}

			bool dohttp(uint32_t ucid, uint32_t protoc, const uint8_t* pkg, size_t pkgsize)
			{
				http::package http;
				if (http.parse(((const char*)pkg), pkgsize) <= 0)
					return false;

				loghttpstartline(CLOG_DEFAULT_DBG, ucid, (const char*)pkg, pkgsize);

				if (!http.ismethod("GET") && !http.ismethod("HEAD")) { // only support GET
					httpreterr(ucid, ec::http_sret400, 400);
					return http.HasKeepAlive();
				}

				ec::str1k sfile, utf8;

				if (!http.GetUrl(sfile))
					return false;
				url2utf8(sfile.c_str(), utf8);
				if (utf8.size() < 1)
					return false;
				if (utf8[0] == '.' || (utf8.size() > 1 && utf8[1] == '.')) {
					httpreterr(ucid, ec::http_sret404, 404);
					return http.HasKeepAlive();
				}
				sfile = _pathhttp;
				if (utf8[0] == '/') {
					if (1 == utf8.size())
						sfile += "index.html";
					else
						sfile += utf8.c_str() + 1;
				}
				else
					sfile += utf8;

				if (ec::http::isdir(sfile.c_str())) {
					httpreterr(ucid, ec::http_sret404, 404);
					return http.HasKeepAlive();
				}

				long long flen = ec::io::filesize(sfile.c_str());
				if (flen < 0) {
					httpreterr(ucid, ec::http_sret404, 404);
					return http.HasKeepAlive();
				}

				if (http.ismethod("HEAD"))
					return DoHead(ucid, sfile.c_str(), &http);

				int64_t rangpos = 0, rangsize = 0;
				utf8.clear();
				if (http.GetHeadFiled("Range", utf8)) { // "Range: bytes=0-1023"
					if (!parserange(utf8.data(), utf8.size(), rangpos, rangsize)) {
						httpreterr(ucid, ec::http_sret413, 413);
						return http.HasKeepAlive();
					}
				}
				if (rangsize > MAXSIZE_HTTP_DOWNFILE)
					rangsize = MAXSIZE_HTTP_DOWNFILE;
				if (!rangpos && !rangsize) { // get all
					if (flen > MAXSIZE_HTTP_DOWNFILE) {
						httpreterr(ucid, ec::http_sret413, 413);
						return http.HasKeepAlive();
					}
					return downfile(ucid, &http, sfile.c_str());
				}
				return DoGetRang(ucid, sfile.c_str(), &http, rangpos, rangsize, flen);
			}
		};
	} // namespae net
} // namespace ec
