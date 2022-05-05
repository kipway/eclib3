/*!
\file ec_ipcwrk.h
\author	jiangyong
\email  kipway@outlook.com
\update 2022.5.2

net::ipcwrk
	a base class for AF_UNIX IPC worker

eclib 3.0 Copyright (c) 2017-2022, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

*/
#pragma once

#include <stdint.h>
#include "ec_netio.h"

#ifdef _WIN32
#	ifndef pollfd
#		define  pollfd WSAPOLLFD
#   endif

#else
#	define USE_AFUNIX 1
#	include <sys/eventfd.h>
#	include <sys/un.h>
#	include <poll.h>
#	include <fcntl.h>
#endif

#if defined(_MEM_TINY) // < 256M
#define SIZE_IPRCV_BUF (1024 * 256)
#define SIZE_IPSND_BUF (1024 * 512)
#elif defined(_MEM_SML) // < 1G
#define SIZE_IPRCV_BUF (1024 * 512)
#define SIZE_IPSND_BUF (1024 * 1024)
#else
#define SIZE_IPRCV_BUF (1024 * 1024)
#define SIZE_IPSND_BUF (1024 * 1024 * 2)
#endif
#include "ec_log.h"
namespace ec {
	class ipcwrk
	{
	public:
		ipcwrk() : _nst(st_unkown)
			, _sizercvbuf(SIZE_IPRCV_BUF)
			, _sizesndbuf(SIZE_IPSND_BUF)
			, _timeconnect(0)
			, _timeclose(0)
		{
			_pollfd.events = 0;
			_pollfd.revents = 0;
			_pollfd.fd = INVALID_SOCKET;
		}
		virtual ~ipcwrk() {
			if (INVALID_SOCKET != _pollfd.fd) {
				closesocket(_pollfd.fd);
				_pollfd.fd = INVALID_SOCKET;
			}
			_pollfd.events = 0;
			_pollfd.revents = 0;
			_nst = 0;
		}
		enum {
			st_unkown = 0,
			st_connected = 1,
			st_logined = 2
		};

	protected:
		int _nst;
		int _sizercvbuf;
		int _sizesndbuf;
		ec::net::url _url;

	private:
		pollfd _pollfd;
		time_t _timeconnect;
		time_t _timeclose;// for delay reconnect
	protected:
		virtual void ondisconnect() = 0;
		virtual void onconnect() = 0;
		virtual int  onrecv(const uint8_t *pdata, size_t size) = 0; // return 0:ok; -1:error need close
		virtual void onidle() = 0;
		virtual void onreadonce() {};

	private:
		bool connectasyn() {
			if (_pollfd.fd != INVALID_SOCKET)
				return true;
			if (::time(nullptr) - _timeclose < 2) // reconnect delay 2 seconds
				return false;
#ifdef USE_AFUNIX
			struct sockaddr_un srvaddr;
			memset(&srvaddr, 0, sizeof(srvaddr));
			srvaddr.sun_family = AF_UNIX;
			snprintf(srvaddr.sun_path, sizeof(srvaddr.sun_path), "/var/tmp/%s:%d", _url._protocol.c_str(), _url._port);
			SOCKET s = socket(AF_UNIX, SOCK_STREAM, 0);
#else
			struct sockaddr_in srvaddr;
			memset(&srvaddr, 0, sizeof(srvaddr));
			srvaddr.sin_family = AF_INET;
			srvaddr.sin_port = htons(_url._port);
			srvaddr.sin_addr.s_addr = inet_addr(_url._ip.c_str());
			SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#endif
			if (s == INVALID_SOCKET)
				return false;

			net::setrecvbuf(s, _sizercvbuf);
			net::setsendbuf(s, _sizesndbuf);

			long ul = 1;
#ifdef _WIN32
			if (SOCKET_ERROR == ioctlsocket(s, FIONBIO, (unsigned long*)&ul)) {
				closesocket(s);
				return false;
			}
#else
			if (ioctl(s, FIONBIO, &ul) == -1) {
				closesocket(s);
				return INVALID_SOCKET;
			}
#endif
			if (connect(s, (sockaddr *)(&srvaddr), sizeof(srvaddr)) < 0) {
#ifdef _WIN32
				if (WSAEWOULDBLOCK != WSAGetLastError()) {
					closesocket(s);
					return false;
				}
#else
				if (EINPROGRESS != errno) {
					closesocket(s);
					return false;
				}
#endif
			}
			_pollfd.fd = s;
			_pollfd.events = POLLOUT;
			_nst = st_unkown;
			_timeconnect = ::time(0);
			return true;
		}

