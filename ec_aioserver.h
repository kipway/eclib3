/*!
\file ec_aioserver.h

eclib3 AIO net server
Asynchronous service using epoll

\author  jiangyong
*/

#pragma once
#ifdef _WIN32
#include "ec_aiowin32.h"
#else
#include "ec_aiolinux.h"
#endif

#include "ec_aiosession.h"

#if (0 != EC_AIOSRV_TLS)
	#include "ec_aiotls.h"
#endif

#if (0 != EC_AIOSRV_HTTP)
	#include "ec_aiohttp.h"
	#if (0 != EC_AIOSRV_TLS)
		#include "ec_aiohttps.h"
	#endif
#endif

#ifdef USE_AFUNIX_WIN
#include <afunix.h>
#endif
#ifndef EC_AIO_READONCE_SIZE
#define EC_AIO_READONCE_SIZE 32000
#endif
namespace ec {
	namespace aio {
#ifdef _WIN32
		template<class NETIO = netio_win32>
#else
		template<class NETIO = netio_linux>
#endif
		class server
		{
		protected:
			NETIO _net;
			ec::memory* _piomem;
			ec::ilog* _plog;
		protected:
			int _fdepoll;
			ec::blk_alloctor<> _sndbufblks;
			ec::hashmap<int, psession, kep_session, del_session > _mapsession;//会话连接
		private:
			int _lastwaiterr;
			struct epoll_event _fdevts[EC_AIO_EVTS];
		protected:
#if (0 != EC_AIOSRV_TLS)
			tls::srvca _ca;  // certificate
#endif
			char _recvtmp[EC_AIO_READONCE_SIZE];
		public:
			server(ec::memory* piomem, ec::ilog* plog)
				: _piomem(piomem)
				, _plog(plog)
				, _fdepoll(-1)
				, _sndbufblks(EC_AIO_SNDBUF_BLOCKSIZE - EC_ALLOCTOR_ALIGN, EC_AIO_SNDBUF_HEAPSIZE / EC_AIO_SNDBUF_BLOCKSIZE)
				, _lastwaiterr(-100)
			{
			}
			virtual ~server()
			{
				_mapsession.clear();
			}

#if (0 != EC_AIOSRV_TLS)
			bool initca(const char* filecert, const char* filerootcert, const char* fileprivatekey)
			{
				if (!_ca.InitCert(filecert, filerootcert, fileprivatekey)) {
					if (_plog)
						_plog->add(CLOG_DEFAULT_ERR, "Load certificate failed (%s,%s,%s)", filecert,
							filerootcert != nullptr ? filerootcert : "none", fileprivatekey);
					return false;
				}
				return true;
			}
#endif
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
			 * @brief tcp listen
			 * @param port port
			 * @param sip  ipv4 or ipv6, nullptr or empty is ipv4 0.0.0.0
			 * @return virtual fd; -1:failed
			*/
			int tcplisten(uint16_t port, const char* sip = nullptr, int ip6only = 0)
			{
				ec::net::socketaddr netaddr;
				if (netaddr.set(port, sip) < 0)
					return -1;
				int addrlen = 0;
				struct sockaddr* paddr = netaddr.getsockaddr(&addrlen);
				if (!paddr)
					return -1;
				int fdl = _net.bind_listen(paddr, addrlen, ip6only);
				if (fdl < 0) {
					_plog->add(CLOG_DEFAULT_ERR, "bind listen tcp://%s:%u failed.", netaddr.viewip(), port);
					return -1;
				}
				_plog->add(CLOG_DEFAULT_INF, "start listen tcp://%s:%u", netaddr.viewip(), port);
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

			int udplisten(uint16_t port, const char* sip = nullptr, int ip6only = 0) // return udp server fd, -1 error
			{
				ec::net::socketaddr netaddr;
				if (netaddr.set(port, sip) < 0)
					return -1;
				int addrlen = 0;
				struct sockaddr* paddr = netaddr.getsockaddr(&addrlen);
				if (!paddr)
					return -1;
				int fdl = _net.create_udp(paddr, addrlen, ip6only);

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

#if !defined _WIN32 || defined USE_AFUNIX_WIN
			int afunixlisten( const char* spath)
			{
				int fdl = _net.bind_listen_afunix(spath);
				if (fdl < 0) {
					_plog->add(CLOG_DEFAULT_ERR, "bind listen af_unix %s failed.", spath);
					return -1;
				}
				_plog->add(CLOG_DEFAULT_MSG, "bind listen af_unix %s success.", spath);

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
#endif
			int tcpconnect(uint16_t port, const char* sip)
			{
				ec::net::socketaddr netaddr;
				if (netaddr.set(port, sip) < 0)
					return -1;
				int addrlen = 0;
				struct sockaddr* paddr = netaddr.getsockaddr(&addrlen);
				if (!paddr)
					return -1;
				int fd = _net.connect_asyn(paddr, addrlen);
				if (fd < 0) {
					_plog->add(CLOG_DEFAULT_ERR, "connect tcp://%s:%u failed.", netaddr.viewip(), port);
					return -1;
				}
				struct epoll_event evt;
				evt.events = EPOLLIN | EPOLLOUT | EPOLLERR;
				evt.data.fd = fd;
				int nerr = 0;
				if (0 != (nerr = _net.epoll_ctl_(_fdepoll, EPOLL_CTL_ADD, fd, &evt))) {
					_plog->add(CLOG_DEFAULT_ERR, "epoll_ctrl_ failed. error = %d", nerr);
					_net.close_(fd);
					return -1;
				}
				psession pss = new session(&_sndbufblks, fd, _piomem);
				if (!pss) {
					_plog->add(CLOG_DEFAULT_ERR, "new session memory error");
					_net.close_(fd);
					return -1;
				}
				pss->_epollevents = EPOLLIN | EPOLLOUT | EPOLLERR;
				_mapsession.set(fd, pss);
				return fd;
			}

			void close()
			{
				std::vector<int> fds;
				fds.reserve(1024);
				_net.getall(fds);

				int fdtype = 0;
				for (auto& i : fds) {
					fdtype = _net.getfdtype(i);
					if (fdtype >= 0 && fdtype != _net.fd_epoll) { //fd除epoll外全部关闭
						_net.epoll_ctl_(_fdepoll, EPOLL_CTL_DEL, i, nullptr);
						_net.close_(i);
					}
				}
				if (_fdepoll >= 0)
					_net.close_(_fdepoll);
				_fdepoll = -1;
			}

			void runtime(int waitmsec, int64_t &currentmsec)
			{
				timerjob(currentmsec);//定时任务，维持上游连接和发送心跳消息
				if (-1 == _fdepoll)
					return;

				int nret = _net.epoll_wait_(_fdepoll, _fdevts, static_cast<int>(sizeof(_fdevts) / sizeof(struct epoll_event)), waitmsec);
				if (nret <= 0) {
					if (_lastwaiterr != nret)
						_plog->add(CLOG_DEFAULT_MSG, "epoll_wait_ return %d", nret);
					_lastwaiterr = nret;
					return;
				}
				currentmsec = ec::mstime();
				for (auto i = 0; i < nret; i++)
					onevent(_fdevts[i], currentmsec);
			}

			int sendtofd(int fd, const void* pdata, size_t size)
			{
				psession pss = nullptr;
				if (!_mapsession.get(fd, pss))
					return -1;
				int ns = pss->sendasyn(pdata, size, _plog);
				if (ns < 0)
					return -1;
				sendtrigger(fd);
				return ns;
			}

			int sendtofd_direct(int fd, const void* pdata, size_t size)
			{
				psession pss = nullptr;
				if (!_mapsession.get(fd, pss))
					return -1;
				const uint8_t* pu = (const uint8_t*)pdata;
				int nlen = (int)(size);
				if (pss->_sndbuf.empty()) {//direct send first
					int ns = _net.send_(fd, pu, nlen, 0);
					if (ns < 0) {
						if (_net.geterrno() != EAGAIN)
							return -1;
					}
					else if (ns == (int)(size)) {
						pss->_allsend += ns;
						return ns;
					}
					pu += ns;
					nlen -= ns;
					pss->_allsend += ns;
				}
				if (pss->sendasyn(pu, nlen, _plog) < 0)
					return -1;
				sendtrigger(fd);
				return (int)size;
			}

			psession getsession(int fd)
			{
				psession pss = nullptr;
				if (!_mapsession.get(fd, pss))
					return nullptr;
				return pss;
			}

			int getsessionstatus(int fd)
			{
				psession pss = nullptr;
				if (!_mapsession.get(fd, pss))
					return -1;
				return pss->_status;
			}

			int setsessionstatus(int fd, int st)
			{
				psession pss = nullptr;
				if (!_mapsession.get(fd, pss))
					return -1;
				pss->_status = st;
				return 0;
			}

			int getsessionprotocol(int fd)
			{
				psession pss = nullptr;
				if (!_mapsession.get(fd, pss))
					return -1;
				return pss->_protocol;
			}

			int setsessionprotocol(int fd, int protocol)
			{
				psession pss = nullptr;
				if (!_mapsession.get(fd, pss))
					return -1;
				pss->_protocol = protocol;
				return 0;
			}

			void setreadpause(int* protocols, int numprotocols, int readpause)
			{
				int j;
				for (auto& i : _mapsession) {
					for (j = 0; j < numprotocols; j++) {
						if (protocols[j] == i->_protocol) {
							i->_readpause = readpause;
							break;
						}
					}
				}
			}

			int waterlevel(int fd)
			{
				psession pss = nullptr;
				if (!_mapsession.get(fd, pss))
					return -1;
				return pss->_sndbuf.waterlevel();
			}

			template<class _ClsPtr>
			bool getextdata(int fd, const char* clsname, _ClsPtr& ptr)
			{
				psession pi = nullptr;
				ptr = nullptr;
				if (!_mapsession.get(fd, pi))
					return false;
				return pi->getextdata(clsname, ptr);
			}

			bool setextdata(int fd, ssext_data* pdata)
			{
				psession pi = nullptr;
				if (!_mapsession.get(fd, pi))
					return false;
				pi->setextdata(pdata);
				return true;
			}
		protected:
#if (0 != EC_AIOSRV_TLS)
			virtual ec::tls::srvca* getCA(int fdlisten) {
				return &_ca;
			}
#endif
			virtual void timerjob(int64_t currentms)
			{
			}
			virtual void onconnect_in(int fd, const char* peer_ip, uint16_t peer_port, int fdlisten)
			{
				_plog->add(CLOG_DEFAULT_DBG, "fd(%d) connect in from %s:%u at listen fd %d", fd, peer_ip, peer_port, fdlisten);
				psession pss = new session(&_sndbufblks, fd, _piomem, fdlisten);
				if (!pss)
					return;
				ec::strlcpy(pss->_peerip, peer_ip, sizeof(pss->_peerip));
				pss->_peerport = peer_port;
				_mapsession.set(fd, pss);

				struct epoll_event evt;
				evt.events = EPOLLIN | EPOLLOUT | EPOLLERR;
				evt.data.fd = fd;
				pss->_epollevents = evt.events;
				int nerr = 0;
				if (0 != (nerr = _net.epoll_ctl_(_fdepoll, EPOLL_CTL_ADD, fd, &evt)))
					_plog->add(CLOG_DEFAULT_ERR, "epoll_ctrl_ EPOLL_CTL_ADD failed @onconnect_in. fd = %d, error = %d", fd, nerr);
			};
			virtual void onconnect_out(int fd, psession* ss) {};
			virtual void onclosefd(int fd, const char* serrinfo)
			{
				// Since Linux 2.6.9, event can be specified as NULL when using EPOLL_CTL_DEL
				_net.epoll_ctl_(_fdepoll, EPOLL_CTL_DEL, fd, nullptr);
				_net.close_(fd);
				_mapsession.erase(fd);
			}

			virtual void onsendonce(psession pss) {};
			int onepollout(int fd)
			{
				psession pss = nullptr;
				if (!_mapsession.get(fd, pss))
					return -1;
				int nfdtype = _net.getfdtype(fd);
				if (NETIO::fd_tcpout == nfdtype) { // asyn connect out
					if (pss->_status == EC_AIO_FD_CONNECTING) {
#ifndef _WIN32
						int serr = 0;
						socklen_t serrlen = sizeof(serr);
						getsockopt(_net.getsysfd(fd), SOL_SOCKET, SO_ERROR, (void*)&serr, &serrlen);
						if (serr) {
							onclosefd(fd, "connect failed.");
							return -1;
						}
#endif
						pss->_status = EC_AIO_FD_CONNECTED;
						onconnect_out(fd, &pss);
					}
				}
				if (sendbuf(pss) < 0) {
					onclosefd(fd, "send failed.");
					return -1;
				}
				return 0;
			}

			//return number send bytes, -1: error
			int sendbuf(psession pss, const void* pdata = nullptr, size_t size = 0)
			{
				int nr = 0, ns, fd = pss->_fd, nall = 0;
				if (pdata && size)
					pss->_sndbuf.append((const uint8_t*)pdata, size);

				const void* pd = nullptr;
				size_t zlen = 0;

				pd = pss->_sndbuf.get(zlen);
				while (pd && zlen) {
					ns = _net.send_(fd, pd, (int)(zlen), 0);
					_plog->add(CLOG_DEFAULT_ALL, "sendbuf fd(%d) size %d", fd, ns);
					if (ns < 0) {
						if (_net.geterrno() != EAGAIN)
							nr = -1;
						break;
					}
					else if (!ns)
						break;
					nall += ns;
					pss->_sndbuf.freesize(ns);
					if (ns < (int)(zlen))
						break;
					pd = pss->_sndbuf.get(zlen);
				}
				if (nr >= 0)
					onsendonce(pss);
				return nr < 0 ? -1 : nall;
			}

			void sendtrigger(int fd)
			{
				psession pss = nullptr;
				if (!_mapsession.get(fd, pss))
					return;

				struct epoll_event evtmod;
				evtmod.events = pss->_sndbuf.empty() ? EPOLLIN | EPOLLERR : EPOLLIN | EPOLLOUT | EPOLLERR;
				evtmod.data.fd = fd;

				if (pss->_epollevents == evtmod.events)
					return;
				pss->_epollevents = evtmod.events;
				int nerr = 0;
				if (0 != (nerr = _net.epoll_ctl_(_fdepoll, EPOLL_CTL_MOD, fd, &evtmod)))
					_plog->add(CLOG_DEFAULT_ERR, "epoll_ctrl_ EPOLL_CTL_MOD failed @onevent. fd = %d,  error = %d", fd, nerr);
			}

			void udp_trigger(int fd, int needsend, uint32_t* pevents)
			{
				struct epoll_event evtmod;
				evtmod.events = needsend ? EPOLLIN | EPOLLOUT | EPOLLERR : EPOLLIN | EPOLLERR;
				evtmod.data.fd = fd;
				int nerr = 0;
				if (pevents) {
					if (*pevents == evtmod.events)
						return;
					*pevents = evtmod.events;
				}
				if (0 != (nerr = _net.epoll_ctl_(_fdepoll, EPOLL_CTL_MOD, fd, &evtmod)))
					_plog->add(CLOG_DEFAULT_ERR, "udp epoll_ctrl_ EPOLL_CTL_MOD failed @onevent. fd = %d,  error = %d", fd, nerr);
				else
					_plog->add(CLOG_DEFAULT_ALL, "udp epoll_ctrl_ EPOLL_CTL_MOD success @onevent. fd = %d, error = %08XH", fd, evtmod.events);

			}

			//return -1: error need close
			virtual int domessage(int fd, ec::bytes& sbuf, int msgtype) = 0;
			virtual int onudpmessage(int fd, const void* pudpfrm, int nsize, const struct sockaddr_in* src_addr) { return 0; }; // return 0 continue read, -1:break;
			virtual void onprotocol(int fd, int nproco) {};
			virtual size_t  sizecanrecv(int fd, psession pss) { return 32 * 1024u; };
			virtual void onudpepollmod(int fd) {};
			virtual void onudpepollout(int fd) {};
			virtual int onupdate_proctcp(int fd, psession* pi) //base TCP protocol. return -1 :error ; 0:no; 1:ok
			{
				const uint8_t* pu = (const uint8_t*)(*pi)->_rbuf.data_();
				size_t size = (*pi)->_rbuf.size_();
				if (size < 3u)
					return 0;
#if (0 != EC_AIOSRV_TLS)
				if (pu[0] == 22 && pu[1] == 3 && pu[2] > 0 && pu[2] <= 3) { // update client TLS protocol
					if (!_ca._pcer.data() || !_ca._pcer.size() || !_ca._pRsaPub || !_ca._pRsaPrivate) {
						if (_plog)
							_plog->add(CLOG_DEFAULT_MOR, "fd(%d) update TLS1.2 protocol failed, no server certificate", fd);
						return -2;// not support
					}
					ec::tls::srvca* pCA = getCA((*pi)->_fdlisten);
					psession ptls = new session_tls(fd, std::move(**pi), pCA->_pcer.data(), pCA->_pcer.size(),
						pCA->_prootcer.data(), pCA->_prootcer.size(), &pCA->_csRsa, pCA->_pRsaPrivate, _plog);
					if (!ptls)
						return -1;
					_mapsession.set(ptls->_fd, ptls);
					*pi = ptls;
					if (_plog)
						_plog->add(CLOG_DEFAULT_MSG, "fd(%d) update TLS1.2 protocol success", fd);
					onprotocol(fd, ptls->_protocol);
					return 1;
				}
#endif
#if (0 != EC_AIOSRV_HTTP)
				if ((ec::strineq("head", (const char*)pu, 4)
					|| ec::strineq("get", (const char*)pu, 3))
					) { //update http
					ec::http::package r;
					if (r.parse((const char*)pu, size) < 0)
						return -1;
					psession phttp = new session_http(std::move(**pi));
					if (!phttp)
						return -1;
					_mapsession.set(phttp->_fd, phttp);
					*pi = phttp;
					if (_plog)
						_plog->add(CLOG_DEFAULT_MSG, "fd(%u) update HTTP protocol success", fd);
					onprotocol(fd, phttp->_protocol);
					return 1;
				}
#endif
				return 0;
			}
#if (0 != EC_AIOSRV_TLS)
			virtual int onupdate_proctls(int fd, psession* pi) //base TLS protocol. return -1 :error ; 0:no; 1:ok
			{
				const uint8_t* pu = (const uint8_t*)(*pi)->_rbuf.data_();
				size_t size = (*pi)->_rbuf.size_();
				if (size < 3u)
					return 0;
				if ((ec::strineq("head", (const char*)pu, 4)
					|| ec::strineq("get", (const char*)pu, 3))
					) { //update http
					ec::http::package r;
					if (r.parse((const char*)pu, size) < 0)
						return -1;
					psession phttp = new session_https(std::move(*((session_tls*)*pi)));
					if (!phttp)
						return -1;
					_mapsession.set(phttp->_fd, phttp);
					*pi = phttp;
					if (_plog)
						_plog->add(CLOG_DEFAULT_MSG, "fd(%u) update HTTPS protocol success", fd);
					onprotocol(fd, phttp->_protocol);
					return 1;
				}
				return 0;
			}
#endif
		private:
			void onudpevent(struct epoll_event& evt)
			{
				if (evt.events & EPOLLIN) {
					int nr = -1, ndo = 160;
					char sbuf[1024 * 64];
					struct sockaddr_in addr;
					socklen_t addrlen = (socklen_t)sizeof(addr);
					memset(&addr, 0, sizeof(addr));
					do {
						nr = _net.recvfrom_(evt.data.fd, sbuf, (int)sizeof(sbuf), 0, (struct sockaddr*)&addr, &addrlen);
						if (nr > 0) {
							if (onudpmessage(evt.data.fd, sbuf, nr, &addr))
								break;
						}
						--ndo;
					} while (nr > 0 && ndo > 0);
				}
				if (evt.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
					_plog->add(CLOG_DEFAULT_DBG, "udp fd %d  error events %08XH", evt.data.fd, evt.events);
					onclosefd(evt.data.fd, "EPOLLERR");
					return;
				}
				if (evt.events & EPOLLOUT) {
					_plog->add(CLOG_DEFAULT_ALL, "udp fd %d  EPOLLOUT, events %08XH", evt.data.fd, evt.events);
					onudpepollout(evt.data.fd);
				}
				onudpepollmod(evt.data.fd);
			}

			void onevent(struct epoll_event& evt, int64_t currentmsec)
			{
				int nfdtype = _net.getfdtype(evt.data.fd);
				if (nfdtype == NETIO::fd_udp) {
					onudpevent(evt);
					return;
				}
				if ((evt.events & EPOLLIN) && NETIO::fd_listen == nfdtype) {
					_plog->add(CLOG_DEFAULT_ALL, "listen fd %d  EPOLLIN, events %08XH", evt.data.fd, evt.events);
					ec::net::socketaddr clientaddr;
					socklen_t* paddrlen = nullptr;
					struct sockaddr* paddr = clientaddr.getbuffer(&paddrlen);
					int fdc = _net.accept_(evt.data.fd, paddr, paddrlen);
					if (fdc >= 0) {
						uint16_t uport = 0;
						char sip[48] = { 0 };
						clientaddr.get(uport, sip, sizeof(sip));
						onconnect_in(fdc, sip, uport, evt.data.fd);
					}
					else
						_plog->add(CLOG_DEFAULT_DBG, "fd %d  accept failed.", evt.data.fd);
					return;
				}
				if (evt.events & EPOLLIN) {
					int nr = -1;
					// supports only level-triggered events.
					psession pss = nullptr;
					if (_mapsession.get(evt.data.fd, pss) && !pss->_readpause) {
						size_t zr = sizecanrecv(evt.data.fd, pss);//为应用层流控设计
						if (zr > 0) {
							if (zr > sizeof(_recvtmp))
								zr = sizeof(_recvtmp);
							nr = _net.recv_(evt.data.fd, _recvtmp, zr, 0);
							if (!nr || (nr < 0 && EAGAIN != _net.geterrno() && EWOULDBLOCK != _net.geterrno())) {
								onclosefd(evt.data.fd, nr ? "recv error" : "peer close");
								return;
							}
							if (onrecv(pss, _recvtmp, nr, currentmsec) < 0) {
								onclosefd(evt.data.fd, nullptr);
								return;
							}
						}
					}
				}
				if (evt.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
					_plog->add(CLOG_DEFAULT_DBG, "fd %d  error events %08XH", evt.data.fd, evt.events);
					onclosefd(evt.data.fd, "EPOLLERR");
					return;
				}
				if (evt.events & EPOLLOUT) {
					_plog->add(CLOG_DEFAULT_ALL, "fd %d  EPOLLOUT, events %08XH", evt.data.fd, evt.events);
					if (onepollout(evt.data.fd) < 0)
						return;
				}
				if (NETIO::fd_listen != nfdtype && NETIO::fd_epoll != nfdtype)
					sendtrigger(evt.data.fd);
			}

			int onrecv(psession pss, const void* pbuf, size_t size, int64_t currentms)
			{
				ec::bytes msg(_piomem);
				int msgtype = pss->onrecvbytes(pbuf, size, _plog, &msg);
				if (EC_AIO_PROC_TCP == pss->_protocol && EC_AIO_MSG_TCP == msgtype) {
					pss->_rbuf.append(msg.data(), msg.size());
					int nup = onupdate_proctcp(pss->_fd, &pss);
					if (nup != 1)
						return nup;
					msg.clear();
					msgtype = pss->onrecvbytes(nullptr, 0, _plog, &msg);
				}
#if (0 != EC_AIOSRV_TLS)
				else if (EC_AIO_PROC_TLS == pss->_protocol && EC_AIO_MSG_TCP == msgtype) {
					pss->_rbuf.append(msg.data(), msg.size());
					int nup = onupdate_proctls(pss->_fd, &pss);
					if (nup != 1)
						return nup;
					msg.clear();
					msgtype = pss->onrecvbytes(nullptr, 0, _plog, &msg);
				}
#endif
				while (msgtype > EC_AIO_MSG_NUL) {
					if (domessage(pss->_fd, msg, msgtype) < 0)
						return -1;
					msg.clear();
					msgtype = pss->onrecvbytes(nullptr, 0, _plog, &msg);
				}
				if (msgtype == EC_AIO_MSG_ERR) {
					onclosefd(pss->_fd, "onrecvbytes error");
					return -1;
				}
				return 0;
			}
		};
	} // namespace aio
}//namespace ec