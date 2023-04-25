/*!
\file netio_win32.h

windows 版的网络IO, wepoll模型

\author jiangyong

\date_create  2021-6-13

\update 2023-2-1  增加TCP ipv6支持
\update 2022-2-12 删除sctp,fd改为增长至增加INT32_MAX然后回到1
\update 2021-11-7 增加 setkeepalive 和 tcp模拟sctp
\update 2021-6-17 增加 int getfdtype(int fd)
\update 2021-6-19 init中增加发送和接收缓冲参数设置.
\fix    2021-6-20 修正缓冲大小单位错误.
*/
#pragma once
#include <winsock2.h>
#include <string>
#ifdef USE_AFUNIX_WIN
#include <afunix.h>
#endif
#include "./wepoll/wepoll.h"
#include "ec_map.h"
#include "ec_jsonx.h"
#include "ec_netio.h"

#ifndef SIZE_MAX_FD
#define SIZE_MAX_FD  16384 //最大fd连接数
#endif

#ifndef socklen_t
typedef int socklen_t;
#endif

class netio_win32
{
public:
	enum fdtype {
		fd_tcp = 0,
		fd_tcpout,
		fd_listen,
		fd_epoll,
		fd_udp
	};
private:
	struct t_fd
	{
		int  kfd; //>=0;  <0 error; key
		int  fdtype;
		union {
			SOCKET sock;
			HANDLE ephnd;
		};
	};

	struct keq_fd
	{
		bool operator()(int key, const t_fd& val)
		{
			return key == val.kfd;
		}
	};

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

	void setfd(int kfd, fdtype type, SOCKET sysfd)
	{
		t_fd t;
		t.kfd = kfd;
		t.fdtype = type;
		t.sock = sysfd;
		_mapfd.set(kfd, t);
	}

	bool socketfull()
	{
		return _mapfd.size() >= SIZE_MAX_FD;
	}

public:
	inline int geterrno()
	{
		int nerr = 0;
		DWORD dwerr = GetLastError();//把windows的错误码转换为linux的错误码
		switch (dwerr) {
		case WSAEWOULDBLOCK:
			nerr = EAGAIN;
			break;
		case WSAEBADF:
		case WSAENOTSOCK:
			nerr = EBADF;
			break;
		case WSAEMFILE:
			nerr = EMFILE;
			break;
			// todo 没有转换完
		default:
			nerr = EPIPE;
			break;
		}
		return nerr;
	}

	ec::hashmap<int, t_fd, keq_fd>& getmap() {
		return _mapfd;
	}
public:
	netio_win32() : _nextfd(0), _sizercvbuf(128), _sizesndbuf(128), _mapfd(1024)
	{
	}
	~netio_win32() {
		std::vector<HANDLE> vepolls;
		for (auto& i : _mapfd) {
			if (fd_epoll == i.fdtype)
				vepolls.push_back(i.ephnd);
			else {
				if (fd_tcp == i.fdtype || fd_tcpout == i.fdtype || fd_udp == i.fdtype)
					shutdown(i.sock, SD_BOTH);
				closesocket(i.sock);
			}
		}
		_mapfd.clear();
		for (auto& i : vepolls)
			epoll_close(i);
	}
	inline size_t size() {
		return _mapfd.size();
	}
	bool init(const char* env, std::string& errout)//env={"rcvbufsize":256, "sndbufsize":8192}
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

	int epoll_create_(int size)
	{
		int fd = nextfd();
		if (fd < 0)
			return -1;

		t_fd t;
		t.kfd = fd;
		t.fdtype = fd_epoll;
		t.ephnd = epoll_create(1);
		if (!t.ephnd)
			return -1;
		_mapfd.set(fd, t);
		return fd;
	}

	int epoll_ctl_(int epfd, int op, int fd, struct epoll_event* event)
	{
		t_fd* pepoll = _mapfd.get(epfd);
		if (!pepoll || pepoll->fdtype != fd_epoll)
			return -1;
		t_fd* pfd = _mapfd.get(fd);
		if (!pfd)
			return -1;
		return epoll_ctl(pepoll->ephnd, op, pfd->sock, event);
	}

	int epoll_wait_(int epfd, struct epoll_event* events, int maxevents, int timeout)
	{
		t_fd* pepoll = _mapfd.get(epfd);
		if (!pepoll || pepoll->fdtype != fd_epoll)
			return -1;
		return epoll_wait(pepoll->ephnd, events, maxevents, timeout);
	}

	void close_(int fd) //shutdown and close one fd
	{
		t_fd* p = _mapfd.get(fd);
		if (!p)
			return;
		if (fd_epoll == p->fdtype) {
			epoll_close(p->ephnd);
		}
		else {
			if (fd_tcp == p->fdtype || fd_tcpout == p->fdtype)
				shutdown(p->sock, SD_BOTH);
			closesocket(p->sock);
		}
		_mapfd.erase(fd);
	}