		void closefd(bool bnotify = true)
		{
			if (_pollfd.fd != INVALID_SOCKET) {
#ifdef _WIN32
				shutdown(_pollfd.fd, SD_BOTH);
				::closesocket(_pollfd.fd);
#else
				shutdown(_pollfd.fd, SHUT_RDWR);
				::close(_pollfd.fd);
#endif
			}
			_pollfd.fd = INVALID_SOCKET;
			_pollfd.events = 0;
			_pollfd.revents = 0;
			_nst = st_unkown;
			if (bnotify)
				ondisconnect();
			_timeclose = ::time(nullptr);
		}
	public:
		bool init(const char* url, int sizercvbuf = 0, int sizesndbuf = 0)
		{
			if (!_url.parse(url, strlen(url)))
				return false;
			if(sizercvbuf)
				_sizercvbuf = sizercvbuf;
			if(sizesndbuf)
				_sizesndbuf = sizesndbuf;
			return _url._port != 0 && !_url._protocol.empty();
		}

		int send_non_block(const void* p, int nsize) // return -1:error; >=0 : send bytes
		{
			if (st_unkown == _nst)
				return -1;
			int nr = net::_send_non_block(_pollfd.fd, (const char*)p, (int)nsize);
			if (nr < 0)
				closefd();
			return nr;
		}

		void runtime(int waitmicroseconds) {
			onidle();
			if (INVALID_SOCKET == _pollfd.fd) {
				connectasyn();
				return;
			}

			if (st_logined != _nst) {
				time_t tcur = ::time(0);
				if (tcur - _timeconnect > 15) { // connect time out 15 seconds
					closefd();
					_timeconnect = tcur;
					return;
				}
			}
#ifdef _WIN32
			int n = WSAPoll(&_pollfd, 1, waitmicroseconds);
#else
			int n = poll(&_pollfd, 1, waitmicroseconds);
#endif
			if (n <= 0)
				return;
			if (_pollfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
				closefd();
				return;
			}

			else if (_pollfd.revents & POLLOUT) {
				if (!_nst) {
					_nst = st_connected;
#ifndef _WIN32
					int serr = 0;
					socklen_t serrlen = sizeof(serr);
					getsockopt(_pollfd.fd, SOL_SOCKET, SO_ERROR, (void *)&serr, &serrlen);
					if (serr) {
						closefd();
						return;
					}
#endif
					onconnect();
				}
			}
			else if (_pollfd.revents & POLLIN) {
				char rbuf[1024 * 32];
				while (1) {
#ifdef _WIN32
					int nr = ::recv(_pollfd.fd, rbuf, (int)sizeof(rbuf), 0);
#else
					int nr = ::recv(_pollfd.fd, rbuf, (int)sizeof(rbuf), MSG_DONTWAIT);
#endif
					if (nr == 0) {   //close gracefully
						closefd();
						return;
					}
					else if (nr < 0) {
#ifdef _WIN32
						int nerr = (int)WSAGetLastError();
						if (WSAEWOULDBLOCK == nerr)
							return;
#else
						int nerr = errno;
						if (nerr == EAGAIN || nerr == EWOULDBLOCK)
							return;
#endif
						closefd();
						return;
					}
					if (-1 == onrecv((const uint8_t*)rbuf, nr)) {
						closefd();
						return;
					}
					onreadonce();
				}
			}
			if (_nst)
				_pollfd.events = POLLIN;
			else
				_pollfd.events = POLLOUT;
		}
	};
}// namespace ec
