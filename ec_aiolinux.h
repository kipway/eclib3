/*!
\file netio_linux.h

原生的linux网络IO, epoll模型.

\author jiangyong

\update 2023-2-1  增加TCP ipv6支持
\update 2022-11-9 适配ec_aiosrv.h
\update 2022-2-12 删除sctp,fd改为增长至增加INT32_MAX然后回到1
\update 2021-11-7 增加sctp支持和 TCP setkeepalive函数
\update 2021-6-17 增加 int getfdtype(int fd)
\update 2021-6-19 init中增加发送和接收缓冲参数设置.
\fix    2021-6-20 修正缓冲大小单位错误.

* eclib 3.0 Copyright (c) 2017-2023, kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/
#pragma once

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/epoll.h>

#include <string>
#include "ec_map.h"
#include "ec_netio.h"
#include "ec_jsonx.h"
#include "ec_vector.hpp"
#ifndef SIZE_MAX_FD
#define SIZE_MAX_FD  16384 //最大fd连接数
#endif

class netio_linux
{
public:
	enum fdtype {
		fd_tcp = 0,
		fd_tcpout,
		fd_listen,
		fd_epoll,
		fd_udp
	};

	struct t_fd
	{
		int  kfd;
		int  fdtype;
		int  sysfd; //>=0;  <0 error
		uint32_t pollevents;
	};

	struct keq_fd
	{
		bool operator()(int key, const t_fd& val)
		{
			return key == val.kfd;
		}
	};

private:
	int _nextfd;
	int _sizercvbuf, _sizesndbuf; // kbytes
	ec::hashmap<int, t_fd, keq_fd> _mapfd;

	int nextfd()
	{
		if (_mapfd.size() >= SIZE_MAX_FD)
			return -1;
		do {
			++_nextfd;
			if (_nextfd == INT32_MAX)
				_nextfd = 1;
		} while (_mapfd.has(_nextfd));
		return _nextfd;
	}

	void setfd(int kfd, fdtype type, int sysfd)
	{
		t_fd t;
		int flags = fcntl(sysfd, F_GETFD);
		flags |= FD_CLOEXEC;
		fcntl(sysfd, F_SETFD, flags);

		t.kfd = kfd;
		t.fdtype = type;
		t.sysfd = sysfd;
		t.pollevents = 0;
		_mapfd.set(kfd, t);
	}

	bool socketfull()
	{
		return _mapfd.size() >= SIZE_MAX_FD;
	}

public:
	inline int geterrno() {
		return errno;
	}
	ec::hashmap<int, t_fd, keq_fd>& getmap() {
		return _mapfd;
	}
public:
	netio_linux() :_nextfd(0), _sizercvbuf(128), _sizesndbuf(128), _mapfd(1024)
	{
	}
	~netio_linux() {
		ec::vector<int> vepolls;
		vepolls.reserve(1000);
		for (auto& i : _mapfd) {
			if (fd_epoll == i.fdtype)
				vepolls.push_back(i.sysfd);
			else {
				if (fd_tcp == i.fdtype || fd_tcpout == i.fdtype || fd_udp == i.fdtype)
					shutdown(i.sysfd, SHUT_RDWR);
				closesocket(i.sysfd);
			}
		}
		_mapfd.clear();
		for (auto& i : vepolls)
			close(i);
	}
	inline size_t size() {
		return _mapfd.size();
	}
	template<class STR_ = std::string>
	bool init(const char* env, STR_& errout)//{"rcvbufsize":256, "sndbufsize":8192}
	{
		if (!env || !*env)
			return true;
		ec::json js;
		if (!js.from_str(env, strlen(env))) {
			errout = "parse env string failed.";
			return false;
		}
		int n = 0;
		if (js.get_jnumber("rcvbufsize", n)) {
			if (n < 32)
				_sizercvbuf = 32;
			if (n > 262144)
				_sizercvbuf = 262144;
		}
		if (js.get_jnumber("sndbufsize", n)) {
			if (n < 32)
				_sizesndbuf = 32;
			if (n > 262144)
				_sizesndbuf = 262144;
		}
		errout.clear();
		return true;
	}

	inline int epoll_create_(int size)
	{
		int kfd = nextfd();
		if (kfd < 0)
			return -1;
		int sysfd = epoll_create(1);
		if (sysfd < 0)
			return -1;
		setfd(kfd, fd_epoll, sysfd);
		return kfd;
	}

	inline int epoll_ctl_(int epfd, int op, int fd, struct epoll_event* event)
	{
		t_fd* pepoll = _mapfd.get(epfd);
		if (!pepoll || pepoll->fdtype != fd_epoll)
			return -1;
		t_fd* pfd = _mapfd.get(fd);
		if (!pfd)
			return -1;
		if (event) {
			if (EPOLL_CTL_MOD == op && pfd->pollevents == event->events)
				return 0;
			pfd->pollevents = event->events;
		}
		return epoll_ctl(pepoll->sysfd, op, pfd->sysfd, event);
	}

	inline int epoll_wait_(int epfd, struct epoll_event* events, int maxevents, int timeout)
	{
		t_fd* pepoll = _mapfd.get(epfd);
		if (!pepoll || pepoll->fdtype != fd_epoll)
			return -1;
		return epoll_wait(pepoll->sysfd, events, maxevents, timeout);
	}

	inline void close_(int fd) //shutdown and close one fd
	{
		t_fd* p = _mapfd.get(fd);
		if (!p)
			return;
		if (fd_tcp == p->fdtype || fd_tcpout == p->fdtype)
			shutdown(p->sysfd, SHUT_RDWR);
		close(p->sysfd);
		_mapfd.erase(fd);
	}

	int connect_asyn(const struct sockaddr* addr, socklen_t addrlen) //connect nobloack, return fd
	{
		int sysfd, kfd = nextfd();
		if (kfd < 0)
			return -1;
		sysfd = socket(addr->sa_family, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
		if (sysfd < 0)
			return -1;
		if (connect(sysfd, addr, addrlen) < 0) {
			if (errno != EAGAIN && errno != EINPROGRESS) {
				close(sysfd);
				return -1;
			}
		}
		ec::net::setrecvbuf(sysfd, _sizercvbuf * 1024);
		ec::net::setsendbuf(sysfd, _sizesndbuf * 1024);
		setfd(kfd, fd_tcpout, sysfd);
		return kfd;
	}

	int bind_listen(const struct sockaddr* addr, socklen_t addrlen, int ipv6only = 0) // bind and listen return fd
	{
		int sysfd, kfd = nextfd();
		if (kfd < 0)
			return -1;
		sysfd = socket(addr->sa_family, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
		if (sysfd < 0)
			return -1;
		int opt = 1; // IPV6_V6ONLY
		if (ipv6only && addr->sa_family == AF_INET6) {
			if (-1 == setsockopt(sysfd, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&opt, sizeof(opt))) {
				close(sysfd);
				return -1;
			}
		}
		opt = 1; // reuse
		if (-1 == setsockopt(sysfd, SOL_SOCKET, SO_REUSEADDR, (const void*)&opt, sizeof(opt))) {
			close(sysfd);
			return -1;
		}
		if (bind(sysfd, addr, addrlen) < 0 || listen(sysfd, SOMAXCONN) < 0) {
			close(sysfd);
			return -1;
		}
		setfd(kfd, fd_listen, sysfd);
		return kfd;
	}

	int bind_listen_afunix(const char* spath) // bind and listen af_unix return fd
	{
		int kfd = nextfd();
		if (kfd < 0)
			return -1;

		SOCKET sysfd = INVALID_SOCKET;
		if ((sysfd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0)) == INVALID_SOCKET)
			return -1;

		struct sockaddr_un Addr;
		memset(&Addr, 0, sizeof(Addr));
		Addr.sun_family = AF_UNIX;
		snprintf(Addr.sun_path, sizeof(Addr.sun_path), "%s", spath);
		unlink(Addr.sun_path);
		if (bind(sysfd, (const sockaddr*)&Addr, sizeof(Addr)) == SOCKET_ERROR) {
			fprintf(stderr, "ERR:af_unix [%s] bind failed with error %d\n", spath, errno);
			::close(sysfd);
			return -1;
		}
		if (listen(sysfd, SOMAXCONN) == SOCKET_ERROR) {
			fprintf(stderr, "ERR: af_unix [%s]  listen failed with error %d\n", spath, errno);
			::close(sysfd);
			return -1;
		}
		setfd(kfd, fd_listen, sysfd);
		return kfd;
	}

	inline int accept_(int fd, struct sockaddr* addr, socklen_t* addrlen) // return fd
	{
		t_fd* p = _mapfd.get(fd);
		if (!p || fd_listen != p->fdtype)
			return -1;
		int sysfd = accept(p->sysfd, addr, addrlen);
		if (sysfd < 0)
			return -1;
		int kfd = nextfd();
		if (kfd < 0) {
			close(sysfd);
			return -1;
		}
		ec::net::setrecvbuf(sysfd, _sizercvbuf * 1024);
		ec::net::setsendbuf(sysfd, _sizesndbuf * 1024);
		setfd(kfd, fd_tcp, sysfd);
		return kfd;
	}

	inline int recv_(int fd, void* buf, size_t len, int flags)
	{
		t_fd* p = _mapfd.get(fd);
		if (!p || (fd_tcp != p->fdtype && fd_tcpout != p->fdtype))
			return -1;
		return recv(p->sysfd, buf, len, MSG_DONTWAIT);
	}

	inline int send_(int fd, const void* buf, size_t len, int flags)
	{
		t_fd* p = _mapfd.get(fd);
		if (!p || (fd_tcp != p->fdtype && fd_tcpout != p->fdtype))
			return -1;
		return send(p->sysfd, buf, len, flags);
	}
	
	inline int shutdown_(int fd, int how)
	{
		t_fd* p = _mapfd.get(fd);
		if (!p || (fd_tcp != p->fdtype && fd_tcpout != p->fdtype))
			return -1;
		return shutdown(p->sysfd, SHUT_RDWR);
	}

	void getall(ec::vector<int>& fds)
	{
		fds.reserve(_mapfd.size());
		for (auto& i : _mapfd)
			fds.push_back(i.kfd);
	}

	int getfdtype(int fd)
	{
		auto* pi = _mapfd.get(fd);
		if (!pi)
			return -1;
		return pi->fdtype;
	}

	int setkeepalive(int fd, bool bfast = false)
	{
		t_fd* p = _mapfd.get(fd);
		if (!p || (fd_tcp != p->fdtype && fd_tcpout != p->fdtype))
			return -1;

		int keepAlive = 1;
		int keepIdle = 30;
		int keepInterval = 5;
		int keepCount = 3;
		if (bfast) {
			keepIdle = 5;
			keepInterval = 1;
			keepCount = 3;
		}
		setsockopt(p->sysfd, SOL_SOCKET, SO_KEEPALIVE, (void*)&keepAlive, sizeof(keepAlive));
		setsockopt(p->sysfd, SOL_TCP, TCP_KEEPIDLE, (void*)&keepIdle, sizeof(keepIdle));
		setsockopt(p->sysfd, SOL_TCP, TCP_KEEPINTVL, (void*)&keepInterval, sizeof(keepInterval));
		setsockopt(p->sysfd, SOL_TCP, TCP_KEEPCNT, (void*)&keepCount, sizeof(keepCount));
		return 0;
	}

	int tcpnodelay(int fd)
	{
		t_fd* p = _mapfd.get(fd);
		if (!p || (fd_tcp != p->fdtype && fd_tcpout != p->fdtype))
			return -1;
		int bNodelay = 1;
		return setsockopt(p->sysfd, IPPROTO_TCP, TCP_NODELAY, (char*)&bNodelay, sizeof(bNodelay));
	}

	int create_udp(const struct sockaddr* addr, socklen_t addrlen, int ipv6only = 0) // create udp and bind if addr is not null , return fd
	{
		int kfd = nextfd();
		if (kfd < 0)
			return -1;
		int sysfd = socket(addr ? addr->sa_family : AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);
		if (sysfd < 0)
			return -1;

		int opt = 1; // IPV6_V6ONLY
		if (ipv6only && addr && addr->sa_family == AF_INET6) {
			if (-1 == setsockopt(sysfd, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&opt, sizeof(opt))) {
				close(sysfd);
				return -1;
			}
		}
		int sndbufsize = 512 * 1024;
		setsockopt(sysfd, SOL_SOCKET, SO_RCVBUF, (char*)&sndbufsize, (socklen_t)sizeof(sndbufsize));

		if (addr && addrlen) {
			opt = 1; // reuse
			if (-1 == setsockopt(sysfd, SOL_SOCKET, SO_REUSEADDR, (const void*)&opt, sizeof(opt)) ||
				bind(sysfd, addr, addrlen) < 0) {
				close(sysfd);
				return -1;
			}
		}
		setfd(kfd, fd_udp, sysfd);
		return kfd;
	}

	inline int recvfrom_(int fd, void* buf, size_t len, int flags, struct sockaddr* from, socklen_t* fromlen)
	{
		t_fd* p = _mapfd.get(fd);
		if (!p || fd_udp != p->fdtype)
			return -1;
		return recvfrom(p->sysfd, buf, len, MSG_DONTWAIT, from, fromlen);
	}

	inline int sendto_(int fd, const void* buf, size_t len, const struct sockaddr* dest_addr, socklen_t addrlen)
	{
		t_fd* p = _mapfd.get(fd);
		if (!p || fd_udp != p->fdtype)
			return -1;
		return sendto(p->sysfd, buf, len, MSG_DONTWAIT, dest_addr, addrlen);
	}

	int setsendbuf(int fd, int n)
	{
		int nval = n;
		t_fd* p = _mapfd.get(fd);
		if (!p)
			return -1;
		if (-1 == setsockopt(p->sysfd, SOL_SOCKET, SO_SNDBUF, (char*)&nval, (socklen_t)sizeof(nval)))
			return -1;
		return nval;
	}

	int setrecvbuf(int fd, int n)
	{
		int nval = n;
		t_fd* p = _mapfd.get(fd);
		if (!p)
			return -1;
		if (-1 == setsockopt(p->sysfd, SOL_SOCKET, SO_RCVBUF, (char*)&nval, (socklen_t)sizeof(nval)))
			return -1;
		return nval;
	}

	int getsysfd(int fd)
	{
		t_fd* p = _mapfd.get(fd);
		if (!p)
			return -1;
		return p->sysfd;
	}

	int getbufsize(int fd, int op)
	{
		int nlen = 4, nval = 0;
		t_fd* p = _mapfd.get(fd);
		if (!p)
			return -1;
		if (getsockopt(p->sysfd, SOL_SOCKET, op, (char*)&nval, (socklen_t*)&nlen) < 0)
			return -1;
		return nval;
	}
};
