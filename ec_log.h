/*!
\file ec_log.h
\author	jiangyong
\email  kipway@outlook.com
\update 2021.5.31

ilog
	A client log base class

udplog
	a client log class, send logs to UDP server

prtlog
	a client log class, print logs to terminal

eclib 3.0 Copyright (c) 2017-2020, kipway
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

#include "ec_netio.h"
#include "ec_time.h"
#include "ec_string.h"

#define EC_LOG_FRM_SIZE (1024 * 60) // MAX size of one log string
namespace ec 
{
	static const struct t_loglevel {
		int v;
		const char* s;
	} loglevitems[] = { {CLOG_DEFAULT_ERR, "err"}
	, {CLOG_DEFAULT_WRN, "wrn"}
	, {CLOG_DEFAULT_MSG, "msg"}
	, {CLOG_DEFAULT_MOR, "mor"}
	, {CLOG_DEFAULT_DBG, "dbg"}
	};

	class ilog // log base class
	{
	public:
		virtual ~ilog() {
		}
		virtual int open(const char* args) = 0; // return -1:error; 0:success
#ifdef _WIN32
		virtual int add(int level, const char * format, ...) = 0; //return >= 0 out string length; -1:error;
#else
		virtual int add(int level, const char * format, ...) __attribute__((format(printf, 3, 4))) = 0; //return >= 0 out string length; -1:error;
#endif
		virtual int getlevel() = 0;
		virtual void setlevel(int lev) = 0;
		virtual void runtime() = 0;
	private:
		struct t_logi {
			int v;
			const char* s;
		};
	public:
		static const char* level_str(int n)
		{
			for (size_t i = 0; i < sizeof(loglevitems) / sizeof(t_loglevel); i++) {
				if (loglevitems[i].v == n)
					return loglevitems[i].s;
			}
			return "ndf"; // not define
		}
		static int  level_val(const char* s)
		{
			for (size_t i = 0; i < sizeof(loglevitems) / sizeof(t_loglevel); i++) {
				if (ec::strieq(loglevitems[i].s, s))
					return loglevitems[i].v;
			}
			return CLOG_DEFAULT_MSG;
		}
	};

	class udplog :public ilog // log class write to UDP server
	{
	public:
		udplog(int nlev = CLOG_DEFAULT_MSG) : _level(nlev), _socket(INVALID_SOCKET)
		{
			memset(&_srvaddr, 0, sizeof(_srvaddr));
		}
		virtual ~udplog() {
			if (_socket != INVALID_SOCKET) {
				::closesocket(_socket);
				_socket = INVALID_SOCKET;
			}
		}
		int open(const char* str) // "udp://127.0.0.1:999/cabin"
		{
			if (!str || !*str)
				return -1;
			size_t pos = 0, zl = strlen(str);
			char sprotocol[8] = { 0 }, sip[40] = { 0 }, sport[8] = { 0 }, scabin[80] = { 0 };
			if (!ec::strnext(":/", str, zl, pos, sprotocol, sizeof(sprotocol))
				|| !ec::strnext(":/", str, zl, pos, sip, sizeof(sip))
				|| !ec::strnext("/", str, zl, pos, sport, sizeof(sport))
				|| !ec::strnext('\n', str, zl, pos, scabin, sizeof(scabin))
				)
				return -1;
			_cabin.assign(scabin);
			uint16_t uport = (uint16_t)atoi(sport);
			if (_socket != INVALID_SOCKET) {
				::closesocket(_socket);
				_socket = INVALID_SOCKET;
			}
			memset(&_srvaddr, 0, sizeof(_srvaddr));
			_srvaddr.sin_family = AF_INET;
			_srvaddr.sin_addr.s_addr = inet_addr(sip);
			_srvaddr.sin_port = htons(uport);
			_socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

			if (INVALID_SOCKET == _socket)
				return -1;
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
		virtual int add(int level, const char * format, ...)
#else
		virtual int add(int level, const char * format, ...) __attribute__((format(printf, 3, 4)))
#endif
		{
			if (INVALID_SOCKET == _socket)
				return -1;
			if (_level < level)
				return 0;
			char buf[EC_LOG_FRM_SIZE + 80];
			int nh = snprintf(buf, sizeof(buf), "{\"order\":\"wlog\",\"level\":%d,\"cabin\":\"%s\"}\n", level, _cabin.c_str());
			if (nh < 0)
				return -1;
			va_list arg_ptr;
			va_start(arg_ptr, format);
			int nb = vsnprintf(&buf[nh], EC_LOG_FRM_SIZE, format, arg_ptr);
			va_end(arg_ptr);
			if (nb <= 0 || nb >= EC_LOG_FRM_SIZE)
				return -1;
			return ::sendto(_socket, buf, nh + nb, 0, (struct sockaddr *)&_srvaddr, sizeof(_srvaddr));
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
	protected:
		int _level;
		SOCKET	_socket;
		struct	sockaddr_in _srvaddr;
		str128 _cabin;
	};

	class prtlog : public ec::ilog // log print to current terminal
	{
	public:
		prtlog(int nlev = CLOG_DEFAULT_DBG) :_level(nlev)
		{
		}
		virtual int open(const char* args)
		{
			return 0;
		}
#ifdef _WIN32
		virtual int add(int level, const char * format, ...)
#else
		virtual int add(int level, const char * format, ...) __attribute__((format(printf, 3, 4)))
#endif
		{
			if (_level < level)
				return 0;
			int ns = 0;
			ec::cTime ctm(ec::nstime(&ns));

			printf("[%d/%d/%d %02d:%02d:%02d.%06d] [%s] "
				, ctm._year, ctm._mon, ctm._day, ctm._hour, ctm._min, ctm._sec, ns
				, ec::ilog::level_str(level));

			va_list arg_ptr;
			va_start(arg_ptr, format);
			int n = vprintf(format, arg_ptr);
			va_end(arg_ptr);

			printf("\n");
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
	protected:
		int _level;
	};
} // namespace ec