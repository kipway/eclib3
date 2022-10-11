/*!
\file ec_netio.h
\author	jiangyong
\email  kipway@outlook.com
update 2022.8.25

functions for NET IO

eclib 3.0 Copyright (c) 2017-2022, kipway
source repository : https://github.com/kipway/eclib

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/
#pragma once

#ifdef _WIN32
#	pragma warning(disable : 4996)
#	include <winsock2.h>
#	include <mstcpip.h>
#   include <ws2tcpip.h>
#else
#	include <unistd.h>
#	include <sys/time.h>
#	include <sys/types.h>
#	include <sys/socket.h>
#   include <sys/un.h>
#	include <sys/ioctl.h>
#	include <sys/select.h>
#	include <netinet/tcp.h>
#	include <arpa/inet.h>
#	include <errno.h>
#   include <netdb.h>
#	include <poll.h>

#ifndef SOCKET
#	define SOCKET int
#endif

#ifndef INVALID_SOCKET
#	define INVALID_SOCKET    (-1)
#endif

#ifndef SOCKET_ERROR
#	define SOCKET_ERROR      (-1)
#endif

#ifndef closesocket
#	define closesocket(a) close(a)
#endif

#ifndef TIMEVAL
#	define TIMEVAL struct timeval
#endif

#endif

#include <type_traits>
#include "ec_string.h"
#include "ec_time.h"

#define MAX_XPOLLTCPSRV_THREADS 16
#define NETIO_EVT_NONE 0
#define NETIO_EVT_IN   1
#define NETIO_EVT_OUT  2
#define NETIO_EVT_ERR -1
namespace ec
{
	namespace net {
		template<typename SCK
			, class = typename std::enable_if<std::is_same<SCK, SOCKET>::value>::type>
			int io_wait(SCK s, int evt, int millisecond) // return NETIO_EVT_XXX
		{
#ifdef _WIN32
			TIMEVAL tv01 = { millisecond / 1000, 1000 * (millisecond % 1000) };
			int ne = 0;
			fd_set fdr, fdw, fde;
			FD_ZERO(&fdr);
			FD_ZERO(&fdw);
			FD_ZERO(&fde);
			if (evt & NETIO_EVT_IN)
				FD_SET(s, &fdr);
			if (evt & NETIO_EVT_OUT)
				FD_SET(s, &fdw);
			FD_SET(s, &fde);
			ne = ::select(0, (evt & NETIO_EVT_IN) ? &fdr : nullptr, (evt & NETIO_EVT_OUT) ? &fdw : nullptr, &fde, &tv01);
			if (SOCKET_ERROR == ne || ((ne > 0) && FD_ISSET(s, &fde)))
				return NETIO_EVT_ERR;
			if (0 == ne)
				return NETIO_EVT_NONE;
			return (FD_ISSET(s, &fdr) ? NETIO_EVT_IN : 0) + (FD_ISSET(s, &fdw) ? NETIO_EVT_OUT : 0);
#else
			pollfd tfd;
			tfd.fd = s;
			tfd.events = 0;
			tfd.revents = 0;
			if (evt & NETIO_EVT_IN)
				tfd.events |= POLLIN;
			if (evt & NETIO_EVT_OUT)
				tfd.events |= POLLOUT;
			int ne = poll(&tfd, 1, millisecond);
			if (ne < 0)
				return NETIO_EVT_ERR;
			if (ne == 0)
				return NETIO_EVT_NONE;
			if (tfd.revents & (POLLERR | POLLHUP | POLLNVAL)) // error
				return NETIO_EVT_ERR;
			return ((tfd.revents & POLLIN) ? NETIO_EVT_IN : 0) + ((tfd.revents & POLLOUT) ? NETIO_EVT_OUT : 0);
#endif
		}

		template<typename SCK
			, class = typename std::enable_if<std::is_same<SCK, SOCKET>::value>::type>
			void tcpnodelay(SCK s)
		{
			int bNodelay = 1;
			setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char *)&bNodelay, sizeof(bNodelay));
		}

		template<typename SCK
			, class = typename std::enable_if<std::is_same<SCK, SOCKET>::value>::type>
			int getrecvbuf(SCK s)
		{
			int nval = 0;
			socklen_t nsize = (socklen_t)sizeof(nval);
			if (-1 == getsockopt(s, SOL_SOCKET, SO_RCVBUF, (char *)&nval, &nsize))
				return -1;
			return nval;
		}

		template<typename SCK
			, class = typename std::enable_if<std::is_same<SCK, SOCKET>::value>::type>
			int setrecvbuf(SCK s, int n)
		{
			int nval = n;
			if (-1 == setsockopt(s, SOL_SOCKET, SO_RCVBUF, (char *)&nval, (socklen_t)sizeof(nval)))
				return -1;
			return nval;
		}

		template<typename SCK
			, class = typename std::enable_if<std::is_same<SCK, SOCKET>::value>::type>
			int getsendbuf(SCK s)
		{
			int nval = 0;
			socklen_t nsize = (socklen_t)sizeof(nval);
			if (-1 == getsockopt(s, SOL_SOCKET, SO_SNDBUF, (char *)&nval, &nsize))
				return -1;
			return nval;
		}

		template<typename SCK
			, class = typename std::enable_if<std::is_same<SCK, SOCKET>::value>::type>
			int setsendbuf(SCK s, int n)
		{
			int nval = n;
			if (-1 == setsockopt(s, SOL_SOCKET, SO_SNDBUF, (char *)&nval, (socklen_t)sizeof(nval)))
				return -1;
			return nval;
		}
#ifndef _WIN32
		template<typename SCK
			, class = typename std::enable_if<std::is_same<SCK, SOCKET>::value>::type>
			bool setkeepalive(SCK s, bool bfast = false)
		{
			int keepAlive = 1;
			int keepIdle = 30;
			int keepInterval = 5;
			int keepCount = 3;
			if (bfast) {
				keepIdle = 5;
				keepInterval = 1;
				keepCount = 3;
			}
			setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (void *)&keepAlive, sizeof(keepAlive));
			setsockopt(s, SOL_TCP, TCP_KEEPIDLE, (void*)&keepIdle, sizeof(keepIdle));
			setsockopt(s, SOL_TCP, TCP_KEEPINTVL, (void *)&keepInterval, sizeof(keepInterval));
			setsockopt(s, SOL_TCP, TCP_KEEPCNT, (void *)&keepCount, sizeof(keepCount));

			unsigned int tcp_timeout = bfast ? 15000 : 30000; //15/30 seconds before aborting a write()
			setsockopt(s, SOL_TCP, TCP_USER_TIMEOUT, &tcp_timeout, sizeof(unsigned int));
			return true;
		}
#else
		template<typename SCK
			, class = typename std::enable_if<std::is_same<SCK, SOCKET>::value>::type>
			bool setkeepalive(SCK sock, bool bfast = false)
		{
			BOOL bKeepAlive = 1;
			int nRet = setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE,
				(char*)&bKeepAlive, sizeof(bKeepAlive));
			if (nRet == SOCKET_ERROR)
				return false;
			tcp_keepalive alive_in;
			tcp_keepalive alive_out;
			if (bfast) {
				alive_in.keepalivetime = 5 * 1000;
				alive_in.keepaliveinterval = 1000;
			}
			else {
				alive_in.keepalivetime = 30 * 1000;
				alive_in.keepaliveinterval = 5000;
			}
			alive_in.onoff = 1;
			unsigned long ulBytesReturn = 0;

			nRet = WSAIoctl(sock, SIO_KEEPALIVE_VALS, &alive_in, sizeof(alive_in),
				&alive_out, sizeof(alive_out), &ulBytesReturn, NULL, NULL);
			if (nRet == SOCKET_ERROR)
				return false;
			return true;
		}
#endif

		template<typename charT
			, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
			SOCKET tcpconnect(const charT* sip, unsigned short suport, int seconds, bool bFIONBIO = false)
		{
			if (!sip || !*sip || !inet_addr(sip) || !suport)
				return INVALID_SOCKET;

			struct sockaddr_in ServerHostAddr = { 0 };
			ServerHostAddr.sin_family = AF_INET;
			ServerHostAddr.sin_port = htons(suport);
			ServerHostAddr.sin_addr.s_addr = inet_addr(sip);
			SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

			if (s == INVALID_SOCKET)
				return INVALID_SOCKET;

			long ul = 1; // set none block
#ifdef _WIN32
			if (SOCKET_ERROR == ioctlsocket(s, FIONBIO, (unsigned long*)&ul)) {
				::closesocket(s);
				return INVALID_SOCKET;
			}
#else
			if (ioctl(s, FIONBIO, &ul) == -1) {
				::closesocket(s);
				return INVALID_SOCKET;
			}
#endif
			int nerr = connect(s, (sockaddr *)(&ServerHostAddr), sizeof(ServerHostAddr));
			if (nerr == -1) {
#ifdef _WIN32
				int errcode = WSAGetLastError();
				if (WSAEWOULDBLOCK != errcode) {
					::closesocket(s);
					return INVALID_SOCKET;
				}
#else
				int errcode = errno;
				if (errcode != EAGAIN && errcode != EINPROGRESS) {
					::closesocket(s);
					return INVALID_SOCKET;
				}
#endif
			}
			if (io_wait(s, NETIO_EVT_OUT, seconds * 1000) <= 0) {
				::closesocket(s);
				return  INVALID_SOCKET;
			}
			if (!bFIONBIO) {
				ul = 0;
#ifdef _WIN32
				if (SOCKET_ERROR == ioctlsocket(s, FIONBIO, (unsigned long*)&ul)) {
					::closesocket(s);
					return INVALID_SOCKET;
				}
#else
				if (ioctl(s, FIONBIO, &ul) == -1) {
					::closesocket(s);
					return INVALID_SOCKET;
				}
#endif
			}
			return s;
		}

#ifndef _WIN32
		inline SOCKET	afunix_connect(unsigned short uport, int seconds, const char* skey, bool bFIONBIO = false)
		{
			if (!uport)
				return INVALID_SOCKET;

			struct sockaddr_un srvaddr;
			memset(&srvaddr, 0, sizeof(srvaddr));
			srvaddr.sun_family = AF_UNIX;
			snprintf(srvaddr.sun_path, sizeof(srvaddr.sun_path), "/var/tmp/%s:%d", skey, uport);
			SOCKET s = socket(AF_UNIX, SOCK_STREAM, 0);

			if (s == INVALID_SOCKET)
				return INVALID_SOCKET;

			long ul = 1;

			if (ioctl(s, FIONBIO, &ul) == -1) {
				closesocket(s);
				return INVALID_SOCKET;
			}

			int nst = connect(s, (sockaddr *)(&srvaddr), sizeof(srvaddr));
			if (nst == -1) {
				int errcode = errno;
				if (errcode != EAGAIN && errcode != EINPROGRESS) {
					::closesocket(s);
					return INVALID_SOCKET;
				}
			}
			if (io_wait(s, NETIO_EVT_OUT, seconds * 1000) <= 0) {
				::closesocket(s);
				return  INVALID_SOCKET;
			}

			if (!bFIONBIO) {
				ul = 0;
				if (ioctl(s, FIONBIO, &ul) == -1) {
					::closesocket(s);
					return INVALID_SOCKET;
				}
			}
			return s;
		}
#endif
		template<typename SCK
			, class = typename std::enable_if<std::is_same<SCK, SOCKET>::value>::type>
			int _send_non_block(SCK s, const char* pbuf, int nsize)//return send bytes size or -1 for error,use for nonblocking
		{
			int  nret = 0;
#ifdef _WIN32
			nret = ::send(s, pbuf, nsize, 0);
			if (SOCKET_ERROR == nret) {
				int nerr = WSAGetLastError();
				if (WSAEWOULDBLOCK == nerr || WSAENOBUFS == nerr)  // nonblocking  mode
					return 0;
			}
#else
			nret = ::send(s, pbuf, nsize, MSG_DONTWAIT | MSG_NOSIGNAL);
			if (SOCKET_ERROR == nret) {
				if (EAGAIN == errno || EWOULDBLOCK == errno) // nonblocking  mode
					return 0;
			}
#endif
			return nret;
		};

		//return send bytes size or -1 for error,use for block or nonblocking  mode
		template<typename SCK
			, class = typename std::enable_if<std::is_same<SCK, SOCKET>::value>::type>
			int tcpsend(SCK s, const void* pbuf, int nsize, int millisecond = 4000)
		{
			const char *ps = (const char*)pbuf;
			int  nsend = 0;
			int  nret, ne;
			int64_t tend = 0, tcur = 0;
			do {
				nret = _send_non_block(s, ps + nsend, nsize - nsend);
				if (nret < 0)
					return SOCKET_ERROR;
				nsend += nret;
				if (nsend < nsize) {
					ne = io_wait(s, NETIO_EVT_OUT, 100);
					if (ne == NETIO_EVT_ERR)
						return SOCKET_ERROR;
					tcur = mstime();
					if (!tend)
						tend = tcur + millisecond;
				}
			} while (nsend < nsize && tcur < tend);
			return nsend;
		}

		template<typename SCK
			, class = typename std::enable_if<std::is_same<SCK, SOCKET>::value>::type>
			int send_non_block(SCK s, const void* pbuf, int nsize)//return send bytes size or -1 for error,use for nonblocking
		{
			int  nret = 0;
#ifdef _WIN32
			if (nsize > 1024 * 32)
				nsize = 1024 * 32;
			nret = ::send(s, (const char*)pbuf, nsize, 0);
			if (SOCKET_ERROR == nret) {
				int nerr = WSAGetLastError();
				if (WSAEWOULDBLOCK == nerr || WSAENOBUFS == nerr)  // nonblocking  mode
					return 0;
			}
#else
			nret = ::send(s, (const char*)pbuf, nsize, MSG_DONTWAIT | MSG_NOSIGNAL);
			if (SOCKET_ERROR == nret) {
				if (EAGAIN == errno || EWOULDBLOCK == errno) // nonblocking  mode
					return 0;
			}
#endif
			return nret;
		};

		template<typename SCK
			, class = typename std::enable_if<std::is_same<SCK, SOCKET>::value>::type>
			int tcpread(SCK s, void* pbuf, int nbufsize, int millisecond)
		{
			if (s == INVALID_SOCKET)
				return SOCKET_ERROR;
			int ne = io_wait(s, NETIO_EVT_IN, millisecond);
			if (ne <= 0)
				return ne;

			int nr = ::recv(s, (char*)pbuf, nbufsize, 0);
			if (nr == 0) // close
				return SOCKET_ERROR;
			else if (nr < 0) {
#ifdef _WIN32
				int nerr = (int)WSAGetLastError();
				if (WSAEWOULDBLOCK == nerr)
					return 0;
#else
				if (EAGAIN == errno || EWOULDBLOCK == errno)
					return 0;
#endif
			}
			return nr;
		}

		template<typename charT
			, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
			unsigned int gethostip(const charT* shost) //return net byte order
		{
			unsigned int uip = 0;
			struct addrinfo *result = NULL;
			struct addrinfo *ptr = NULL;
			struct addrinfo hints;

			memset(&hints, 0, sizeof(hints));
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;

			if (getaddrinfo(shost, NULL, &hints, &result))
				return 0;

			for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {
				if (ptr->ai_family == AF_INET) {
#ifdef _WIN32
					uip = ((struct sockaddr_in *) ptr->ai_addr)->sin_addr.S_un.S_addr;
#else
					uip = ((struct sockaddr_in *) ptr->ai_addr)->sin_addr.s_addr;
#endif
					break;
				}
			}
			if (result)
				freeaddrinfo(result);
			return uip;
		}

		template<typename charT
			, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
			SOCKET tcpconnectasyn(const charT* sip, unsigned short suport, int &status) //异步连接
		{
			if (!sip || !*sip || !suport)
				return INVALID_SOCKET;

			struct sockaddr_in ServerHostAddr = { 0 };
			ServerHostAddr.sin_family = AF_INET;
			ServerHostAddr.sin_port = htons(suport);
			ServerHostAddr.sin_addr.s_addr = inet_addr(sip);
			if (INADDR_NONE == ServerHostAddr.sin_addr.s_addr)
				return INVALID_SOCKET;
			SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (s == INVALID_SOCKET)
				return INVALID_SOCKET;
			long ul = 1;
#ifdef _WIN32
			if (SOCKET_ERROR == ioctlsocket(s, FIONBIO, (unsigned long*)&ul)) {
				closesocket(s);
				return INVALID_SOCKET;
			}
#else
			if (ioctl(s, FIONBIO, &ul) == -1) {
				closesocket(s);
				return INVALID_SOCKET;
			}
#endif
			status = connect(s, (sockaddr *)(&ServerHostAddr), sizeof(ServerHostAddr));
			return s;
		}
#ifdef _WIN32

		template<typename charT
			, class = typename std::enable_if<std::is_same<charT, char>::value>::type>
			const char *inet_ntop_xp(int af, const void *src, charT *dst, socklen_t size)
		{
			struct sockaddr_storage ss;
			unsigned long s = size;

			ZeroMemory(&ss, sizeof(ss));
			ss.ss_family = af;

			switch (af) {
			case AF_INET:
				((struct sockaddr_in *)&ss)->sin_addr = *(struct in_addr *)src;
				break;

			case AF_INET6:
				((struct sockaddr_in6 *)&ss)->sin6_addr = *(struct in6_addr *)src;
				break;

			default:
				return NULL;
			}
			// cannot directly use &size because of strict aliasing rules
			return WSAAddressToStringA((struct sockaddr *)&ss, sizeof(ss), NULL, dst, &s) == 0 ? dst : NULL;
		}
#endif

		template<class _Str>
		int get_ip_by_domain(const char *domain, _Str& sout)
		{
			char **pptr;
			struct hostent *hptr;
			hptr = gethostbyname(domain);
			if (nullptr == hptr) {
				return -1;
			}
			int n = 0;
			char sip[40] = { 0 };
			if (hptr->h_addrtype == AF_INET) {
				for (pptr = hptr->h_addr_list; *pptr != nullptr; pptr++) {
#ifdef _WIN32
					if (nullptr != inet_ntop_xp(hptr->h_addrtype, *pptr, sip, sizeof(sip))) {
#else
					if (nullptr != inet_ntop(hptr->h_addrtype, *pptr, sip, sizeof(sip))) {
#endif
						n++;
						sout = sip;
						if (n > 1)
							return 0;
					}
				}
				return n > 0 ? 0 : -1;
			}
			return -1;
		}
		/*!
		parse url
		demo:
		udp://0.0.0.0:999/path?level=dbg
		*/
		class url
		{
		public:
			url() :_port(0) {
			}
			bool parse(const char* surl, size_t urlsize)
			{
				ec::strargs ss;
				ec::strsplit(":/", surl, urlsize, ss, 4);
				if (ss.size() < 2)
					return false;
				_protocol.clear();
				_ip.clear();
				_path.clear();
				_args.clear();

				_protocol.append(ss[0]._str, ss[0]._size);

				_ip.append(ss[1]._str, ss[1]._size);
				if (INADDR_NONE == inet_addr(_ip.c_str())) {
					std::string t = _ip;
					if (ec::net::get_ip_by_domain(t.c_str(), _ip) < 0)
						return false;
				}

				if ((ss.size() > 2) && (*(ss[2]._str - 1) == ':')) {
					_port = ss[2].stoi();
					if(ss.size() > 3)
						_path.append(ss[3]._str, urlsize - (ss[3]._str - surl));
					if (!_port)
						return false;
				}
				else {
					_port = 0;
					_path.append(ss[2]._str, urlsize - (ss[2]._str - surl));
				}
				size_t apos = _path.find_first_of('?');
				if (std::string::npos != apos) {
					_args.append(_path.c_str() + apos + 1);
					_path.resize(apos);
				}
				return true;
			}
		public:
			uint16_t _port;
			std::string _protocol;
			std::string _ip;
			std::string _path;
			std::string _args;
		};
	}//net
}// ec
