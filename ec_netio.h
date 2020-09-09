/*!
\file ec_netio.h
\author	jiangyong
\email  kipway@outlook.com
update 2020.9.6

functions for NET IO

eclib 3.0 Copyright (c) 2017-2020, kipway
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
namespace ec
{
	namespace net {
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
			int keepIdle = 20;
			int keepInterval = 2;
			int keepCount = 5;
			if (bfast) {
				keepIdle = 5;
				keepInterval = 1;
				keepCount = 3;
			}
			setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (void *)&keepAlive, sizeof(keepAlive));
			setsockopt(s, SOL_TCP, TCP_KEEPIDLE, (void*)&keepIdle, sizeof(keepIdle));
			setsockopt(s, SOL_TCP, TCP_KEEPINTVL, (void *)&keepInterval, sizeof(keepInterval));
			setsockopt(s, SOL_TCP, TCP_KEEPCNT, (void *)&keepCount, sizeof(keepCount));
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
				alive_in.keepalivetime = 20 * 1000;
				alive_in.keepaliveinterval = 2000;
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
			SOCKET tcpconnect(const charT* sip, unsigned short suport, int nTimeOutSec, bool bFIONBIO = false)
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
			connect(s, (sockaddr *)(&ServerHostAddr), sizeof(ServerHostAddr));

			TIMEVAL tv01 = { nTimeOutSec, 0 };
			fd_set fdw;
			FD_ZERO(&fdw);
			FD_SET(s, &fdw);
			int ne;
#ifdef _WIN32
			ne = ::select(0, NULL, &fdw, NULL, &tv01);
#else
			ne = ::select(s + 1, NULL, &fdw, NULL, &tv01);
#endif
			if (ne <= 0 || !FD_ISSET(s, &fdw)) {
				::closesocket(s);
				return  INVALID_SOCKET;
			}
			ul = 0;
#ifdef _WIN32
			if (!bFIONBIO) {
				if (SOCKET_ERROR == ioctlsocket(s, FIONBIO, (unsigned long*)&ul)) {
					::closesocket(s);
					return INVALID_SOCKET;
				}
			}
#else
			int serr = 0;
			socklen_t serrlen = sizeof(serr);
			getsockopt(s, SOL_SOCKET, SO_ERROR, (void *)&serr, &serrlen);
			if (serr) {
				::closesocket(s);
				return INVALID_SOCKET;
			}
			if (!bFIONBIO) {
				if (ioctl(s, FIONBIO, &ul) == -1) {
					::closesocket(s);
					return INVALID_SOCKET;
				}
			}
#endif
			return s;
		}

#ifndef _WIN32
		inline SOCKET	afunix_connect(unsigned short uport, int nTimeOutSec, bool bFIONBIO = false)
		{
			if (!uport)
				return INVALID_SOCKET;

			struct sockaddr_un srvaddr;
			memset(&srvaddr, 0, sizeof(srvaddr));
			srvaddr.sun_family = AF_UNIX;
			snprintf(srvaddr.sun_path, sizeof(srvaddr.sun_path), "/var/tmp/ECIPC:%d", uport);
			SOCKET s = socket(AF_UNIX, SOCK_STREAM, 0);

			if (s == INVALID_SOCKET)
				return INVALID_SOCKET;

			long ul = 1;

			if (ioctl(s, FIONBIO, &ul) == -1) {
				closesocket(s);
				return INVALID_SOCKET;
			}

			connect(s, (sockaddr *)(&srvaddr), sizeof(srvaddr));

			TIMEVAL tv01 = { nTimeOutSec, 0 };
			fd_set fdw;
			FD_ZERO(&fdw);
			FD_SET(s, &fdw);
			int ne;

			ne = ::select(s + 1, NULL, &fdw, NULL, &tv01);

			if (ne <= 0 || !FD_ISSET(s, &fdw)) {
				closesocket(s);
				return  INVALID_SOCKET;
			}
			ul = 0;

			int serr = 0;
			socklen_t serrlen = sizeof(serr);
			getsockopt(s, SOL_SOCKET, SO_ERROR, (void *)&serr, &serrlen);
			if (serr) {
				::closesocket(s);
				return INVALID_SOCKET;
			}
			if (!bFIONBIO) {
				if (ioctl(s, FIONBIO, &ul) == -1) {
					closesocket(s);
					return INVALID_SOCKET;
				}
			}
			return s;
		}
#endif

		//return send bytes size or -1 for error,use for block or nonblocking  mode
		template<typename SCK
			, class = typename std::enable_if<std::is_same<SCK, SOCKET>::value>::type>
			int tcpsend(SCK s, const void* pbuf, int nsize, int ntimeoutmsec = 4000)
		{
			char *ps = (char*)pbuf;
			int  nsend = 0, ns = 0;
			int  nret;
			while (nsend < nsize) {
#ifdef _WIN32
				nret = ::send(s, ps + nsend, nsize - nsend, 0);
				if (SOCKET_ERROR == nret) {
					int nerr = WSAGetLastError();
					if (WSAEWOULDBLOCK == nerr || WSAENOBUFS == nerr) { // nonblocking  mode
						TIMEVAL tv01 = { 0, 1000 * 100 };
						fd_set fdw, fde;
						FD_ZERO(&fdw);
						FD_ZERO(&fde);
						FD_SET(s, &fdw);
						FD_SET(s, &fde);
						if (-1 == ::select(0, NULL, &fdw, &fde, &tv01))
							return SOCKET_ERROR;
						if (FD_ISSET(s, &fde))
							return SOCKET_ERROR;
						ns++;
						if (ns > ntimeoutmsec / 100 + 1)
							return SOCKET_ERROR;
						continue;
					}
					else
						return SOCKET_ERROR;
				}
				else if (nret > 0) {
					ns = 0;
					nsend += nret;
				}
#else
				nret = ::send(s, ps + nsend, nsize - nsend, MSG_DONTWAIT | MSG_NOSIGNAL);
				if (SOCKET_ERROR == nret) {
					if (errno == EAGAIN || errno == EWOULDBLOCK) { // nonblocking  mode
						TIMEVAL tv01 = { 0, 1000 * 100 };
						fd_set fdw, fde;
						FD_ZERO(&fdw);
						FD_ZERO(&fde);
						FD_SET(s, &fdw);
						FD_SET(s, &fde);
						if (-1 == ::select(s + 1, NULL, &fdw, &fde, &tv01))
							return SOCKET_ERROR;
						if (FD_ISSET(s, &fde))
							return SOCKET_ERROR;
						ns++;
						if (ns > ntimeoutmsec / 100 + 1) //4 secs
							return SOCKET_ERROR;
						continue;
					}
					else
						return SOCKET_ERROR;
				}
				else if (nret > 0) {
					nsend += nret;
					ns = 0;
				}
#endif
			}
			return nsend;
		}

		template<typename SCK
			, class = typename std::enable_if<std::is_same<SCK, SOCKET>::value>::type>
			int send_non_block(SCK s, const void* pbuf, int nsize)//return send bytes size or -1 for error,use for nonblocking
		{
			int  nret;
#ifdef _WIN32
			if (nsize > 1024 * 32)
				nsize = 1024 * 32;
			nret = ::send(s, (char*)pbuf, nsize, 0);
			if (SOCKET_ERROR == nret) {
				int nerr = WSAGetLastError();
				if (WSAEWOULDBLOCK == nerr || WSAENOBUFS == nerr)  // nonblocking  mode
					return 0;
				else
					return SOCKET_ERROR;
			}
			return nret;
#else
			nret = ::send(s, (char*)pbuf, nsize, MSG_DONTWAIT | MSG_NOSIGNAL);
			if (SOCKET_ERROR == nret) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) // nonblocking  mode
					return 0;
				else
					return SOCKET_ERROR;
			}
			return nret;
#endif
		};

		template<typename SCK
			, class = typename std::enable_if<std::is_same<SCK, SOCKET>::value>::type>
			int tcpread(SCK s, void* pbuf, int nbufsize, int nTimeOutMsec)
		{
			if (s == INVALID_SOCKET)
				return SOCKET_ERROR;

			TIMEVAL tv01 = { nTimeOutMsec / 1000, 1000 * (nTimeOutMsec % 1000) };
			fd_set fdr, fde;
			FD_ZERO(&fdr);
			FD_ZERO(&fde);
			FD_SET(s, &fdr);
			FD_SET(s, &fde);

#ifdef _WIN32
			int nRet = ::select(0, &fdr, NULL, &fde, &tv01);
#else
			int nRet = ::select(s + 1, &fdr, NULL, &fde, &tv01);
#endif

			if (SOCKET_ERROR == nRet)
				return SOCKET_ERROR;

			if (nRet == 0)
				return 0;
			if (FD_ISSET(s, &fde))
				return SOCKET_ERROR;

			nRet = ::recv(s, (char*)pbuf, nbufsize, 0);

			if (nRet <= 0)
				return SOCKET_ERROR;
			return nRet;
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
	}//net
}// ec
