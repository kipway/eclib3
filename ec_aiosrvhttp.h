/*!
\file ec_aiosrvhttp.h

http/ws server 

\author  jiangyong

\update 
  2023-12-25 fix http Security vulnerability
  2023-5-30 support multi http root path
  2023-5-23 update http rang download big file
  2023-5-21 update http download big file

eclib 3.0 Copyright (c) 2017-2023, kipway
Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/

#pragma once

#include "ec_aiosrv.h"
#include "ec_diskio.h"
#include "ec_map.h"
#include "ec_string.h"
#include "ec_http.h"

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
		public:
			struct i_root
			{
				ec::string _name;//path name, "VIDEO"
				ec::string _path;// mappped path, The last character is '/'. "/data/vidoes/"
			};
			struct keq_rootnode {
				bool operator()(const char* key, const i_root& val)
				{
					return ec::streq(key, val._name.c_str());
				}
			};
		protected:
			int _fdlisten;
		protected:
			ec::mimecfg* _pmine;
			char _pathhttp[512];//utf8, http documents root path. The last character is '/'
			ec::hashmap<const char*, i_root, keq_rootnode> _roots;
		public:
			httpserver(ec::ilog* plog, ec::mimecfg* pmine) :
				ec::aio::netserver(plog)
				, _fdlisten(-1)
				, _pmine(pmine)
				, _roots(32)
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
			void addrootpath(const char* sname, const char* spath)
			{
				if (!sname || !*sname || !spath || !*spath)
					return;
				i_root it;
				it._name = sname;
				it._path = spath;
				ec::formatpath(it._path);
				_roots.set(sname, std::move(it));
			}
			template<class _STR = std::string>
			bool getRootPath(const char* src, _STR& sout)
			{
				char sname[80];
				ec::fixstring strname(sname, sizeof(sname));
				while (*src) {
					if (*src == '/') {
						i_root* pi = _roots.get(strname.c_str());
						if (!pi)
							return false;
						sout.clear();
						sout.append(pi->_path.c_str(), pi->_path.size());
						++src;
						if (*src)
							sout.append(src);
						return true;
					}
					else
						strname.push_back(*src);
					++src;
				}
				return false;
			}
		protected:
			/**
			 * @brief The application layer processes HTTP packets
			 * @param pkghttp HTTP message
			 * @return return false will process HTTP message in the default way
			*/
			virtual bool doAppHttp(http::package& pkghttp)
			{
				return false;
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
			/**
			 * @brief
			 * @param statuscode  like 200 ,206, 404
			 * @param statusinfo  "OK"
			 * @param sContentType like "text/html"
			 * @return
			*/
			bool httpwrite(int fd, ec::http::package* pPkg, int statuscode, const char* statusinfo,
				const char* body, size_t sizebody, const char* sContentType, bool bzip = true)
			{
				ec::bytes vs;
				vs.reserve(1024 + sizebody);
				if (!pPkg->make(&vs, statuscode, statusinfo, sContentType, "Accept-Ranges: bytes\r\n", body, sizebody, bzip))
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
					return httpwrite(fd, pPkg, 404, "not fund", html_404, strlen(html_404), "text/html");
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
					return httpwrite(fd, pPkg, 404, "not fund", html_404, strlen(html_404), "text/html");
				}
				ec::aio::session* ps = getsession(fd);
				if (!ps)
					return false;
				if (ps->hasSendJob())
					ps->setHttpDownFile(nullptr, 0, 0);
				const char* sext = ec::http::file_extname(sfile);
				ec::str256 content_type;
				bool bzip = true;
				if (sext && *sext) {
					_pmine->getmime(sext, content_type);
					bzip = !ec::http::iszipfile(sext);
				}
				return httpwrite(fd, pPkg, 200, "ok", data.data(), data.size(), content_type.c_str(), bzip);
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
					return httpwrite(fd, pPkg, 404, "not fund", html_404, strlen(html_404), "text/html");
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
					return httpwrite(fd, pPkg, 404, "not fund", html_404, strlen(html_404), "text/html");
				}
				if (_plog)
					_plog->add(CLOG_DEFAULT_DBG, "fd(%d) http down file '%s' Content-Length=%jd rang %jd-%jd/%jd", 
						fd, sfile, sizeContent, lpos, (lpos + sizeContent - 1), lfilesize);
				ec::aio::session* ps = getsession(fd);
				if (!ps)
					return false;
				if (lpos + lread < lposend + 1)
					ps->setHttpDownFile(sfile, lpos + lread, lposend + 1);
				else
					ps->setHttpDownFile(nullptr, 0, 0);
				return sendtofd(fd, answer.data(), answer.size()) >= 0;
			}
			bool dohttp(int fd, const uint8_t* pkg, size_t pkgsize)
			{
				ec::http::package http;
				if (http.parse(((const char*)pkg), pkgsize) <= 0)
					return false;
				if (doAppHttp(http))
					return true;
				loghttpstartline(CLOG_DEFAULT_DBG, fd, (const char*)pkg, pkgsize);
#ifdef _DEBUG
				loghttphead(CLOG_DEFAULT_ALL, "head", _plog, &http);
#endif
				if (!http.ismethod("GET") && !http.ismethod("HEAD")) { // only support GET
					return httpwrite(fd, &http, 400, "Bad Request", html_400, strlen(html_400), "text/html");
				}
				ec::str1k sfile, utf8, pathmaproot;
				if (!http.GetUrl(sfile))
					return false;
				url2utf8(sfile.c_str(), utf8);
				if (utf8.size() < 1)
					return false;
				if (strstr(utf8.c_str(),"..")) {
					return httpwrite(fd, &http, 404, "not fund", html_404, strlen(html_404), "text/html");
				}
				sfile = (const char*)_pathhttp;
				if (utf8[0] == '/') {
					if (getRootPath(utf8.c_str() + 1, pathmaproot))
						sfile = pathmaproot.c_str();
					else
						sfile.append(utf8.data() + 1, utf8.size() - 1);
				}
				else
					sfile.append(utf8.data(), utf8.size());
				if (sfile.back() == '/')
					sfile += "index.html";
				else if (ec::http::isdir(sfile.c_str()))
					sfile += "/index.html";
				long long flen = ec::io::filesize(sfile.c_str());
				if (flen < 0) {
					return httpwrite(fd, &http, 404, "not fund", html_404, strlen(html_404), "text/html");
				}
				if (http.ismethod("HEAD"))
					return DoHead(fd, sfile.c_str(), &http);

				utf8.clear();
				if (http.GetHeadFiled("Range", utf8)) { // "Range: bytes=0-1023" or "Range: bytes=0-"
					int64_t rangpos = 0, rangposend = 0;
					if (!parserange(utf8.data(), utf8.size(), rangpos, rangposend)) {
						return httpwrite(fd, &http, 413, "Request Entity Too Large", html_413, strlen(html_413), "text/html");
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