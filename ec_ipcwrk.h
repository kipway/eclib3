/*!
\file ec_ipcwrk.h
\author	jiangyong
\email  kipway@outlook.com
\update
  2023.3.2 add self manage send buffer
  2023.3.1 update recv while

net::ipcwrk
	a base class for AF_UNIX IPC worker

eclib 3.0 Copyright (c) 2017-2023, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

*/
#pragma once

#include <stdint.h>
#include "ec_memory.h"
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

#ifndef EC_IPC_SNDBUF_MAXSIZE  // self manage send buffer default max size
#if defined(_MEM_TINY) // < 256M
#define EC_IPC_SNDBUF_MAXSIZE (1024 * 1024 * 2)
#elif defined(_MEM_SML) // < 1G
#define EC_IPC_SNDBUF_MAXSIZE (1024 * 1024 * 8) 
#else
#define EC_IPC_SNDBUF_MAXSIZE (1024 * 1024 * 64) // 64MB
#endif
#endif

namespace ec {
	class ipcwrk
	{
	public:
		ipcwrk() : _nst(st_unkown)
			, _sizercvbuf(SIZE_IPRCV_BUF)
			, _sizesndbuf(SIZE_IPSND_BUF)
			, _timeconnect(0)
			, _timeclose(0)
			, _blkallocator(1024 * 32 - EC_ALLOCTOR_ALIGN, 32) // 32K * 32blk = 1MB / heap
			, _sndbuf(EC_IPC_SNDBUF_MAXSIZE, &_blkallocator)
		{
			_fd = INVALID_SOCKET;
			_sname = "noname";
		}
		virtual ~ipcwrk() {
			if (INVALID_SOCKET != _fd) {
				closesocket(_fd);
				_fd = INVALID_SOCKET;
			}
			_nst = st_unkown;
			_sndbuf.clear();
		}
		enum {
			st_unkown = 0,
			st_connected = 1,
			st_logined = 2
		};

		int GetSocketErrno() {
#ifdef _WIN32
			return WSAGetLastError();
#else
			return errno;
#endif
		}
	protected:
		int _nst;
		int _sizercvbuf;
		int _sizesndbuf;
		ec::net::url _url;

	private:
		SOCKET _fd;
		pollfd _pollfd;
		time_t _timeconnect;
		time_t _timeclose;// for delay reconnect
		std::string _sname;
	private:
		ec::blk_alloctor<> _blkallocator;
		ec::io_buffer<> _sndbuf;
	protected:
		virtual void ondisconnect() = 0;
		virtual void onconnect() = 0;
		virtual int  onrecv(const uint8_t *pdata, size_t size) = 0; // return 0:ok; -1:error need close
		virtual void onidle() = 0;
		virtual void onreadonce() {};

	private:
		bool connectasyn(ec::ilog* plog = nullptr) {
			if (_fd != INVALID_SOCKET)
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
			if (s == INVALID_SOCKET) {
				if (plog)
					plog->add(CLOG_DEFAULT_ERR, "ipcwrk %s connectasyn create socket failed", _sname.c_str());
				return false;
			}

			net::setrecvbuf(s, _sizercvbuf);
			net::setsendbuf(s, _sizesndbuf);

			long ul = 1;
#ifdef _WIN32
			if (SOCKET_ERROR == ioctlsocket(s, FIONBIO, (unsigned long*)&ul)) {
				if (plog)
					plog->add(CLOG_DEFAULT_ERR, "ipcwrk %s connectasyn ioctl error %d", _sname.c_str(), WSAGetLastError());
				closesocket(s);
				return false;
			}
#else
			if (ioctl(s, FIONBIO, &ul) == -1) {
				if (plog)
					plog->add(CLOG_DEFAULT_ERR, "ipcwrk %s connectasyn ioctl error %d", _sname.c_str(), errno);
				::close(s);
				return false;
			}
			int flags = fcntl(s, F_GETFD);
			flags |= FD_CLOEXEC;
			fcntl(s, F_SETFD, flags);
#endif
			if (connect(s, (sockaddr *)(&srvaddr), sizeof(srvaddr)) < 0) {
#ifdef _WIN32
				int nerr = WSAGetLastError();
				if (WSAEWOULDBLOCK != nerr) {
					if (plog)
						plog->add(CLOG_DEFAULT_ERR, "ipcwrk %s connectasyn error %d", _sname.c_str(), nerr);
					closesocket(s);
					return false;
				}
#else
				if (EINPROGRESS != errno) {
					if (plog)
						plog->add(CLOG_DEFAULT_ERR, "ipcwrk %s connectasyn error %d", _sname.c_str(), errno);
					::close(s);
					return false;
				}
#endif
			}
			_fd = s;
			_nst = st_unkown;
			_timeconnect = ::time(0);
			_sndbuf.clear();
			return true;
		}

		void closefd(bool bnotify = true)
		{
			if (_fd != INVALID_SOCKET) {
#ifdef _WIN32
				shutdown(_fd, SD_BOTH);
				::closesocket(_fd);
#else
				shutdown(_fd, SHUT_RDWR);
				::close(_fd);
#endif
			}
			_fd = INVALID_SOCKET;
			_nst = st_unkown;
			if (bnotify)
				ondisconnect();
			_timeclose = ::time(nullptr);
		}

