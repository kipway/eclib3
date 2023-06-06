/*
* @file ec_netepoll.h
* base net server class use epoll for linux
* 
* @author jiangyong
* @update
	2023-6-6  增加可持续fd, update closefd() 可选通知
    2023-5-21 update for download big http file
    2023-5.13 remove ec::memory
    2023-2-09 first version

* eclib 3.0 Copyright (c) 2017-2023, kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/
#pragma once
#include "ec_aiolinux.h"
#include "ec_aiosession.h"
#include "ec_memory.h"
#include "ec_log.h"
#include "ec_map.h"
#include "ec_vector.hpp"
#ifndef SIZE_MAX_FD
#define SIZE_MAX_FD  16384 //最大fd连接数
#endif

namespace ec {
	namespace aio {
		using NETIO = netio_linux;
		class serverepoll_
		{
		protected:
			ec::ilog* _plog;

			int _fdepoll;
			NETIO _net;

		private:
			int _lastwaiterr;
			struct epoll_event _fdevts[EC_AIO_EVTS];
		protected:
			char _recvtmp[EC_AIO_READONCE_SIZE];

		protected:
			/**
			 * @brief before disconnect call
			 * @param kfd keyfd
			*/
			virtual void onDisconnect(int kfd) = 0;

			/**
			 * @brief after disconnect call
			 * @param kfd keyfd
			*/
			virtual void onDisconnected(int kfd) = 0;

			/**
			 * @brief received data
			 * @param kfd keyfd
			 * @param pdata Received data
			 * @param size  Received data size
			 * @return 0:OK; -1:error
			*/
			virtual int onReceived(int kfd, const void* pdata, size_t size) = 0;

			/**
			 * @brief received UDP data
			 * @param kfd keyfd
			 * @param pdata Received data
			 * @param size  Received data size
			 * @param addrfrom peer address
			 * @param size of peer address
			 * @return 0:OK; -1:error
			*/
			virtual int onReceivedFrom(int kfd, const void* pdata, size_t size, const struct sockaddr* addrfrom, int addrlen) {
				return 0;
			}

			/**
			 * @brief TCP Accept
			 * @param kfd keyfd
			 * @param sip peer ip
			 * @param port peer port
			 * @param kfd_listen keyfd of listened
			*/
			virtual void onAccept(int kfd, const char* sip, uint16_t port, int kfd_listen) = 0;

			/**
			 * @brief size can receive ,use for flowctrl
			 * @param pss
			 * @return >0 size can receive;  0: pause read
			*/
			virtual size_t  sizeCanRecv(psession pss) {
				return EC_AIO_READONCE_SIZE;
			};

			/**
			* @brief TCP asyn connect out success
			* @param kfd keyfd
			* @remark will call onDisconnect and onDisconnected if failed.
			*/
			virtual void onTcpOutConnected(int kfd) {
			}

			/**
			 * @brief get the session of kfd
			 * @param kfd keyfd
			 * @return nullptr or psession
			*/
			virtual psession getSession(int kfd) = 0;
		protected:
			inline int setsendbuf(int fd, int n)
			{
				return _net.setsendbuf(fd, n);
			}

			inline int setrecvbuf(int fd, int n)
			{
				return _net.setrecvbuf(fd, n);
			}

			inline int connect_asyn(const struct sockaddr* addr, socklen_t addrlen) {
				return _net.connect_asyn(addr, addrlen);
			}

			/**
			 * @brief shutdown and close a kfd
			 * @param kfd  keyfd
			 * @return
			*/
			int close_(int kfd) //shutdown and close kfd
			{
				int ftype = _net.getfdtype(kfd);
				if (ftype < 0)
					return -1;
				_plog->add(CLOG_DEFAULT_DBG, "close_ fd(%d), fdtype = %d", kfd, ftype);
				_net.close_(kfd);
				return 0;
			}

			int epoll_add_tcpout(int kfd)
			{
				struct epoll_event evt;
				memset(&evt, 0, sizeof(evt));
				evt.events = EPOLLIN | EPOLLOUT | EPOLLERR;
				evt.data.fd = kfd;
				int nerr = 0;
				if (0 != (nerr = _net.epoll_ctl_(_fdepoll, EPOLL_CTL_ADD, kfd, &evt))) {
					_plog->add(CLOG_DEFAULT_ERR, "EPOLL_CTL_ADD failed. error = %d", nerr);
					_net.close_(kfd);
					return -1;
				}
				return 0;
			}

		public:
			serverepoll_(ec::ilog* plog) : _plog(plog), _fdepoll(-1), _lastwaiterr(-100)
			{
			}
			virtual ~serverepoll_() {

			}
			inline void SetFdFile(const char* sfile) {
				_net.SetFdFile(sfile);
			}
			//create epoll, return 0:ok; -1:error
			int open()
			{
				if (_fdepoll >= 0)
					return 0;
				_fdepoll = _net.epoll_create_(1);
				if (_fdepoll < 0) {
					_plog->add(CLOG_DEFAULT_ERR, "epoll_create_ failed.");
					return -1;
				}
				_plog->add(CLOG_DEFAULT_MSG, "epoll_create_ success.");
				return 0;
			}

			/**
			 * @brief 关闭所有连接和epoll handel
			 * @return 0;
			 * @remark 用于退出时调用，不会通知应用层连接断开，应用层需自己释放和连接相关的资源。
			*/
			void close()
			{
				ec::vector<int> fds;
				fds.reserve(1024);
				_net.getall(fds);

				int fdtype = 0;
				for (auto& i : fds) {
					fdtype = _net.getfdtype(i);
					if (fdtype >= 0 && fdtype != _net.fd_epoll) { //fd除epoll外全部关闭
						_net.epoll_ctl_(_fdepoll, EPOLL_CTL_DEL, i, nullptr);
						_net.close_(i);
						_plog->add(CLOG_DEFAULT_DBG, "close fd(%d), fdtype = %d @serverepoll_::close", i, fdtype);
					}
				}
				if (_fdepoll >= 0)
					_net.close_(_fdepoll);
				_fdepoll = -1;
			}

			/**
			 * @brief tcp listen
			 * @param port port
			 * @param sip  ipv4 or ipv6, nullptr or empty is ipv4 0.0.0.0
			 * @return virtual fd; -1:failed
			*/
			int tcplisten(uint16_t port, const char* sip = nullptr, int ipv6only = 0)
			{
				ec::net::socketaddr netaddr;
				if (netaddr.set(port, sip) < 0)
					return -1;
				int addrlen = 0;
				struct sockaddr* paddr = netaddr.getsockaddr(&addrlen);
				if (!paddr)
					return -1;
				int fdl = _net.bind_listen(paddr, addrlen, ipv6only);
				if (fdl < 0) {
					_plog->add(CLOG_DEFAULT_ERR, "bind listen tcp://%s:%u failed.", netaddr.viewip(), port);
					return -1;
				}
				_plog->add(CLOG_DEFAULT_MSG, "fd(%d) bind listen tcp://%s:%u success.", fdl, netaddr.viewip(), port);

				struct epoll_event evt;
				memset(&evt, 0, sizeof(evt));
				evt.events = EPOLLIN | EPOLLERR;
				evt.data.fd = fdl;

				int nerr = 0;
				if (0 != (nerr = _net.epoll_ctl_(_fdepoll, EPOLL_CTL_ADD, fdl, &evt))) {
					_plog->add(CLOG_DEFAULT_ERR, "epoll_ctrl_ failed. error = %d", nerr);
					_net.close_(fdl);
					return -1;
				}
				return fdl;
			}

			int udplisten(uint16_t port, const char* sip = nullptr, int ipv6only = 0) // return udp server fd, -1 error
			{
				ec::net::socketaddr netaddr;
				if (netaddr.set(port, sip) < 0)
					return -1;
				int addrlen = 0;
				struct sockaddr* paddr = netaddr.getsockaddr(&addrlen);
				if (!paddr)
					return -1;
				int fdl = _net.create_udp(paddr, addrlen, ipv6only);

				if (fdl < 0) {
					_plog->add(CLOG_DEFAULT_ERR, "bind udp://%s:%u failed.", netaddr.viewip(), port);
					return -1;
				}
				_plog->add(CLOG_DEFAULT_MSG, "fd(%d) bind udp://%s:%u success.", fdl, netaddr.viewip(), port);

				struct epoll_event evt;
				evt.events = EPOLLIN | EPOLLERR;
				evt.data.fd = fdl;

				int nerr = 0;
				if (0 != (nerr = _net.epoll_ctl_(_fdepoll, EPOLL_CTL_ADD, fdl, &evt))) {
					_plog->add(CLOG_DEFAULT_ERR, "epoll_ctrl_ failed. error = %d", nerr);
					_net.close_(fdl);
					return -1;
				}
				return fdl;
			}

			void runtime_(int waitmsec)
			{
				if (-1 == _fdepoll)
					return;

				int64_t curmstime = ec::mstime();
				if (llabs(curmstime - _lastmstime) >= 5) { //5毫秒处理一次接收流控
					dorecvflowctrl();
					_lastmstime = curmstime;
				}

				int nret = _net.epoll_wait_(_fdepoll, _fdevts, static_cast<int>(sizeof(_fdevts) / sizeof(struct epoll_event)), waitmsec);
				if (nret < 0) {
					if (_lastwaiterr != nret)
						_plog->add(CLOG_DEFAULT_ERR, "epoll_wait_ return %d", nret);
					_lastwaiterr = nret;
					return;
				}
				for (auto i = 0; i < nret; i++)
					onevent(_fdevts[i]);
			}

			/**
			 * @brief 设置可发送事件
			 * @param kfd keyfd
			*/
			void sendtrigger(int kfd)
			{
				psession pss = getSession(kfd);
				if (!pss)
					return;
				triger_evt(pss);
			}

			void udp_trigger(int kfd, bool bsend)
			{
				if(bsend)
					udp_sendto(kfd);
				struct epoll_event evtmod;
				memset(&evtmod, 0, sizeof(evtmod));
				evtmod.events = bsend ? EPOLLIN | EPOLLOUT | EPOLLERR : EPOLLIN | EPOLLERR;
				evtmod.data.fd = kfd;
				int nerr = 0;
				if (0 != (nerr = _net.epoll_ctl_(_fdepoll, EPOLL_CTL_MOD, kfd, &evtmod)))
					_plog->add(CLOG_DEFAULT_ERR, "udp epoll_ctrl_ EPOLL_CTL_MOD failed @onevent. fd = %d,  error = %d", kfd, nerr);
			}

			/**
			 * @brief 发送，直到系统缓冲满或者发送应用缓冲发送完成。
			  * @param kfd keyfd
			 * @return  >=0: post bytes;  -1:failed, close fd and call onDisconnected(kfd)
			*/
			int postsend(int kfd)
			{
				int ns = 0;
				psession pss = getSession(kfd);
				if (!pss)
					return -1;
				if ((ns = sendbuf(pss)) < 0) {
					closefd(kfd);
					return -1;
				}
				triger_evt(pss);
				return ns;
			}

			/**
			 * @brief 主动关闭连接，会产生onClosed调用
			 * @param kfd keyfd
			*/
			void closefd(int kfd, bool bnotify = true)
			{
				if (bnotify)
					onDisconnect(kfd);
				// Since Linux 2.6.9, event can be specified as NULL when using EPOLL_CTL_DEL
				_net.epoll_ctl_(_fdepoll, EPOLL_CTL_DEL, kfd, nullptr);
				_net.close_(kfd);
				onDisconnected(kfd);
			}

			size_t size_fds()
			{
				return _net.size();
			}

			inline int getbufsize(int fd, int op)
			{
				return _net.getbufsize(fd, op);
			}
		private:
			int64_t _lastmstime = 0;//上次扫描可发送的时间，单位GMT毫秒
			void dorecvflowctrl()//接收流控
			{
				for (auto& i : _net.getmap()) {
					if (i.fdtype != _net.fd_listen && i.fdtype != _net.fd_epoll && i.fdtype != _net.fd_udp) {
						triger_evt(getSession(i.kfd));
					}
				}
			}

			void triger_evt(psession pss)
			{
				if (!pss)
					return;
				struct epoll_event evtmod;
				memset(&evtmod, 0, sizeof(evtmod));
				evtmod.events = EPOLLERR;

				if (sizeCanRecv(pss) > 0 && !pss->_readpause)
					evtmod.events |= EPOLLIN;
				if (!pss->_sndbuf.empty() || pss->_status == EC_AIO_FD_CONNECTING || pss->hasSendJob())
					evtmod.events |= EPOLLOUT;
				evtmod.data.fd = pss->_fd;

				int nerr = 0;
				if (0 != (nerr = _net.epoll_ctl_(_fdepoll, EPOLL_CTL_MOD, pss->_fd, &evtmod)))
					_plog->add(CLOG_DEFAULT_ERR, "epoll_ctrl_ EPOLL_CTL_MOD failed @onevent. fd = %d,  error = %d", pss->_fd, nerr);
			}

			void udp_sendto(int kfd)
			{
				psession pss = getSession(kfd);
				if (!pss) {
					udp_trigger(kfd, 0);
					return;
				}
				udb_buffer_* pfrms = pss->getudpsndbuffer();
				if (!pfrms || pfrms->empty()) {
					udp_trigger(kfd, 0);
					return;
				}
				int numsnd = 0, nbytes = 0;
				do {
					auto& frm = pfrms->front();
					if (!frm.empty()) {
						if (_net.sendto_(kfd, frm.data(), frm.size(), frm.getnetaddr(), frm.netaddrlen()) < 0) {
							if (EAGAIN != errno) {
								ec::net::socketaddr peeraddr;
								peeraddr.set(frm.getnetaddr(), frm.netaddrlen());
								_plog->add(CLOG_DEFAULT_DBG, "fd(%d) sento %s:%u error %d.", kfd,
									peeraddr.viewip(), peeraddr.port(), errno);
							}
							break;
						}
						if (_plog->getlevel() == CLOG_DEFAULT_ALL) {
							ec::net::socketaddr peeraddr;
							peeraddr.set(frm.getnetaddr(), frm.netaddrlen());
							_plog->add(CLOG_DEFAULT_ALL, "fd(%d) sento %s:%u %zu bytes.", kfd,
								peeraddr.viewip(), peeraddr.port(), frm.size());
						}
						nbytes += (int)frm.size();
					}
					pfrms->pop();
					numsnd++;
				} while (!pfrms->empty() && numsnd < 16 && nbytes < 1024 * 32);
			}

			char udpbuf_[1024 * 64] = { 0 };
			void onudpevent(struct epoll_event& evt)
			{
				if (evt.events & EPOLLIN) {
					int nr = -1, ndo = 128;
					socklen_t* paddrlen = nullptr;
					ec::net::socketaddr addr;
					struct sockaddr* paddr = addr.getbuffer(&paddrlen);
					do {
						nr = _net.recvfrom_(evt.data.fd, udpbuf_, (int)sizeof(udpbuf_), 0, paddr, paddrlen);
						if (nr > 0) {
							if (_plog->getlevel() == CLOG_DEFAULT_ALL) {
								_plog->add(CLOG_DEFAULT_ALL, "fd(%d) recvfrom %s:%u %d bytes.", evt.data.fd,
									addr.viewip(), addr.port(), nr);
							}
							if (onReceivedFrom(evt.data.fd, udpbuf_, nr, paddr, *paddrlen) < 0)
								break;
						}
						else if (nr < 0 && EAGAIN != errno) {
							_plog->add(CLOG_DEFAULT_ALL, "fd(%d) recvfrom failed. error %d", evt.data.fd, errno);
						}
						--ndo;
					} while (nr > 0 && ndo > 0);
				}
				if (evt.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
					_plog->add(CLOG_DEFAULT_DBG, "udp fd(%d)  error events %08XH", evt.data.fd, evt.events);
					closefd(evt.data.fd);
					return;
				}
				if (evt.events & EPOLLOUT) {
					udp_sendto(evt.data.fd);
				}
			}

			void onevent(struct epoll_event& evt)
			{
				int nfdtype = _net.getfdtype(evt.data.fd);
				if (nfdtype == _net.fd_udp) {
					onudpevent(evt);
					return;
				}
				if ((evt.events & EPOLLIN) && _net.fd_listen == nfdtype) {
					_plog->add(CLOG_DEFAULT_ALL, "listen fd(%d)  EPOLLIN, events %08XH", evt.data.fd, evt.events);
					ec::net::socketaddr clientaddr;
					socklen_t* paddrlen = nullptr;
					struct sockaddr* paddr = clientaddr.getbuffer(&paddrlen);
					int fdc = _net.accept_(evt.data.fd, paddr, paddrlen);
					if (fdc >= 0) {
						int nerr = 0;
						struct epoll_event ev;
						memset(&ev, 0, sizeof(ev));
						ev.events = EPOLLIN | EPOLLOUT | EPOLLERR;
						ev.data.fd = fdc;
						if (0 != (nerr = _net.epoll_ctl_(_fdepoll, EPOLL_CTL_ADD, fdc, &ev))) {
							_plog->add(CLOG_DEFAULT_ERR, "epoll_ctrl_ EPOLL_CTL_ADD failed @onconnect_in. fd = %d, error = %d", fdc, nerr);
							_net.close_(fdc);
						}
						else {
							uint16_t uport = 0;
							char sip[48] = { 0 };
							clientaddr.get(uport, sip, sizeof(sip));
							onAccept(fdc, sip, uport, evt.data.fd);
						}
					}
					else
						_plog->add(CLOG_DEFAULT_ERR, "accept failed. listen fd = %d", evt.data.fd);
					return;
				}
				if (evt.events & EPOLLIN) {
					int nr = -1;
					psession pss = getSession(evt.data.fd);
					if (pss && !pss->_readpause) {
						size_t zr = sizeCanRecv(pss);
						if (zr > 0) {
							if (zr > sizeof(_recvtmp))
								zr = sizeof(_recvtmp);
							nr = _net.recv_(evt.data.fd, _recvtmp, zr, 0);
							if (!nr || (nr < 0 && EAGAIN != _net.geterrno() && EWOULDBLOCK != _net.geterrno())) {
								closefd(evt.data.fd);
								return;
							}
							_plog->add(CLOG_DEFAULT_ALL, "fd(%d) received %d bytes", evt.data.fd, nr);
							if (onReceived(evt.data.fd, _recvtmp, nr) < 0) {
								closefd(evt.data.fd);
								return;
							}
						}
					}
				}
				if (evt.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
					_plog->add(CLOG_DEFAULT_DBG, "fd(%d)  error events %08XH", evt.data.fd, evt.events);
					closefd(evt.data.fd);
					return;
				}
				if (evt.events & EPOLLOUT) {
					_plog->add(CLOG_DEFAULT_ALL, "fd(%d)  EPOLLOUT, events %08XH", evt.data.fd, evt.events);
					if (onepollout(evt.data.fd) < 0)
						return;
				}
				if (NETIO::fd_listen != nfdtype && NETIO::fd_epoll != nfdtype)
					sendtrigger(evt.data.fd);
			}

			int onepollout(int kfd)
			{
				psession pss = getSession(kfd);
				if (!pss)
					return -1;
				int nfdtype = _net.getfdtype(kfd);
				if (_net.fd_tcpout == nfdtype) { // asyn connect out
					if (pss->_status == EC_AIO_FD_CONNECTING) {
						int serr = 0;
						socklen_t serrlen = sizeof(serr);
						getsockopt(_net.getsysfd(kfd), SOL_SOCKET, SO_ERROR, (void*)&serr, &serrlen);
						if (serr) {
							closefd(kfd);
							return -1;
						}
						pss->_status = EC_AIO_FD_CONNECTED;
						onTcpOutConnected(kfd);
						return 0;
					}
				}
				if (sendbuf(pss) < 0) {
					closefd(kfd);
					return -1;
				}
				if (pss->_sndbuf.empty()) {
					if (!pss->onSendCompleted()) {
						closefd(kfd);
						return -1;
					}
				}
				return 0;
			}

			/**
			 * @brief 发送,直到系统缓冲满或者发送应用缓冲发送完成。
			 * @param pss
			 * @param pdata
			 * @param size
			 * @return 返回发送的总字节数, -1:error
			*/
			int sendbuf(psession pss, const void* pdata = nullptr, size_t size = 0)
			{
				int ns = 0, fd = pss->_fd, nsnd = 0;
				if (pdata && size)
					pss->_sndbuf.append((const uint8_t*)pdata, size);

				const void* pd = nullptr;
				size_t zlen = 0;

				pd = pss->_sndbuf.get(zlen);
				while (pd && zlen) {
					ns = _net.send_(fd, pd, (int)(zlen), 0);
					_plog->add(CLOG_DEFAULT_ALL, "sendbuf fd(%d) size %d", fd, ns);
					if (ns < 0) {
						int nerr = _net.geterrno();
						if (nerr != EAGAIN)
							_plog->add(CLOG_DEFAULT_ERR, "fd(%d) sendbuf syserr %d", fd, nerr);
						else
							ns = 0;
						break;
					}
					else if (!ns)
						break;
					nsnd += ns;
					pss->_sndbuf.freesize(ns);
					if (ns < (int)(zlen))
						break;
					pd = pss->_sndbuf.get(zlen);
				}
				return ns < 0 ? -1 : nsnd;
			}
		private:

		};
	}//namespace sio
}//namespace ec
