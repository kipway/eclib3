/*!
\file ec_aiosrvhttp.h

http/ws server 

\author  jiangyong

\update 
  2023-5-23 update http rang download big file
  2023-5-21 update http download big file

eclib 3.0 Copyright (c) 2017-2023, kipway
Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/

#pragma once

#include "ec_aiosrv.h"
#include "ec_diskio.h"

#ifndef HTTP_RANGE_SIZE
#if defined(_MEM_TINY) // < 256M
#define HTTP_RANGE_SIZE (1024 * 30)
#elif defined(_MEM_SML) // < 1G
#define HTTP_RANGE_SIZE (1024 * 60)
#else
#define HTTP_RANGE_SIZE (1024 * 120)
#endif
#endif

namespace ec {
	namespace aio {
		class httpserver : public netserver
		{
		protected:
			int _fdlisten;
		protected:
			ec::mimecfg* _pmine;
			char _pathhttp[512];//utf8, http documents root path. The last character is '/'

		public:
			httpserver(ec::ilog* plog, ec::mimecfg* pmine) :
				ec::aio::netserver(plog)
				, _fdlisten(-1)
				, _pmine(pmine)
			{
				_pathhttp[0] = 0;
			}
			void init(const char* httproot)
			{
				ec::strlcpy(_pathhttp, httproot, sizeof(_pathhttp) - 1);
				char* ps = (char*)_pathhttp;
				while (*ps) {
					if (*ps == '\\')
						*ps = '/';
					++ps;
				}
				if (*(ps - 1) != '/') {
					*ps = '/';
					*(ps + 1) = 0;
				}
			}
		protected:
			bool httpreterr(int fd, const char* sret, int errcode)
			{
				int nret = this->sendtofd(fd, sret, strlen(sret));
				if (this->_plog) {
					if (nret >= 0)
						this->_plog->add(CLOG_DEFAULT_DBG, "http write fd(%u): error %d", fd, errcode);
					else
						this->_plog->add(CLOG_DEFAULT_DBG, "http write fd(%u) failed.", fd);
				}
				return nret > 0;
			}
			void loghttpstartline(int nlevel, int fd, const char* s, size_t size) //output http start line to log
			{
				if (!this->_plog || nlevel > this->_plog->getlevel())
					return;
				ec::http::ctxt src(s, size);
				ec::http::ctxt sl;
				int ne = src.getline(&sl); // start line
				if (ne > 2)
					this->_plog->add(nlevel, "fd(%u) %.*s", fd, ne - 2, sl._s);
			}
			bool httpwrite_401(uint32_t ucid, ec::http::package* pPkg, const char* html = nullptr, size_t htmlsize = 0, const char* stype = nullptr)
			{
				ec::bytes vs;
				vs.reserve(1024 * 2);
				if (!pPkg->make(&vs, 401, "Unauthorized", stype, "WWW-Authenticate: Basic\r\n", html, htmlsize))
					return false;
				return this->sendtofd(ucid, vs.data(), vs.size()) >= 0;
			}
			bool httpwrite(int fd, ec::http::package* pPkg, const char* html, size_t size, const char* stype)
			{
				ec::bytes vs;
				vs.reserve(1024 + size);
				ec::str256 content_type;
				bool bzip = true;
				if (stype && *stype) {
					_pmine->getmime(stype, content_type);
					bzip = !ec::http::iszipfile(stype);
				}
				if (!pPkg->make(&vs, 200, "ok", content_type.c_str(), "Accept-Ranges: bytes\r\n", html, size, bzip))
					return false;
				return this->sendtofd(fd, vs.data(), vs.size()) >= 0;
			}
			void loghttphead(int loglevel, const char* sinfo, ec::ilog* plog, ec::http::package* ph) //output http heade to log
			{
				if (plog->getlevel() < loglevel)
					return;
				ec::string vs;
				vs.reserve(2000);
				for (auto& i : ph->_head) {
					vs += '\t';
					vs.append(i._key._s, i._key._size);
					vs += ':';
					vs.append(i._val._s, i._val._size);
					vs += '\n';
				}
				plog->add(loglevel, "%s\n%s", sinfo, vs.c_str());
			}
			bool parserange(const char* stxt, size_t txtlen, int64_t& lpos, int64_t& lposend)
			{
				char suints[16] = { 0 }, spos[16] = { 0 }, send[16] = { 0 };
				size_t pos = 0;
				if (!strnext('=', stxt, txtlen, pos, suints, sizeof(suints)) || !strieq("bytes", suints))
					return false;
				if (!strnext('-', stxt, txtlen, pos, spos, sizeof(spos)))
					return false;
				lpos = atoll(spos);
				if (lpos < 0)
					lpos = 0;
				if (!strnext(',', stxt, txtlen, pos, send, sizeof(send)))
					lposend = -1;
				else
					lposend = atoll(send);
				return true;
			}
			bool DoHead(int fd, const char* sfile, ec::http::package* pPkg)
			{
				ec::str256 tmp;
				long long flen = ec::io::filesize(sfile);
				if (flen < 0) {
					httpreterr(fd, ec::http_sret404, 404);
					return pPkg->HasKeepAlive();
				}
				ec::string answer;
				answer.reserve(4000);
				answer += "HTTP/1.1 200 ok\r\nServer: eclib web server\r\n";
				if (pPkg->HasKeepAlive())
					answer += "Connection: keep-alive\r\n";
				answer += "Accept-Ranges: bytes\r\n";
				if (!tmp.format("Content-Length: %lld\r\n\r\n", flen))
					return false;
				answer.append(tmp.data(), tmp.size());
				if (this->_plog)
					this->_plog->add(CLOG_DEFAULT_DBG, "http head write fd(%u) size %zu :\n%s", fd, answer.size(), answer.c_str());
				return this->sendtofd(fd, answer.data(), answer.size()) >= 0;
			}
			bool downfile(int fd, ec::http::package* pPkg, const char* sfile)
			{
				ec::string data;
				if (!ec::io::lckread(sfile, &data) || !data.size()) {
					httpreterr(fd, ec::http_sret404, 404);
					return pPkg->HasKeepAlive();
				}
				ec::aio::session* ps = getsession(fd);
				if (!ps)
					return false;
				if (ps->hasSendJob())
					ps->setHttpDownFile(nullptr, 0, 0);
				const char* sext = ec::http::file_extname(sfile);
				return httpwrite(fd, pPkg, data.data(), data.size(), sext);
			}
			bool downbigfile(int fd, http::package* pPkg, const char* sfile, long long filelen)
			{
				if (filelen <= HTTP_RANGE_SIZE) {
					return downfile(fd, pPkg, sfile);
				}
				ec::string data, sContent;
				data.reserve(HTTP_RANGE_SIZE + 400);
				data = "HTTP/1.1 200 ok\r\nServer: eclib3 web server\r\n";
				data += "Connection: keep-alive\r\nAccept-Ranges: bytes\r\n";

				const char* sext = ec::http::file_extname(sfile);
				if (sext && *sext && _pmine->getmime(sext, sContent)) {
					data.append("Content-type: ").append(sContent).append("\r\n");
				}
				else
					data += "Content-type: application/octet-stream\r\n";
				data.append("Content-Length: ").append(ec::to_string(filelen)).append("\r\n\r\n");

				if (!ec::io::lckread(sfile, &data, 0, HTTP_RANGE_SIZE, filelen)) {
					httpreterr(fd, ec::http_sret404, 404);
					return true;
				}
				if (_plog)
					_plog->add(CLOG_DEFAULT_DBG, "fd(%u) http down file '%s', Content-Length=%lld",
						fd, sfile, filelen);
				ec::aio::session* ps = getsession(fd);
				if (!ps)
					return false;
				ps->setHttpDownFile(sfile, HTTP_RANGE_SIZE, filelen);
				return sendtofd(fd, data.data(), data.size()) >= 0;
			}
			bool DoGetRang(int fd, const char* sfile, ec::http::package* pPkg, int64_t lpos, int64_t lposend, int64_t lfilesize)
			{
				str1k tmp;
				ec::string answer;
				if (lpos >= lposend || lposend + 1 > lfilesize) {
					answer.reserve(512);
					answer += "HTTP/1.1 416 Range Not Satisfiable\r\nServer: eclib web server\r\n";
					if (pPkg->HasKeepAlive())
						answer += "Connection: keep-alive\r\n";
					tmp.format("Content-Range: bytes */%jd\r\n\r\n", lfilesize);
					answer.append(tmp);
					return sendtofd(fd, answer.data(), answer.size()) >= 0;
				}
				int64_t sizeContent = lposend - lpos + 1;
				answer.reserve(HTTP_RANGE_SIZE + 512);
				answer += "HTTP/1.1 206 Partial Content\r\nServer: eclib web server\r\n";
				if (pPkg->HasKeepAlive())
					answer += "Connection: keep-alive\r\n";
				answer += "Accept-Ranges: bytes\r\n";
				const char* sext = http::file_extname(sfile);
				if (sext && *sext && _pmine->getmime(sext, tmp)) {
					answer += "Content-type: ";
					answer += tmp;
					answer += "\r\n";
				}
				else
					answer += "Content-type: application/octet-stream\r\n";
				if (!tmp.format("Content-Range: bytes %jd-%jd/%jd\r\n", lpos, (lpos + sizeContent - 1), lfilesize))
					return false;
				answer.append(tmp);
				if (!tmp.format("Content-Length: %jd\r\n\r\n", sizeContent))
					return false;
				answer += tmp;
				int64_t lread = sizeContent > HTTP_RANGE_SIZE ? HTTP_RANGE_SIZE : sizeContent;
				if (!io::lckread(sfile, &answer, lpos, lread, lfilesize)) {
					httpreterr(fd, ec::http_sret404, 404);
					return pPkg->HasKeepAlive();
				}
				if (_plog)
					_plog->add(CLOG_DEFAULT_DBG, " fd(%d) http down file '%s' Content-Length=%jd rang %jd-%jd/%jd", 
						fd, sfile, sizeContent, lpos, (lpos + sizeContent - 1), lfilesize);
				ec::aio::session* ps = getsession(fd);
				if (!ps)
					return false;
				if (lpos + lread < lfilesize)
					ps->setHttpDownFile(sfile, lpos + lread, lfilesize);
				else
					ps->setHttpDownFile(nullptr, 0, 0);
				return sendtofd(fd, answer.data(), answer.size()) >= 0;
			}
			bool dohttp(int fd, const uint8_t* pkg, size_t pkgsize)
			{
				ec::http::package http;
				if (http.parse(((const char*)pkg), pkgsize) <= 0)
					return false;
				loghttpstartline(CLOG_DEFAULT_DBG, fd, (const char*)pkg, pkgsize);
				loghttphead(CLOG_DEFAULT_ALL, "head", _plog, &http);
				if (!http.ismethod("GET") && !http.ismethod("HEAD")) { // only support GET
					httpreterr(fd, ec::http_sret400, 400);
					return http.HasKeepAlive();
				}
				ec::str1k sfile, utf8;
				if (!http.GetUrl(sfile))
					return false;
				url2utf8(sfile.c_str(), utf8);
				if (utf8.size() < 1)
					return false;
				if (utf8[0] == '.' || (utf8.size() > 1 && utf8[1] == '.')) {
					httpreterr(fd, ec::http_sret404, 404);
					return http.HasKeepAlive();
				}
				sfile = (const char*)_pathhttp;
				if (utf8[0] == '/') {
					if (1 == utf8.size())
						sfile += "index.html";
					else
						sfile += utf8.c_str() + 1;
				}
				else
					sfile += utf8;
				if (ec::http::isdir(sfile.c_str())) {
					httpreterr(fd, ec::http_sret404, 404);
					return http.HasKeepAlive();
				}
				long long flen = ec::io::filesize(sfile.c_str());
				if (flen < 0) {
					httpreterr(fd, ec::http_sret404, 404);
					return http.HasKeepAlive();
				}
				if (http.ismethod("HEAD"))
					return DoHead(fd, sfile.c_str(), &http);

				utf8.clear();
				if (http.GetHeadFiled("Range", utf8)) { // "Range: bytes=0-1023" or "Range: bytes=0-"
					int64_t rangpos = 0, rangposend = 0;
					if (!parserange(utf8.data(), utf8.size(), rangpos, rangposend)) {
						httpreterr(fd, ec::http_sret413, 413);
						return http.HasKeepAlive();
					}
					if (rangposend <= 0)
						rangposend = flen - 1;
					return DoGetRang(fd, sfile.c_str(), &http, rangpos, rangposend, flen);
				}
				if (flen > HTTP_RANGE_SIZE * 16)
					return downbigfile(fd, &http, sfile.c_str(), flen);
				return downfile(fd, &http, sfile.c_str());
			}
		};
	}//namespace aio
}//namespace ec