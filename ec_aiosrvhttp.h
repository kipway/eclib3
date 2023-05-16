/*!
\file ec_aiosrvhttp.h

http/ws server 

\author  jiangyong
*/

#pragma once

#include "ec_aiosrv.h"
#include "ec_diskio.h"

#ifndef MAXSIZE_AIOHTTP_DOWNFILE
#ifdef _ARM_LINUX
#define MAXSIZE_AIOHTTP_DOWNFILE (2 * 1024 * 1024)
#else
#define MAXSIZE_AIOHTTP_DOWNFILE (32 * 1024 * 1024)
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
				vs.reserve(1024 * 4);
				if (!pPkg->make(&vs, 401, "Unauthorized", stype, "WWW-Authenticate: Basic\r\n", html, htmlsize))
					return false;
				return this->sendtofd(ucid, vs.data(), vs.size()) >= 0;
			}
			bool httpwrite(int fd, ec::http::package* pPkg, const char* html, size_t size, const char* stype)
			{
				ec::bytes vs;
				vs.reserve(1024 * 32);
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
				vs.reserve(4096);
				for (auto& i : ph->_head) {
					vs += '\t';
					vs.append(i._key._s, i._key._size);
					vs += ':';
					vs.append(i._val._s, i._val._size);
					vs += '\n';
				}
				plog->add(loglevel, "%s\n%s", sinfo, vs.c_str());
			}
			bool parserange(const char* stxt, size_t txtlen, int64_t& lpos, int64_t& lsize)
			{
				char suints[8] = { 0 }, spos[16] = { 0 }, ssize[8] = { 0 };
				size_t pos = 0;
				if (!ec::strnext('=', stxt, txtlen, pos, suints, sizeof(suints)) || !ec::strieq("bytes", suints))
					return false;
				if (!ec::strnext('-', stxt, txtlen, pos, spos, sizeof(spos)))
					return false;
				lpos = atoll(spos);
				if (!ec::strnext(',', stxt, txtlen, pos, ssize, sizeof(ssize)))
					lsize = 0;
				else
					lsize = atoll(ssize);
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
				answer.reserve(1024 * 4);
				answer += "HTTP/1.1 200 ok\r\nServer: eclib web server\r\n";
				if (pPkg->HasKeepAlive())
					answer += "Connection: keep-alive\r\n";
				answer += "Accept-Ranges: bytes\r\n";
				if (!tmp.printf("Content-Length: %lld\r\n\r\n", flen))
					return false;
				answer.append(tmp.data(), tmp.size());
				if (this->_plog)
					this->_plog->add(CLOG_DEFAULT_DBG, "http head write fd(%u) size %zu :\n%s", fd, answer.size(), answer.c_str());
				return this->sendtofd(fd, answer.data(), answer.size()) >= 0;
			}
			bool downfile(int fd, ec::http::package* pPkg, const char* sfile)
			{
				ec::string data;
				data.reserve(1024 * 32);
				if (!ec::io::lckread(sfile, &data) || !data.size()) {
					httpreterr(fd, ec::http_sret404, 404);
					return pPkg->HasKeepAlive();
				}
				const char* sext = ec::http::file_extname(sfile);
				return httpwrite(fd, pPkg, data.data(), data.size(), sext);
			}
			bool DoGetRang(int fd, const char* sfile, ec::http::package* pPkg, int64_t lpos, int64_t lsize, int64_t lfilesize)
			{
				ec::str1k tmp;
				ec::string answer;
				answer.reserve(1024 * 32);
				answer += "HTTP/1.1 206 Partial Content\r\nServer: eclib web server\r\n";
				if (pPkg->HasKeepAlive())
					answer += "Connection: keep-alive\r\n";
				answer += "Accept-Ranges: bytes\r\n";

				const char* sext = ec::http::file_extname(sfile);
				if (sext && *sext && _pmine->getmime(sext, tmp)) {
					answer += "Content-type: ";
					answer += tmp.c_str();
					answer += "\r\n";
				}
				else
					answer += "Content-type: application/octet-stream\r\n";
				ec::string filetmp;
				filetmp.reserve(1024 * 32);
				if (!ec::io::lckread(sfile, &filetmp, lpos, lsize)) {
					httpreterr(fd, ec::http_sret404, 404);
					return pPkg->HasKeepAlive();
				}
				if (!tmp.printf("Content-Range: bytes %lld-%lld/%lld\r\n", (long long)lpos, (long long)(lpos + filetmp.size() - 1), (long long)lfilesize))
					return false;
				answer += tmp.c_str();
				if (!tmp.printf("Content-Length: %zu\r\n\r\n", filetmp.size()))
					return false;
				answer += tmp.c_str();

				answer.append(filetmp.data(), filetmp.size());
				if (this->_plog)
					this->_plog->add(CLOG_DEFAULT_DBG, "http write rang rang %lld-%lld/%lld", (long long)lpos, (long long)(lpos + filetmp.size() - 1), (long long)lfilesize);
				return this->sendtofd(fd, answer.data(), answer.size()) >= 0;
			}
			bool dohttp(int fd, const uint8_t* pkg, size_t pkgsize)
			{
				ec::http::package http;
				if (http.parse(((const char*)pkg), pkgsize) <= 0)
					return false;
				loghttpstartline(CLOG_DEFAULT_DBG, fd, (const char*)pkg, pkgsize);
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
				int64_t rangpos = 0, rangsize = 0;
				utf8.clear();
				if (http.GetHeadFiled("Range", utf8)) { // "Range: bytes=0-1023"
					if (!parserange(utf8.data(), utf8.size(), rangpos, rangsize)) {
						httpreterr(fd, ec::http_sret413, 413);
						return http.HasKeepAlive();
					}
				}
				if (rangsize > MAXSIZE_AIOHTTP_DOWNFILE)
					rangsize = MAXSIZE_AIOHTTP_DOWNFILE;
				if (!rangpos && !rangsize) { // get all
					if (flen > MAXSIZE_AIOHTTP_DOWNFILE) {
						httpreterr(fd, ec::http_sret413, 413);
						return http.HasKeepAlive();
					}
					return downfile(fd, &http, sfile.c_str());
				}
				return DoGetRang(fd, sfile.c_str(), &http, rangpos, rangsize, flen);
			}
		};
	}//namespace aio
}//namespace ec