		int  sendbuf(ec::ilog* plog = nullptr) // return -1 error; >= 0 send size
		{
			if (_sndbuf.empty())
				return 0;
			int nr = 0, ns;
			const void* pd = nullptr;
			size_t zlen = 0;

			pd = _sndbuf.get(zlen);
			while (pd && zlen) {
				ns = net::_send_non_block(_fd, (const char*)pd, (int)(zlen));
				if (ns < 0) {
					if (plog)
						plog->add(CLOG_DEFAULT_ERR, "ipcwrk %s send failed, errno %d", _sname.c_str(), GetSocketErrno());
					return -1;
				}
				else if (!ns)
					return nr;
				_sndbuf.freesize(ns);
				nr += ns;
				if (ns < (int)(zlen))
					break;
				pd = _sndbuf.get(zlen);
			}
			return nr;
		}
	protected:
		int send_non_block(const void* p, int nsize, ec::ilog* plog = nullptr) // return -1:error; >=0 : send bytes
		{
			if (st_unkown == _nst)
				return -1;
			if (_sndbuf.empty()) { // buffer empty, send direct
				int nr = net::_send_non_block(_fd, (const char*)p, nsize);
				if (-1 == nr) {
					if (plog)
						plog->add(CLOG_DEFAULT_ERR, "ipcwrk %s send failed, errno %d", _sname.c_str(), GetSocketErrno());
					closefd();
					return -1;
				}
				else if (nr < nsize) {
					size_t zappend = 0;
					_sndbuf.append(((const char*)p) + nr, nsize - nr, &zappend);
					return nr + (int)zappend;
				}
				return nsize;
			}
			if (-1 == sendbuf(plog)) {
				closefd();
				return -1;
			}
			size_t zappend = 0;
			_sndbuf.append(p, nsize, &zappend);
			return (int)zappend;
		}
	public:
		bool init(const char* url, int sizercvbuf = 0, int sizesndbuf = 0, const char* sname = nullptr)
		{
			if (!_url.parse(url, strlen(url)))
				return false;
			if(sizercvbuf)
				_sizercvbuf = sizercvbuf;
			if(sizesndbuf)
				_sizesndbuf = sizesndbuf;
			if (sname && *sname)
				_sname = sname;
			return _url._port != 0 && !_url._protocol.empty();
		}

		void runtime(int waitmicroseconds, ec::ilog* plog = nullptr) {
			onidle();
			if (INVALID_SOCKET == _fd) {
				connectasyn(plog);
				return;
			}

			if (st_logined != _nst) {
				time_t tcur = ::time(0);
				if (tcur - _timeconnect > 15) { // connect time out 15 seconds
					if (plog)
						plog->add(CLOG_DEFAULT_ERR, "ipcwrk %s connect timeover", _sname.c_str());
					closefd();
					_timeconnect = tcur;
					return;
				}
			}
			_pollfd.fd = _fd;
			if (st_unkown == _nst)
				_pollfd.events = POLLOUT;
			else {
				_pollfd.events = _sndbuf.empty() ? POLLIN : POLLIN | POLLOUT;
			}
			_pollfd.revents = 0;
#ifdef _WIN32
			int n = WSAPoll(&_pollfd, 1, waitmicroseconds);
#else
			int n = poll(&_pollfd, 1, waitmicroseconds);
#endif
			if (n <= 0)
				return;
			if (_pollfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
				if (plog)
					plog->add(CLOG_DEFAULT_ERR, "ipcwrk %s disconnect as revents 0x%X", _sname.c_str(), _pollfd.revents);
				closefd();
				return;
			}

			else if (_pollfd.revents & POLLOUT) {
				if (st_unkown == _nst) {
					_nst = st_connected;
#ifndef _WIN32
					int serr = 0;
					socklen_t serrlen = sizeof(serr);
					getsockopt(_pollfd.fd, SOL_SOCKET, SO_ERROR, (void *)&serr, &serrlen);
					if (serr) {
						if (plog)
							plog->add(CLOG_DEFAULT_ERR, "ipcwrk %s connectasyn failed, get error %d", _sname.c_str(), serr);
						closefd();
						return;
					}
#endif
					onconnect();
				}
				else {
					int nsnedbyte = sendbuf(plog);
					if (-1 == nsnedbyte) {
						closefd();
						return;
					}
					if (plog && nsnedbyte > 0) {
						if (plog)
							plog->add(CLOG_DEFAULT_ALL, "ipcwrk %s sendbuf send %d bytes, buffer size %zu, allacator numheaps=%d, numfreeblks=%d", _sname.c_str(),
								nsnedbyte, _sndbuf.size(), _blkallocator.numheaps(), _blkallocator.numfreeblks());
					}
				}
			}
			else if (_pollfd.revents & POLLIN) {
				char rbuf[1024 * 32];
#ifdef _WIN32
				int nr = ::recv(_pollfd.fd, rbuf, (int)sizeof(rbuf), 0);
#else
				int nr = ::recv(_pollfd.fd, rbuf, (int)sizeof(rbuf), MSG_DONTWAIT);
#endif
				if (nr == 0) {   //close gracefully
					if (plog)
						plog->add(CLOG_DEFAULT_ERR, "ipcwrk %s disconnect as peer close gracefully", _sname.c_str());
					closefd();
					return;
				}
				else if (nr < 0) {
#ifdef _WIN32
					int nerr = WSAGetLastError();
					if (WSAEWOULDBLOCK == nerr)
						return;
#else
					int nerr = errno;
					if (nerr == EAGAIN || nerr == EWOULDBLOCK)
						return;
#endif
					if (plog)
						plog->add(CLOG_DEFAULT_ERR, "ipcwrk %s disconnect errno %d", _sname.c_str(), nerr);
					closefd();
					return;
				}
				if (-1 == onrecv((const uint8_t*)rbuf, nr)) {
					if (plog)
						plog->add(CLOG_DEFAULT_ERR, "ipcwrk %s disconnect as onrecv return -1", _sname.c_str());
					closefd();
					return;
				}
				onreadonce();
			}
		}
	};
}// namespace ec
