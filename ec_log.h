/*!
\file ec_log.h
\author	jiangyong
\email  kipway@outlook.com
\update 2023.2.5
2023.2.5 add ipv6 support

ilog
	A client log base class

udplog
	a client log class, send logs to UDP server

prtlog
	a client log class, print logs to terminal

eclib 3.0 Copyright (c) 2017-2023, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/
#pragma once
#include <stdarg.h>

#define CLOG_DEFAULT_ERR  10
#define CLOG_DEFAULT_WRN  20
#define CLOG_DEFAULT_MSG  30
#define CLOG_DEFAULT_MOR  31
#define CLOG_DEFAULT_DBG  40
#define CLOG_DEFAULT_ALL  999

#define CLOG_DEFAULT_INF CLOG_DEFAULT_MSG
#include "ec_netio.h"
#include "ec_time.h"
#include "ec_string.h"

#define EC_LOG_FRM_SIZE (1024 * 30) // MAX size of one log string
namespace ec
{
	static const struct t_loglevel {
		int v;
		const char* s;
	} loglevitems[] = { {CLOG_DEFAULT_ERR, "err"}
	, {CLOG_DEFAULT_WRN, "wrn"}
	, {CLOG_DEFAULT_INF, "inf"}
	, {CLOG_DEFAULT_MSG, "msg"}
	, {CLOG_DEFAULT_MOR, "mor"}
	, {CLOG_DEFAULT_DBG, "dbg"}
	, {CLOG_DEFAULT_ALL, "all"}
	};

	class ilog // log base class
	{
	public:
		virtual ~ilog() {
		}
		virtual int open(const char* args) = 0; // return -1:error; 0:success
#ifdef _WIN32
		virtual int add(int level, const char* format, ...) = 0; //return >= 0 out string length; -1:error;
#else
		virtual int add(int level, const char* format, ...) __attribute__((format(printf, 3, 4))) = 0; //return >= 0 out string length; -1:error;
#endif
#ifdef _WIN32
		virtual int append(int level, const char* format, ...) = 0; //return >= 0 out string length; -1:error;
#else
		virtual int append(int level, const char* format, ...) __attribute__((format(printf, 3, 4))) = 0; //return >= 0 out string length; -1:error;
#endif
		virtual int push(int level, const char* subwho, const char* format, va_list args) {
			return 0;
		};
		virtual int getlevel() = 0;
		virtual void setlevel(int lev) = 0;
		virtual void runtime() = 0;
		virtual void release() = 0;
	private:
		struct t_logi {
			int v;
			const char* s;
		};
	public:
		static const char* level_str(int n, char* sout = nullptr, size_t soutsize = 0)
		{
			for (size_t i = 0; i < sizeof(loglevitems) / sizeof(t_loglevel); i++) {
				if (loglevitems[i].v == n)
					return loglevitems[i].s;
			}
			if (!sout || !soutsize)
				return "ndf"; // not define
			size_t zn = snprintf(sout, soutsize, "%d", n);
			if(zn >= soutsize)
				return "ndf"; // not define
			return sout;
		}
		static int  level_val(const char* s)
		{
			if (!s || !*s)
				return CLOG_DEFAULT_MSG;
			for (size_t i = 0; i < sizeof(loglevitems) / sizeof(t_loglevel); i++) {
				if (ec::strieq(loglevitems[i].s, s))
					return loglevitems[i].v;
			}
			int n = atoi(s);
			return n ? n : CLOG_DEFAULT_MSG;
		}
		static ilog* create(const char* surl);
	};

	class udplog :public ilog // log class write to UDP server
	{
	public:
		udplog(int nlev = CLOG_DEFAULT_MSG) : _level(nlev), _socket(INVALID_SOCKET)
		{

		}
		virtual ~udplog() {
			if (_socket != INVALID_SOCKET) {
				::closesocket(_socket);
				_socket = INVALID_SOCKET;
			}
		}
		int open(const char* str) // "udp://127.0.0.1:999/cabin?level=dbg"
		{
			if (!str || !*str)
				return -1;
			ec::string strurl;
			strurl.reserve(strlen(str) + 1);
			while (*str) { // remove space and table char
				if (*str != '\x20' && *str != '\t')
					strurl.push_back(*str);
				str++;
			}
			ec::net::url<ec::string> purl;
			if (!purl.parse(strurl.c_str(), strurl.size()))
				return -1;
			if (purl._path.empty())
				return -1;
			if (!purl._port)
				purl._port = 999; //default
			_cabin.assign(purl._path.c_str());

			if (!purl._args.empty()) { // parse url args
				size_t pos = 0;
				char skey[16] = { 0 }, sval[16] = { 0 };
				if (ec::strnext('=', purl._args.c_str(), purl._args.size(), pos, skey, sizeof(skey))
					&& ec::strnext('&', purl._args.c_str(), purl._args.size(), pos, sval, sizeof(sval))
					) {
					if (ec::strieq("level", skey))
						_level = ec::ilog::level_val(sval);
				}
			}
			if (_socket != INVALID_SOCKET) {
				::closesocket(_socket);
				_socket = INVALID_SOCKET;
			}
			_srvaddr.set(purl._port, purl.ipstr());
			_socket = ::socket(_srvaddr.sa_family(), SOCK_DGRAM, IPPROTO_UDP);

			if (INVALID_SOCKET == _socket)
				return -1;
			net::setfd_cloexec(_socket);
			int nval = 1024 * 1024;
			setsockopt(_socket, SOL_SOCKET, SO_SNDBUF, (char*)&nval, (socklen_t)sizeof(nval));
			return 0;
		}
		void close()
		{
			if (_socket != INVALID_SOCKET) {
				::closesocket(_socket);
				_socket = INVALID_SOCKET;
			}
		}
	public:
#ifdef _WIN32
		virtual int add(int level, const char* format, ...)
#else
		virtual int add(int level, const char* format, ...) __attribute__((format(printf, 3, 4)))
#endif
		{
			if (INVALID_SOCKET == _socket)
				return -1;
			if (_level < level)
				return 0;
			char buf[EC_LOG_FRM_SIZE];
			size_t zh = snprintf(buf, sizeof(buf), "{\"order\":\"wlog\",\"level\":%d,\"cabin\":\"%s\"}\n", level, _cabin.c_str());
			if (zh >= sizeof(buf))
				return -1;
			va_list arg_ptr;
			va_start(arg_ptr, format);
			size_t zb = vsnprintf(&buf[zh], sizeof(buf) - zh, format, arg_ptr);
			va_end(arg_ptr);
			if (zh + zb >= sizeof(buf))
				return -1;
			int addrlen = 0;
			struct sockaddr* paddr = _srvaddr.getsockaddr(&addrlen);
#ifdef _WIN32
			return ::sendto(_socket, buf, (int)(zh + zb), 0, paddr, addrlen);
#else
			return ::sendto(_socket, buf, (int)(zh + zb), MSG_DONTWAIT, paddr, addrlen);
#endif
		}

		virtual int push(int level, const char* subwho, const char* format, va_list args) {

			if (INVALID_SOCKET == _socket)
				return -1;
			if (_level < level)
				return 0;
			char buf[EC_LOG_FRM_SIZE];
			size_t zh = (subwho && *subwho) ?
				snprintf(buf, sizeof(buf), "{\"order\":\"wlog\",\"level\":%d,\"cabin\":\"%s\"}\n[%s]", level, _cabin.c_str(), subwho) :
				snprintf(buf, sizeof(buf), "{\"order\":\"wlog\",\"level\":%d,\"cabin\":\"%s\"}\n", level, _cabin.c_str());
			if (zh >= sizeof(buf))
				return -1;
			size_t zb = vsnprintf(&buf[zh], sizeof(buf) - zh, format, args);
			if (zh + zb >= sizeof(buf))
				return -1;
			int addrlen = 0;
			struct sockaddr* paddr = _srvaddr.getsockaddr(&addrlen);
#ifdef _WIN32
			return ::sendto(_socket, buf, (int)(zh + zb), 0, paddr, addrlen);
#else
			return ::sendto(_socket, buf, (int)(zh + zb), MSG_DONTWAIT, paddr, addrlen);
#endif
		}

#ifdef _WIN32
		virtual int append(int level, const char* format, ...)
#else
		virtual int append(int level, const char* format, ...) __attribute__((format(printf, 3, 4)))
#endif
		{
			if (INVALID_SOCKET == _socket)
				return -1;
			if (_level < level)
				return 0;
			char buf[EC_LOG_FRM_SIZE];
			size_t zh = snprintf(buf, sizeof(buf), "{\"order\":\"append\",\"level\":%d,\"cabin\":\"%s\"}\n", level, _cabin.c_str());
			if (zh >= sizeof(buf))
				return -1;
			va_list arg_ptr;
			va_start(arg_ptr, format);
			size_t zb = vsnprintf(&buf[zh], sizeof(buf) - zh, format, arg_ptr);
			va_end(arg_ptr);
			if (zh + zb >= sizeof(buf))
				return -1;
			int addrlen = 0;
			struct sockaddr* paddr = _srvaddr.getsockaddr(&addrlen);
#ifdef _WIN32
			return ::sendto(_socket, buf, (int)(zh + zb), 0, paddr, addrlen);
#else
			return ::sendto(_socket, buf, (int)(zh + zb), MSG_DONTWAIT, paddr, addrlen);
#endif
		}
		virtual int getlevel()
		{
			return _level;
		};
		virtual void setlevel(int lev)
		{
			_level = lev;
		}
		virtual void runtime()
		{
		}
		virtual void release()
		{
			delete this;
		}
	protected:
		int _level;
		SOCKET	_socket;
		net::socketaddr _srvaddr;
		str128 _cabin;
	};

	class prtlog : public ec::ilog // log print to current terminal
	{
	public:
		enum out_type{
			out_null = 0, // out to null
			out_std = 1 // out to stdout
		};
		prtlog(int nlev = CLOG_DEFAULT_DBG) :_level(nlev), _outtype(out_null)
		{
		}
		//args: nullptr or "null" prt to null; "stdout" prt to stdout
		virtual int open(const char* args)
		{
			if (args && *args) {
				if (ec::strieq("stdout", args))
					_outtype = out_std;
				else
					_outtype = out_null;
			}
			else
				_outtype = out_std;
			return 0;
		}
#ifdef _WIN32
		virtual int add(int level, const char* format, ...)
#else
		virtual int add(int level, const char* format, ...) __attribute__((format(printf, 3, 4)))
#endif
		{
			if (_outtype == out_null || _level < level)
				return 0;
			int ns = 0;
			ec::cTime ctm(ec::nstime(&ns));
			char slev[16] = { 0 };
			printf("[%d/%d/%d %02d:%02d:%02d.%06d] [%s] "
				, ctm._year, ctm._mon, ctm._day, ctm._hour, ctm._min, ctm._sec, ns
				, ec::ilog::level_str(level, slev, sizeof(slev)));

			va_list arg_ptr;
			va_start(arg_ptr, format);
			int n = vprintf(format, arg_ptr);
			va_end(arg_ptr);

			printf("\n");
			return n;
		}
		virtual int push(int level, const char* subwho, const char* format, va_list args) {
			if (_outtype == out_null || _level < level)
				return 0;
			int ns = 0;
			ec::cTime ctm(ec::nstime(&ns));
			char slev[16] = { 0 };
			(subwho && *subwho) ?
				printf("[%d/%d/%d %02d:%02d:%02d.%06d] [%s][%s] "
					, ctm._year, ctm._mon, ctm._day, ctm._hour, ctm._min, ctm._sec, ns
					, ec::ilog::level_str(level, slev, sizeof(slev)), subwho) :
				printf("[%d/%d/%d %02d:%02d:%02d.%06d] [%s] "
					, ctm._year, ctm._mon, ctm._day, ctm._hour, ctm._min, ctm._sec, ns
					, ec::ilog::level_str(level, slev, sizeof(slev)));

			int n = vprintf(format, args);
			printf("\n");
			fflush(stdout);
			return n;
		}
#ifdef _WIN32
		virtual int append(int level, const char* format, ...)
#else
		virtual int append(int level, const char* format, ...) __attribute__((format(printf, 3, 4)))
#endif
		{
			if (_outtype == out_null || _level < level)
				return 0;
			va_list arg_ptr;
			va_start(arg_ptr, format);
			int n = vprintf(format, arg_ptr);
			va_end(arg_ptr);
			return n;
		}
		virtual int getlevel()
		{
			return _level;
		};
		virtual void setlevel(int lev)
		{
			_level = lev;
		}
		virtual void runtime()
		{
		}
		virtual void release()
		{
			delete this;
		}
	protected:
		int _level;
		out_type _outtype;
	};

	inline ilog* ilog::create(const char* surl)
	{
		size_t pos = 0, zl = (surl && *surl) ? strlen(surl) : 0;
		char sprotocol[16] = { 0 };
		if (!ec::strnext(":/", surl, zl, pos, sprotocol, sizeof(sprotocol)))
			sprotocol[0] = 0;
		if (ec::strieq("udp", sprotocol)) {
			udplog* plog = new udplog();
			plog->open(surl);
			return plog;
		}
		prtlog* plog = new prtlog();
		plog->open(surl);
		return plog;
	}
} // namespace ec