	int connect_asyn(const struct sockaddr* addr, socklen_t addrlen) //connect nobloack, return fd
	{
		t_fd t;
		t.kfd = nextfd();
		if (t.kfd < 0)
			return -1;
		t.sock = socket(addr->sa_family, SOCK_STREAM, IPPROTO_TCP);
		if (INVALID_SOCKET == t.sock)
			return -1;
		long ul = 1;

		if (SOCKET_ERROR == ioctlsocket(t.sock, FIONBIO, (unsigned long*)&ul)) {
			closesocket(t.sock);
			return -1;
		}
		connect(t.sock, addr, addrlen);

		ec::net::setrecvbuf(t.sock, _sizercvbuf * 1024);
		ec::net::setsendbuf(t.sock, _sizesndbuf * 1024);

		t.fdtype = fd_tcpout;
		_mapfd.set(t.kfd, t);
		return t.kfd;
	}

	int bind_listen(const struct sockaddr* addr, socklen_t addrlen, int ipv6only = 0) // bind and listen, return fd
	{
		t_fd t;
		t.kfd = nextfd();
		if (t.kfd < 0)
			return -1;
		t.fdtype = fd_listen;
		t.sock = WSASocket(addr->sa_family, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
		if (INVALID_SOCKET == t.sock)
			return -1;
		int opt = 1;
		u_long iMode = 1;

		if (ipv6only && AF_INET6 == addr->sa_family) {
			if (SOCKET_ERROR == setsockopt(t.sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&opt, sizeof(opt))) {
				closesocket(t.sock);
				return -1;
			}
		}
		if (SOCKET_ERROR == setsockopt(t.sock, SOL_SOCKET, SO_REUSEADDR,
			(const char*)&opt, sizeof(opt)) || SOCKET_ERROR == ioctlsocket(t.sock, FIONBIO, &iMode)) {
			closesocket(t.sock);
			return -1;
		}
		if (bind(t.sock, addr, addrlen) == SOCKET_ERROR) {
			closesocket(t.sock);
			return -1;
		}
		if (listen(t.sock, SOMAXCONN) == SOCKET_ERROR) {
			closesocket(t.sock);
			return -1;
		}
		_mapfd.set(t.kfd, t);
		return t.kfd;
	}

#if !defined _WIN32 || defined USE_AFUNIX_WIN
	int bind_listen_afunix(const char* spath) // bind and listen af_unix return fd
	{
		int kfd = nextfd();
		if (kfd < 0)
			return -1;

		SOCKET sysfd = INVALID_SOCKET;
		if ((sysfd = socket(AF_UNIX, SOCK_STREAM, 0)) == INVALID_SOCKET)
			return -1;

		struct sockaddr_un Addr;
		memset(&Addr, 0, sizeof(Addr));
		Addr.sun_family = AF_UNIX;
		snprintf(Addr.sun_path, sizeof(Addr.sun_path), "%s", spath);
		unlink(Addr.sun_path);
		if (bind(sysfd, (const sockaddr*)&Addr, sizeof(Addr)) == SOCKET_ERROR) {
			fprintf(stderr, "ERR:af_unix [%s] bind failed with error %d\n", spath, ::WSAGetLastError());
			::closesocket(sysfd);
			return -1;
		}
		if (listen(sysfd, SOMAXCONN) == SOCKET_ERROR) {
			fprintf(stderr, "ERR: af_unix [%s]  listen failed with error %d\n", spath, ::WSAGetLastError());
			::closesocket(sysfd);
			return -1;
		}
		setfd(kfd, fd_listen, sysfd);
		return kfd;
	}
#endif

	inline int accept_(int fd, struct sockaddr* addr, socklen_t* addrlen) // return fd
	{
		t_fd* p = _mapfd.get(fd);
		if (!p || fd_listen != p->fdtype)
			return -1;

		SOCKET	sAccept;
		if ((sAccept = ::accept(p->sock, addr, addrlen)) == INVALID_SOCKET)
			return -1;

		u_long iMode = 1;
		if (SOCKET_ERROR == ioctlsocket(sAccept, FIONBIO, &iMode)) {
			::closesocket(sAccept);
			return -1;
		}

		t_fd t;
		t.kfd = nextfd();
		if (t.kfd < 0) {
			::closesocket(sAccept);
			return -1;
		}
		t.fdtype = fd_tcp;
		t.sock = sAccept;
		ec::net::setrecvbuf(t.sock, _sizercvbuf * 1024);
		ec::net::setsendbuf(t.sock, _sizesndbuf * 1024);
		_mapfd.set(t.kfd, t);
		return t.kfd;
	}

	int recv_(int fd, void* buf, size_t len, int flags)
	{
		t_fd* p = _mapfd.get(fd);
		if (!p || (fd_tcp != p->fdtype && fd_tcpout != p->fdtype))
			return -1;
		return recv(p->sock, (char*)buf, (int)len, 0);
	}

	int send_(int fd, const void* buf, size_t len, int flags)
	{
		t_fd* p = _mapfd.get(fd);
		if (!p || (fd_tcp != p->fdtype && fd_tcpout != p->fdtype))
			return -1;
		return send(p->sock, (const char*)buf, (int)len, 0);
	}

	int shutdown_(int fd, int how)
	{
		t_fd* p = _mapfd.get(fd);
		if (!p || (fd_tcp != p->fdtype && fd_tcpout != p->fdtype))
			return -1;
		return shutdown(p->sock, how);
	}

	void getall(std::vector<int>& fds)
	{
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

	int setkeepalive(int fd, bool bfast = false) // return 0:ok; -1:error
	{
		t_fd* p = _mapfd.get(fd);
		if (!p || (fd_tcp != p->fdtype && fd_tcpout != p->fdtype))
			return -1;
		BOOL bKeepAlive = 1;
		int nRet = setsockopt(p->sock, SOL_SOCKET, SO_KEEPALIVE,
			(char*)&bKeepAlive, sizeof(bKeepAlive));
		if (nRet == SOCKET_ERROR)
			return -1;
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

		nRet = WSAIoctl(p->sock, SIO_KEEPALIVE_VALS, &alive_in, sizeof(alive_in),
			&alive_out, sizeof(alive_out), &ulBytesReturn, NULL, NULL);
		if (nRet == SOCKET_ERROR)
			return -1;
		return 0;
	}

	int tcpnodelay(int fd)
	{
		t_fd* p = _mapfd.get(fd);
		if (!p || (fd_tcp != p->fdtype && fd_tcpout != p->fdtype))
			return -1;
		int bNodelay = 1;
		return setsockopt(p->sock, IPPROTO_TCP, TCP_NODELAY, (char*)&bNodelay, sizeof(bNodelay));
	}

	int create_udp(const struct sockaddr* addr, socklen_t addrlen, int ipv6only = 0) // create udp and bind if addr is not null , return fd
	{
		t_fd t;
		t.kfd = nextfd();
		if (t.kfd < 0)
			return -1;
		t.fdtype = fd_udp;
		t.sock = socket(addr ? addr->sa_family : AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (INVALID_SOCKET == t.sock)
			return -1;
		int opt = 1;
		if (ipv6only && addr && AF_INET6 == addr->sa_family) {
			if (SOCKET_ERROR == setsockopt(t.sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&opt, sizeof(opt))) {
				closesocket(t.sock);
				return -1;
			}
		}
		u_long iMode = 1;
		if (SOCKET_ERROR == ioctlsocket(t.sock, FIONBIO, &iMode)) {
			closesocket(t.sock);
			return -1;
		}
		if (addr && addrlen) {
			if (SOCKET_ERROR == setsockopt(t.sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt))
				|| SOCKET_ERROR == bind(t.sock, addr, addrlen)) {
				closesocket(t.sock);
				return -1;
			}
		}
		_mapfd.set(t.kfd, t);
		return t.kfd;
	}

	inline int recvfrom_(int fd, void* buf, size_t len, int flags, struct sockaddr* from, socklen_t* fromlen)
	{
		t_fd* p = _mapfd.get(fd);
		if (!p || fd_udp != p->fdtype)
			return -1;
		return recvfrom(p->sock, (char*)buf, (int)len, 0, from, fromlen);
	}

	inline int sendto_(int fd, const void* buf, size_t len, const struct sockaddr* dest_addr, socklen_t addrlen)
	{
		t_fd* p = _mapfd.get(fd);
		if (!p || fd_udp != p->fdtype)
			return -1;
		return sendto(p->sock, (const char*)buf, (int)len, 0, dest_addr, addrlen);
	}

	int setsendbuf(int fd, int n)
	{
		int nval = n;
		t_fd* p = _mapfd.get(fd);
		if (!p)
			return -1;
		if (-1 == setsockopt(p->sock, SOL_SOCKET, SO_SNDBUF, (char*)&nval, (socklen_t)sizeof(nval)))
			return -1;
		return nval;
	}

	int setrecvbuf(int fd, int n)
	{
		int nval = n;
		t_fd* p = _mapfd.get(fd);
		if (!p)
			return -1;
		if (-1 == setsockopt(p->sock, SOL_SOCKET, SO_RCVBUF, (char*)&nval, (socklen_t)sizeof(nval)))
			return -1;
		return nval;
	}

	int getsysfd(int fd)
	{
		t_fd* p = _mapfd.get(fd);
		if (!p)
			return -1;
		return (int)p->sock;
	}
};