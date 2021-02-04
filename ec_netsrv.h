/*!
\file ec_netsrv.h
\author	jiangyong
\email  kipway@outlook.com
\update 2021.2.3

net::server
	a class for TCP/UDP, HTTP/HTTPS, WS/WSS

eclib 3.0 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/
#pragma once

#ifndef ECNET_DELAY_DISCONNECT_SECS
#	define ECNET_DELAY_DISCONNECT_SECS  30
#endif

#ifndef ECNETSRV_TLS
#	define ECNETSRV_TLS  0
#endif

#ifndef ECNETSRV_WS
#	define ECNETSRV_WS   0
#endif

#ifndef ECNETSRV_WSS
#	define ECNETSRV_WSS  0
#endif
#include "ec_netss_base.h"

#if (0 != ECNETSRV_WS)
#	include "ec_netss_ws.h"
#endif

#if (0 != ECNETSRV_TLS)
#	include "ec_netss_tls.h"

#endif

#if (0 != ECNETSRV_WSS)
#	include "ec_netss_wss.h"
#endif

#ifndef SIZE_TCP_READ_ONCE
#	define SIZE_TCP_READ_ONCE (64 * 1024)
#endif

#define EC_NET_SRV_TCP  0 // TCP server
#define EC_NET_SRV_IPC  1 // IPC server
#define EC_NET_SRV_UDP  2 // UDP server

#include "ec_vector.h"
#include "ec_netio.h"
#include "ec_string.h"

#ifndef _WIN32
#include <sys/resource.h>
#endif

#ifndef SIZE_RESERVED_NOFILE  // reserved number of FD
#define SIZE_RESERVED_NOFILE 80
#endif

#ifndef SIZE_WIN_NOFILE_LIMT  // windows max number of FD
#define SIZE_WIN_NOFILE_LIMT 50000
#endif

namespace ec
{
	namespace net
	{
		class server
		{
		public:
			struct t_ssbufsize { // bytes not send in buffer per connection
				uint32_t key;
				uint32_t usize;
			};
			struct t_listen {
				uint32_t key; //ucid
				uint16_t _wport;
				SOCKET _fd;
				int   _srvtype;// server type , EC_NET_SRV_TCP, EC_NET_SRV_IPC, EC_NET_SRV_UDP
			};
			static int get_nofile_lim_max()
			{
#ifdef _WIN32
				return SIZE_WIN_NOFILE_LIMT;
#else
				struct rlimit rout;
				if (getrlimit(RLIMIT_NOFILE, &rout))
					return SIZE_WIN_NOFILE_LIMT;
				return rout.rlim_cur;
#endif
			}
		protected:
			hashmap<uint32_t, t_listen> _maplisten;
			memory* _pmem; // Memory allocator
			ilog* _plog;
			vector<pollfd> _pollfd;
			vector<uint32_t> _pollkey;
		protected:
			hashmap<uint32_t, PNETSS, keq_netss, del_netss> _map;
			bool _bmodify_pool; // true:session poll changed
			int  _nerr_emfile_count; //
			uint32_t _unextid; // next Unique serial number
			uint32_t _unextsrvid; // next Unique serial number
			time_t _time_lastrun;
			int   _pauseread; // 1: pause read ;0 : read
		protected:
			hashmap<uint32_t, t_ssbufsize> _mapbufsize;// send buffer size map,  todo _mapbufsize的多线程操作
#if (0 != ECNETSRV_TLS)
			tls::srvca _ca;  // certificate
#endif
			evtfd _evtfd; // event for application
			char _sucidfile[512];//ucid save file,if nullstring ,use memory ucid from UCID_DYNAMIC_START
		private:
			bytes _udpbuf;
		public:
			server(memory* pmem, ilog* plog)
				: _pmem(pmem)
				, _plog(plog)
				, _pollfd(pmem)
				, _pollkey(pmem)
				, _nerr_emfile_count(0)
				, _unextid(UCID_DYNAMIC_START)
				, _unextsrvid(0)
				, _udpbuf(pmem)
			{
				_pollkey.reserve(1024);
				_bmodify_pool = _evtfd.open();
				_time_lastrun = 0;
				memset(_sucidfile, 0, sizeof(_sucidfile));
				_pauseread = 0;
				_udpbuf.reserve(1024 * 64);
			}
			void set_ucidfile(const char* sfile)
			{
				if (!sfile || !*sfile)
					return;
				strlcpy(_sucidfile, sfile, sizeof(_sucidfile));
				if (_sucidfile[0]) {
					FILE *pf = fopen(_sucidfile, "rt");
					if (pf) {
						char sid[40] = { 0 };
						fread(sid, 1, sizeof(sid) - 1u, pf);
						fclose(pf);
						_unextid = (uint32_t)atoll(sid);
						if (_plog)
							_plog->add(CLOG_DEFAULT_MSG, "read %s success,ucid start %u", _sucidfile, _unextid);
						if (_unextid < UCID_DYNAMIC_START)
							_unextid = UCID_DYNAMIC_START;
					}
					else {
						FILE *pf = fopen(_sucidfile, "wt");
						if (pf) {
							if (_plog)
								_plog->add(CLOG_DEFAULT_MSG, "%s create success, ucid start %u", _sucidfile, _unextid);
							char sid[40] = { 0 };
							if (snprintf(sid, sizeof(sid), "%u", _unextid) > 0)
								fwrite(sid, 1, strlen(sid), pf);
							fclose(pf);
						}
						else {
							if (_plog)
								_plog->add(CLOG_DEFAULT_ERR, "%s access failed, ucid start %u in memory.", _sucidfile, _unextid);
						}
					}
				}
			}
			virtual ~server()
			{
				_map.clear();
				_mapbufsize.clear();
				_maplisten.clear();
			}

			inline void set_event()
			{
				_evtfd.set_event();
			}

			bool exist_srv(uint16_t port, int srvtype)
			{
				for (const auto & i : _maplisten) {
					if (i._wport == port && i._srvtype == srvtype)
						return true;
				}
				return false;
			}

			/*!
			\brief open default tcp server, ucid = EC_NET_LISTENID
			*/
			uint32_t open(uint16_t port, const char* sip = nullptr)
			{
				return add_tcpsrv(EC_NET_LISTENID, port, sip);
			}

			/*!
			\brief add tcp server
			\param listenid = (EC_NET_LISTENID , EC_NET_CONOUTID)
			*/
			uint32_t add_tcpsrv(uint32_t listenid, uint16_t port, const char* sip = nullptr)
			{
				if (!port || exist_srv(port, EC_NET_SRV_TCP))
					return 0;

				t_listen ni;
				ni._wport = port;
				ni._fd = listen_tcp(port, sip);
				ni._srvtype = EC_NET_SRV_TCP;
				if (INVALID_SOCKET == ni._fd)
					return 0;
				if (!listenid)
					ni.key = nextsrvid();
				else
					ni.key = listenid;
				listen_session *p = new listen_session(ni.key, ni._fd, _pmem, _plog);
				if (!p) {
					::closesocket(ni._fd);
					return 0;
				}
				_maplisten.set(ni.key, ni);
				_map.set(ni.key, (PNETSS)p);
				_bmodify_pool = true;
				return ni.key;
			}

			/*!
			\brief add ipc server
			\param srvid = (EC_NET_LISTENID , EC_NET_CONOUTID)
			*/
			uint32_t add_ipcsrv(uint32_t ucid, uint16_t port, const char* skeywords = "ezipc", const char* slocalip = "127.0.0.171")
			{
				if (!port || exist_srv(port, EC_NET_SRV_IPC))
					return 0;

				t_listen ni;
				ni._wport = port;
				ni._srvtype = EC_NET_SRV_IPC;
				ni._fd = listen_ipc(port, skeywords, slocalip);
				if (INVALID_SOCKET == ni._fd)
					return 0;
				if (!ucid)
					ni.key = nextsrvid();
				else
					ni.key = ucid;
				listen_session *p = new listen_session(ni.key, ni._fd, _pmem, _plog);
				if (!p) {
					::closesocket(ni._fd);
					return 0;
				}
				_maplisten.set(ni.key, ni);
				_map.set(ni.key, (PNETSS)p);
				_bmodify_pool = true;
				return ni.key;
			}

			uint32_t add_udpsrv(uint32_t ucid, uint16_t port, const char* sip = nullptr)
			{
				if (!port || exist_srv(port, EC_NET_SRV_UDP))
					return 0;

				SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
				if (s == INVALID_SOCKET)
					return 0;

				struct sockaddr_in sin;
				memset(&sin, 0, sizeof(sin));
				sin.sin_family = AF_INET;
				if (!sip || !*sip)
					sin.sin_addr.s_addr = htonl(INADDR_ANY);
				else
					sin.sin_addr.s_addr = inet_addr(sip);
				sin.sin_port = htons(port);
				if (SOCKET_ERROR == bind(s, (struct sockaddr *)&sin, (int)sizeof(sin))) {
					::closesocket(s);
					return 0;
				}

				t_listen ni;
				if (!ucid)
					ni.key = nextsrvid();
				else
					ni.key = ucid;
				ni._wport = port;
				ni._srvtype = EC_NET_SRV_UDP;
				ni._fd = s;

				session_udpsrv *p = new session_udpsrv(ni.key, ni._fd, _pmem, _plog);
				if (!p) {
					::closesocket(ni._fd);
					return 0;
				}
				_maplisten.set(ni.key, ni);
				_map.set(ni.key, (PNETSS)p);
				_bmodify_pool = true;
				return ni.key;
			}

			void close()
			{
				_map.clear();
				_mapbufsize.clear();
				_maplisten.clear();
			}

			uint32_t _inbufsize(uint32_t ucid) //get the buffer size by ucid,used for Server push flow control
			{
				PNETSS pi = nullptr;
				if (!_map.get(ucid, pi))
					return 0;
				return (uint32_t)pi->sndbufsize();
			}
			uint32_t lck_bufsize(uint32_t ucid) //get the buffer size by ucid,used for Server push flow control
			{
				t_ssbufsize t;
				t.key = ucid;
				t.usize = 0;
				_mapbufsize.get(ucid, t);
				return t.usize;
			}

			bool ucid_exist(uint32_t ucid)
			{
				return _map.get(ucid) != nullptr;
			}

			/*！
			\brief send application layer message
			\param pdata [in] message byte stream
			\param size [in] pdata size(bytes)
			\return return -1:error ; >=0 OK.
			*/
			int sendbyucid(uint32_t ucid, const void*pmsg, size_t msgsize)
			{
				int nr = -1;
				PNETSS pi = nullptr;
				if (_map.get(ucid, pi))
					nr = pi->send(pmsg, msgsize);

				if (nr >= 0 && pi) {
					t_ssbufsize t;
					t.key = ucid;
					t.usize = (uint32_t)pi->sndbufsize();
					if (t.usize)
						_mapbufsize.set(ucid, t);
					else
						_mapbufsize.erase(ucid);
				}
				if (nr < 0)
					closeucid(ucid);
				return nr;
			}

			bool setsendbufsize(uint32_t ucid, int size)
			{
				PNETSS pi = nullptr;
				if (_map.get(ucid, pi))
					return ec::net::setsendbuf(pi->_fd, size) > 0;
				return false;
			}

			bool set_pause_read(uint32_t ucid, int v)
			{
				PNETSS pi = nullptr;
				if (!_map.get(ucid, pi))
					return false;
				pi->_pause_r = v;
				return true;
			}

			int get_pause_read(uint32_t ucid)
			{
				PNETSS pi = nullptr;
				if (!_map.get(ucid, pi))
					return 0;
				return pi->_pause_r;
			}

			char* get_ip(uint32_t ucid, char* sip, size_t outsize)
			{
				if (!sip || !outsize)
					return nullptr;
				*sip = 0;
				PNETSS pi = nullptr;
				if (_map.get(ucid, pi))
					ec::strlcpy(sip, pi->_ip, outsize);
				return sip;
			}

			void set_status(uint32_t ucid, int32_t st)
			{
				PNETSS pi = nullptr;
				if (_map.get(ucid, pi))
					pi->_status = st;
			}

			int32_t get_status(uint32_t ucid)
			{
				PNETSS pi = nullptr;
				if (_map.get(ucid, pi))
					return pi->_status;
				return -1;
			}

			uint32_t get_protoc(uint32_t ucid)
			{
				PNETSS pi = nullptr;
				if (_map.get(ucid, pi))
					return pi->_protoc;
				return EC_NET_SS_NONE;
			}

			uint32_t get_peerport(uint32_t ucid)
			{
				PNETSS pi = nullptr;
				if (_map.get(ucid, pi))
					return pi->_peerport;
				return 0;
			}

			PNETSS get_session(uint32_t ucid)
			{
				PNETSS pi = nullptr;
				if (_map.get(ucid, pi))
					return pi;
				return nullptr;
			}

			int flushsend(uint32_t ucid) // return >=0:ok ; -1:error
			{
				PNETSS pi = nullptr;
				if (_map.get(ucid, pi))
					return pi->flush_sendbuf();
				return 0;
			}

			template<class _ClsPtr>
			bool getextdata(uint32_t ucid, const char* clsname, _ClsPtr& ptr)
			{
				PNETSS pi = nullptr;
				ptr = nullptr;
				if (!_map.get(ucid, pi))
					return false;
				return pi->getextdata(clsname, ptr);
			}

			bool setextdata(uint32_t ucid, ssext_data* pdata)
			{
				PNETSS pi = nullptr;
				if (!_map.get(ucid, pi))
					return false;
				pi->setextdata(pdata);
				return true;
			}

			/*!
			\brief close session
			\remark for connect in session,call ondisconnect before delete; for connect out session ,call ondisconnect after delete;
			*/
			bool closeucid(uint32_t ucid)
			{
				PNETSS pi = nullptr;
				if (_map.get(ucid, pi)) {
					if (pi->_protoc & EC_NET_SS_CONOUT) { // connect out session , delete first
						uint32_t listenid = pi->_listenid;
						ondelconnectout(ucid);
						deletesession(ucid);
						_bmodify_pool = true;
						_mapbufsize.erase(ucid);
						ondisconnect(listenid, ucid);
					}
					else {   // connect in session , delete last
						ondisconnect(pi->_listenid, ucid);
						_bmodify_pool = true;
						_mapbufsize.erase(ucid);
						deletesession(ucid);
					}
					return true;
				}
				return false;
			}
			void eraseucid(uint32_t ucid)
			{
				_bmodify_pool = true;
				_mapbufsize.erase(ucid);
				deletesession(ucid);
			}
			void addsession(PNETSS pss)
			{
				_map.set(pss->_ucid, pss);
				_bmodify_pool = true;
			}

		private:
			inline void deletesession(uint32_t ucid)
			{
				_map.erase(ucid);
			}
		protected:
			virtual bool onmessage(int listenid, uint32_t ucid, uint32_t protoc, bytes &pkgr, const struct sockaddr_in *src_addr) = 0; // 调度和处理消息, return false will disconnect

			virtual void onconnect(int listenid, uint32_t ucid) = 0;
			virtual void onaccept(int listenid, SOCKET sock)
			{
				ec::net::tcpnodelay(sock);
				ec::net::setkeepalive(sock);
			};
			virtual void ondelconnectout(uint32_t ucid) {}; //before delete connect out
			virtual void ondisconnect(int listenid, uint32_t ucid) = 0;
			virtual void onevent() {};
			virtual void onprotocol(uint32_t ucid, uint32_t uprotoc) {};
			virtual void onsend_c_end(uint32_t ucid, int protoc, int listenid)
			{
			}
			virtual void onpoll_once()
			{
			}
			virtual void onpoll_onefd()
			{
			}
			virtual bool hasprotocol(uint32_t srvid, uint32_t uprotoc)
			{
				return true;
			}
			virtual void on_read_once_end(uint32_t ucid, int listenid)
			{
			}
			virtual int on_update_basetcp(uint32_t ucid, PNETSS *pi, const uint8_t* pu, size_t size) //base TCP protocol. return -1 :error ; 0:no; 1:ok
			{
				if (*pi) {
					(*pi)->_status = EC_NET_ST_ATTACK;
					(*pi)->_time_err = ::time(nullptr);
				}
				return -1;
			}
#if (0 != ECNETSRV_TLS)
			virtual int on_update_basetls(uint32_t ucid, PNETSS* pi, const uint8_t* pu, size_t size) //update base TLS protocol. return -1 :error ; 0:no; 1:ok
			{
				if (*pi) {
					(*pi)->_status = EC_NET_ST_ATTACK;
					(*pi)->_time_err = ::time(nullptr);
				}
				return -1;
			}
#endif
			uint32_t nextid()
			{
				_unextid++;
				if (_unextid < UCID_DYNAMIC_START || _unextid >= INT32_MAX)
					_unextid = UCID_DYNAMIC_START;
				while (_map.get(_unextid)) {
					_unextid++;
					if (_unextid < UCID_DYNAMIC_START)
						_unextid = UCID_DYNAMIC_START;
				}
				if (_sucidfile[0]) {
					FILE *pf = fopen(_sucidfile, "wt");
					if (pf) {
						char sid[40] = { 0 };
						if (snprintf(sid, sizeof(sid), "%u", _unextid) > 0)
							fwrite(sid, 1, strlen(sid), pf);
						fclose(pf);
						if (_plog)
							_plog->add(CLOG_DEFAULT_DBG, "write ucid %u to %s success", _unextid, _sucidfile);
					}
					else {
						if (_plog)
							_plog->add(CLOG_DEFAULT_ERR, "write ucid %u to %s failed", _unextid, _sucidfile);
					}
				}
				return _unextid;
			}

			uint32_t nextsrvid()
			{
				_unextsrvid++;
				if (_unextsrvid < 100)
					_unextsrvid = 100;
				while (_map.get(_unextsrvid)) {
					_unextsrvid++;
					if (_unextsrvid < 100)
						_unextsrvid = 100;
				}
				return _unextsrvid;
			}
		public:
#if (0 != ECNETSRV_TLS)
			bool initca(const char* filecert, const char* filerootcert, const char* fileprivatekey)
			{
				if (_ca._px509)
					_ca.~srvca();
				if (!_ca._px509 && !_ca.InitCert(filecert, filerootcert, fileprivatekey)) {
					if (_plog)
						_plog->add(CLOG_DEFAULT_ERR, "Load certificate failed (%s,%s,%s)", filecert,
							filerootcert != nullptr ? filerootcert : "none", fileprivatekey);
					return false;
				}
				return true;
			}
#endif
#ifndef _WIN32
			static int  SetNoBlock(int sfd)
			{
				int flags, s;
				flags = fcntl(sfd, F_GETFL, 0);
				if (flags == -1)
					return -1;
				flags |= O_NONBLOCK;
				s = fcntl(sfd, F_SETFL, flags);
				if (s == -1)
					return -1;
				return 0;
			}
#endif
			int get_srvtype(uint32_t listenid)
			{
				t_listen* p = _maplisten.get(listenid);
				if (p)
					return p->_srvtype;
				return -1;
			}

			void runtime(int waitmicroseconds)
			{
				int n = 0;
				time_t tcur = ::time(nullptr);
				make_pollfds();
#ifdef _WIN32
				n = WSAPoll(_pollfd.data(), (ULONG)_pollfd.size(), waitmicroseconds);
#else
				n = poll(_pollfd.data(), _pollfd.size(), waitmicroseconds);
#endif
				if (n <= 0) {
					if (_time_lastrun == tcur)
						return;// 1seconds force run once
				}
				_time_lastrun = tcur;

				pollfd* p = _pollfd.data();
				uint32_t* puid = _pollkey.data();
				for (auto i = 0u; i < _pollfd.size(); i++) {
					onpoll_onefd();
					if (0u == i) { // evtfd
						if (p[0].revents & POLLIN) {
							_evtfd.reset_event();
							p[0].events = POLLIN;
							onevent();
						}
						continue;
					}
					PNETSS ps = nullptr;
					if (!_map.get(puid[i], ps)) {
						p[i].revents = 0;
						continue;
					}
					if (ps->_protoc != EC_NET_SS_LISTEN && ps->_status == EC_NET_ST_ATTACK && tcur - ps->_time_err >= ECNET_DELAY_DISCONNECT_SECS) {
						if (closeucid(puid[i]) && _plog)
							_plog->add(CLOG_DEFAULT_MOR, "ucid(%u) disconnect by server as error protocol %d seconds", puid[i], ECNET_DELAY_DISCONNECT_SECS);
						p[i].revents = 0;
						continue;
					}
					if (ps->_protoc == EC_NET_SS_LISTEN) { //listen
						if (p[i].revents & POLLIN) {
							int nsrvtye = get_srvtype(puid[i]);
							if (EC_NET_SRV_TCP == nsrvtye) {
								if (acp(puid[i]))
									_bmodify_pool = true;
							}
							else if (EC_NET_SRV_IPC == nsrvtye) {
								if (acp_ipc(puid[i]))
									_bmodify_pool = true;
							}
							else if (EC_NET_SRV_UDP == nsrvtye) {
								_udpbuf.clear();
								struct sockaddr_in addr;
								socklen_t addrlen = (socklen_t)sizeof(addr);
								memset(&addr, 0, sizeof(addr));
								int nr = ::recvfrom(p[i].fd, (char*)_udpbuf.data(), (int)_udpbuf.capacity(), 0, (struct sockaddr*)&addr, &addrlen);
								if (nr > 0) {
									_udpbuf.resize(nr);
									onmessage(ps->_listenid, puid[i], EC_NET_SRV_UDP, _udpbuf, &addr);
								}
								p[i].events = POLLIN;
							}
						}
						p[i].revents = 0;
						continue;
					}
					if (p[i].revents & (POLLERR | POLLHUP | POLLNVAL)) { // error
						if (closeucid(puid[i]) && _plog)
							_plog->add(CLOG_DEFAULT_MOR, "ucid(%u) disconnect", puid[i]);
						p[i].revents = 0;
						continue;
					}

					if (p[i].revents & POLLOUT) {
						if ((ps->_protoc & EC_NET_SS_CONOUT) && ps->_status == EC_NET_ST_PRE) {
							ps->_status = EC_NET_ST_CONNECT; //connected
#ifndef _WIN32
							int serr = 0;
							socklen_t serrlen = sizeof(serr);
							getsockopt(ps->_fd, SOL_SOCKET, SO_ERROR, (void *)&serr, &serrlen);
							if (serr) {
								if (closeucid(puid[i]) && _plog)
									_plog->add(CLOG_DEFAULT_MOR, "ucid(%u) connect out failed.", puid[i]);
								continue;
							}
#endif
							onconnect(ps->_listenid, puid[i]);
						}
						else {
							if (ps->send_c() < 0) {
								closeucid(puid[i]);
								continue;
							}
							onsend_c_end(puid[i], ps->_protoc, ps->_listenid);
						}
						t_ssbufsize t;
						t.key = puid[i];
						t.usize = (uint32_t)ps->sndbufsize();
						if (t.usize)
							_mapbufsize.set(t.key, t);
						else
							_mapbufsize.erase(t.key);
					}
					if ((p[i].revents & POLLIN) && ps && !ps->_pause_r && !_pauseread)
						doread(puid[i], p[i].fd);
					p[i].revents = 0;
					if (!_bmodify_pool) {
						if (!_map.get(puid[i], ps))
							continue;
						if ((ps->_protoc & EC_NET_SS_CONOUT) && ps->_status == EC_NET_ST_PRE)
							p[i].events = POLLOUT;
						else if (ps->sndbufsize())
							p[i].events = POLLOUT | POLLIN;
						else
							p[i].events = POLLIN;
					}
				}
				onpoll_once();
			}
		protected:
			virtual void on_emfile()
			{
			}
			SOCKET listen_tcp(unsigned short wport, const char* sip = nullptr)
			{
				if (!wport)
					return INVALID_SOCKET;
				SOCKET sl = INVALID_SOCKET;
				struct sockaddr_in	netaddr;
#ifdef _WIN32
				if ((sl = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED)) == INVALID_SOCKET)
#else
				if ((sl = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET)
#endif
				{
					if (_plog)
						_plog->add(CLOG_DEFAULT_ERR, "TCP port %u socket error!", wport);
					fprintf(stderr, "TCP port %u socket error!\n", wport);
					return INVALID_SOCKET;
				}
				netaddr.sin_family = AF_INET;
				if (!sip || !sip[0])
					netaddr.sin_addr.s_addr = htonl(INADDR_ANY);
				else
					netaddr.sin_addr.s_addr = inet_addr(sip);
				netaddr.sin_port = htons(wport);

				int opt = 1;
#ifdef _WIN32
				u_long iMode = 1;

				if (SOCKET_ERROR == setsockopt(sl, SOL_SOCKET, SO_REUSEADDR,
					(const char *)&opt, sizeof(opt)) || SOCKET_ERROR == ioctlsocket(sl, FIONBIO, &iMode)) {
					::closesocket(sl);
					if (_plog)
						_plog->add(CLOG_DEFAULT_ERR, "TCP port [%d] setsockopt or  ioctlsocket failed", wport);
					return INVALID_SOCKET;
				}
#else
				if (-1 == setsockopt(sl, SOL_SOCKET, SO_REUSEADDR,
					(const void *)&opt, sizeof(opt)) || -1 == SetNoBlock(sl)) {
					::close(sl);
					if (_plog)
						_plog->add(CLOG_DEFAULT_ERR, "TCP port [%d] setsockopt or  ioctlsocket failed", wport);
					return INVALID_SOCKET;
				}
#endif
				if (bind(sl, (const sockaddr *)&netaddr, sizeof(netaddr)) == SOCKET_ERROR) {
#ifdef _WIN32
					int nerr = ::WSAGetLastError();
					::closesocket(sl);
#else
					int nerr = errno;
					::close(sl);
#endif
					if (_plog)
						_plog->add(CLOG_DEFAULT_ERR, "TCP port [%d] bind failed with error %d", wport, nerr);
					fprintf(stderr, "ERR:TCP port [%u] bind failed with error %d\n", wport, nerr);
					return INVALID_SOCKET;
				}
				if (listen(sl, SOMAXCONN) == SOCKET_ERROR) {
#ifdef _WIN32
					int nerr = ::WSAGetLastError();
					::closesocket(sl);
#else
					int nerr = errno;
					::close(sl);
#endif

					if (_plog)
						_plog->add(CLOG_DEFAULT_ERR, "TCP port %d  listen failed with error %d", wport, nerr);
					fprintf(stderr, "ERR: TCP port %u  listen failed with error %d\n", wport, nerr);
					return INVALID_SOCKET;
				}
				return sl;
			}

			SOCKET listen_ipc(uint16_t wport, const char* skeywords = "ezipc", const char* slocalip = "127.0.0.171")// c11_ipc.h "ECIPC", "127.0.0.191"
			{
				if (!wport)
					return INVALID_SOCKET;
				SOCKET sl = INVALID_SOCKET;
#ifdef USE_AFUNIX
				if ((sl = socket(AF_UNIX, SOCK_STREAM, 0)) == INVALID_SOCKET)
					return INVALID_SOCKET;

				struct sockaddr_un Addr;
				memset(&Addr, 0, sizeof(Addr));
				Addr.sun_family = AF_UNIX;
				snprintf(Addr.sun_path, sizeof(Addr.sun_path), "/var/tmp/%s:%d", skeywords, wport);
				unlink(Addr.sun_path);
#else
				if ((sl = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET)
					return INVALID_SOCKET;
				struct sockaddr_in	Addr;
				memset(&Addr, 0, sizeof(Addr));
				Addr.sin_family = AF_INET;
				Addr.sin_addr.s_addr = inet_addr(slocalip);
				Addr.sin_port = htons(wport);
#endif
				if (bind(sl, (const sockaddr *)&Addr, sizeof(Addr)) == SOCKET_ERROR) {
#ifdef _WIN32
					int nerr = ::WSAGetLastError();
					::closesocket(sl);
#else
					int nerr = errno;
					::close(sl);
#endif
					if (_plog)
						_plog->add(CLOG_DEFAULT_ERR, "IPC port [%d] bind failed with error %d", wport, nerr);
					fprintf(stderr, "ERR:IPC port [%u] bind failed with error %d\n", wport, nerr);
					return INVALID_SOCKET;
				}
				if (listen(sl, SOMAXCONN) == SOCKET_ERROR) {
#ifdef _WIN32
					int nerr = ::WSAGetLastError();
					closesocket(sl);
#else
					int nerr = errno;
					::close(sl);
#endif
					if (_plog)
						_plog->add(CLOG_DEFAULT_ERR, "IPC port %d  listen failed with error %d", wport, nerr);
					fprintf(stderr, "ERR: IPC port %u  listen failed with error %d\n", wport, nerr);
					return INVALID_SOCKET;
				}
				return sl;
			}
		private:
			void make_pollfds()
			{
				if (!_bmodify_pool) // no change
					return;
				_pollfd.clear();
				_pollkey.clear();

				pollfd t;
				t.fd = _evtfd.getfd(); //add eventfd
				t.events = POLLIN;
				t.revents = 0;
				_pollfd.push_back(t);
				_pollkey.push_back(0);

				for (auto &v : _map) {
					t.fd = v->_fd;
					if ((EC_NET_SS_CONOUT & v->_protoc) && !v->_status)
						t.events = POLLOUT;
					else if (v->sndbufsize())
						t.events = POLLIN | POLLOUT;
					else
						t.events = POLLIN;
					t.revents = 0;
					_pollfd.push_back(t);
					_pollkey.push_back(v->_ucid);
				}
				_bmodify_pool = false;
			}

			bool acp(uint32_t listenid)
			{
				SOCKET	sAccept;
				struct  sockaddr_in		 addrClient;
				int		nClientAddrLen = (int)sizeof(addrClient);
				t_listen* pl = _maplisten.get(listenid);
				if (!pl)
					return false;

#ifdef _WIN32
				if ((sAccept = ::accept(pl->_fd, (struct sockaddr*)(&addrClient), &nClientAddrLen)) == INVALID_SOCKET) {
					int nerr = WSAGetLastError();
					if (WSAEMFILE == nerr) {
						if (!_nerr_emfile_count && _plog)
							_plog->add(CLOG_DEFAULT_ERR, "server port(%d) error EMFILE!", pl->_wport);
						_nerr_emfile_count++;
						on_emfile();
					}
					else
						_nerr_emfile_count = 0;
					return false;
				}
				u_long iMode = 1;
				if (SOCKET_ERROR == ioctlsocket(sAccept, FIONBIO, &iMode)) {
					::closesocket(sAccept);
					return false;
				}
				net::setsendbuf(sAccept, 1024 * 32);
#else
				if ((sAccept = ::accept(pl->_fd, (struct sockaddr*)(&addrClient), (socklen_t*)&nClientAddrLen)) == INVALID_SOCKET) {
					int nerr = errno;
					if (EMFILE == nerr) {
						if (!_nerr_emfile_count && _plog)
							_plog->add(CLOG_DEFAULT_ERR, "server port(%d) error EMFILE!", pl->_wport);
						_nerr_emfile_count++;
						on_emfile();
					}
					else
						_nerr_emfile_count = 0;
					return false;
				}
				if (SetNoBlock(sAccept) < 0) {
					::close(sAccept);
					return false;
				}
#endif
				if ((int)_map.size() + SIZE_RESERVED_NOFILE > get_nofile_lim_max()) {
#ifdef _WIN32
					shutdown(sAccept, SD_BOTH);
					::closesocket(sAccept);
#else
					shutdown(sAccept, SHUT_RDWR);
					::close(sAccept);
#endif
					_plog->add(CLOG_DEFAULT_ERR, "TCP server port(%d) error EMFILE reserved!", pl->_wport);
					return false;
				}
				onaccept(listenid, sAccept);
				PNETSS pi = new session(listenid, nextid(), sAccept, EC_NET_SS_TCP, EC_NET_ST_CONNECT, _pmem, _plog);
				if (!pi) {
					::closesocket(sAccept);
					return false;
				}
				pi->setip(inet_ntoa(addrClient.sin_addr));
				pi->_peerport = ntohs(addrClient.sin_port);
				_map.set(pi->_ucid, pi);
				if (_plog)
					_plog->add(CLOG_DEFAULT_MOR, "port(%u) ucid(%u) connect from %s:%u", pl->_wport, pi->_ucid, pi->_ip, ntohs(addrClient.sin_port));
				onconnect(listenid, pi->_ucid);
				return true;
			}

			bool acp_ipc(uint32_t listenid)
			{
				SOCKET	sAccept;
#ifdef USE_AFUNIX
				struct  sockaddr_un		 addrClient;
#else
				struct  sockaddr_in		 addrClient;
#endif
				int		nClientAddrLen = sizeof(addrClient);
				t_listen* pl = _maplisten.get(listenid);
				if (!pl)
					return false;
#ifdef _WIN32
				if ((sAccept = ::accept(pl->_fd, (struct sockaddr*)(&addrClient), &nClientAddrLen)) == INVALID_SOCKET) {
					int nerr = WSAGetLastError();
					if (WSAEMFILE == nerr) {
						if (!_nerr_emfile_count && _plog)
							_plog->add(CLOG_DEFAULT_ERR, "ipc server port(%d) error EMFILE!", pl->_wport);
						_nerr_emfile_count++;
						on_emfile();
					}
					else
						_nerr_emfile_count = 0;
					return false;
				}
				u_long iMode = 1;
				ioctlsocket(sAccept, FIONBIO, &iMode);
				if (_plog)
					_plog->add(CLOG_DEFAULT_MOR, "ipc port(%u) connect from %s:%u", pl->_wport, inet_ntoa(addrClient.sin_addr), ntohs(addrClient.sin_port));
#else
				if ((sAccept = ::accept(pl->_fd, (struct sockaddr*)(&addrClient), (socklen_t*)&nClientAddrLen)) == INVALID_SOCKET) {
					int nerr = errno;
					if (EMFILE == nerr) {
						if (!_nerr_emfile_count && _plog)
							_plog->add(CLOG_DEFAULT_ERR, "ipc server port(%d) error EMFILE!", pl->_wport);
						_nerr_emfile_count++;
						on_emfile();
					}
					else
						_nerr_emfile_count = 0;
					return false;
				}
				if (SetNoBlock(sAccept) < 0) {
					::close(sAccept);
					return false;
				}
				if (_plog)
					_plog->add(CLOG_DEFAULT_MOR, "ipc port(%u) connect from local", pl->_wport);
#endif
				if ((int)_map.size() + SIZE_RESERVED_NOFILE > get_nofile_lim_max()) {
#ifdef _WIN32
					shutdown(sAccept, SD_BOTH);
					::closesocket(sAccept);
#else
					shutdown(sAccept, SHUT_RDWR);
					::close(sAccept);
#endif
					_plog->add(CLOG_DEFAULT_ERR, "IPC server port(%d) error EMFILE reserved!", pl->_wport);
					return false;
				}
				tcpnodelay(sAccept);
				setkeepalive(sAccept);
				PNETSS pi = new session(listenid, nextid(), sAccept, EC_NET_SS_TCP, EC_NET_ST_CONNECT, _pmem, _plog);
				if (!pi) {
					::closesocket(sAccept);
					return false;
				}
				_map.set(pi->_ucid, pi);
				onconnect(listenid, pi->_ucid);
				return true;
			}

			void doread(uint32_t ucid, SOCKET fd)// return true need remake pollfds
			{
				char rbuf[SIZE_TCP_READ_ONCE];
#ifdef _WIN32
				int nr = ::recv(fd, rbuf, (int)sizeof(rbuf), 0);
#else
				int nr = ::recv(fd, rbuf, (int)sizeof(rbuf), MSG_DONTWAIT);
#endif
				if (nr == 0) { //close gracefully
					closeucid(ucid);
					if (_plog)
						_plog->add(CLOG_DEFAULT_MOR, "ucid(%u) disconnect gracefully", ucid);
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
					closeucid(ucid);
					if (_plog)
						_plog->add(CLOG_DEFAULT_MOR, "ucid(%u) disconnect error %d", ucid, nerr);
					return;
				}

				PNETSS pi = nullptr;
				if (!_map.get(ucid, pi) || pi->_status == EC_NET_ST_ATTACK)
					return;

				pi->_timelastio = ::time(0);
				bytes msgr(_pmem);
				msgr.reserve(1024 * 32);
				int ndo = pi->onrecvbytes((const uint8_t*)rbuf, nr, &msgr);
				if (ndo < 0) {
					closeucid(ucid);
					if (_plog)
						_plog->add(CLOG_DEFAULT_DBG, "ucid(%u) disconnect read error bytes", ucid);
					return;
				}

				const char* serr = nullptr;
				if (pi->_protoc == EC_NET_SS_TCP) { //update protocol
					int nup = update_basetcp(ucid, &pi, (uint8_t*)msgr.data(), msgr.size());
					if (nup < 0) {
						ndo = -1;
						serr = "update tcp_base protocol failed!";
						if (_plog) {
							char stmp[1024];
							int nsize = msgr.size() > 200u ? 200 : (int)msgr.size();
							_plog->add(CLOG_DEFAULT_DBG, "ucid(%u) update tcp_base protocol top200 %zu bytes from %s.\n%s", ucid, msgr.size(), pi->_ip,
								bin2view(msgr.data(), nsize, stmp, sizeof(stmp)));
						}
					}
					else if (1 == nup) {   //update success
						msgr.clear();
						ndo = pi->onrecvbytes(nullptr, 0, &msgr); // re parse bytes
					}
					else   //wait more bytes
						return;
				}
#if (0 != ECNETSRV_TLS)
				else if (pi->_protoc == EC_NET_SS_TLS) {
					int nup = update_basetls(ucid, &pi, (uint8_t*)msgr.data(), msgr.size());
					if (nup < 0) {
						ndo = -1;
						serr = "update tls_base protocol failed!";
						if (_plog) {
							char stmp[1024];
							int nsize = msgr.size() > 200u ? 200 : (int)msgr.size();
							_plog->add(CLOG_DEFAULT_DBG, "ucid(%u) update tls_base protocol top200 %zu bytes from %s.\n%s", ucid, msgr.size(), pi->_ip,
								bin2view(msgr.data(), nsize, stmp, sizeof(stmp)));
						}
					}
					else if (1 == nup) {   //update success
						msgr.clear();
						ndo = pi->onrecvbytes(nullptr, 0, &msgr); // re parse bytes
					}
					else   //wait more bytes
						return;
				}
#endif
				uint32_t srvid = pi->_listenid;
				while (ndo != -1 && msgr.size()) {
					if (!onmessage(pi->_listenid, ucid, pi->_protoc, msgr, nullptr)) {
						ndo = -1;
						serr = "do message failed!";
						break;
					}
					if (!_map.get(ucid, pi)) {
						pi = nullptr;
						break;
					}
					ndo = pi->onrecvbytes(nullptr, 0, &msgr);
					if (-1 == ndo)
						serr = "parse bytes failed!";
				}
				on_read_once_end(ucid, srvid);
				if (pi && -1 == ndo && pi->_status != EC_NET_ST_ATTACK) {
					closeucid(ucid);
					if (_plog)
						_plog->add(CLOG_DEFAULT_MOR, "ucid(%u) disconnected as %s", ucid, serr ? serr : "protocol error");
					return;
				}
			}

			int update_basetcp(uint32_t ucid, PNETSS *pi, const uint8_t* pu, size_t size) //base TCP protocol. return -2:not support; -1 :error ; 0:no; 1:ok
			{
				if (size < 3)
					return 0;
#ifdef _LOG_PROTOCOL_UPDATE
				if (_plog) {
					char stmp[2048];
					int nsize = size > 400u ? 400 : (int)size;
					_plog->add(CLOG_DEFAULT_DBG, "ucid(%u) update tcp_base protocol top400 %zu bytes:\n%s", ucid, size,
						_plog->bin2view(pu, nsize, stmp, sizeof(stmp)));
				}
#endif

#if (0 != ECNETSRV_TLS)
				if (hasprotocol((*pi)->_listenid, EC_NET_SS_TLS)
					&& pu[0] == 22 && pu[1] == 3 && pu[2] > 0 && pu[2] <= 3) { // update client TLS protocol
					if (!_ca._pcer.data() || !_ca._pcer.size() || !_ca._pRsaPub || !_ca._pRsaPrivate) {
						if (_plog)
							_plog->add(CLOG_DEFAULT_MOR, "ucid(%u) update TLS1.2 protocol failed, no server certificate", ucid);
						return -2;// not support
					}
					session_tls *pss = new session_tls(*pi, _ca._pcer.data(), _ca._pcer.size(),
						_ca._prootcer.data(), _ca._prootcer.size(), &_ca._csRsa, _ca._pRsaPrivate);
					if (!pss)
						return -1;
					*pi = pss;
					_map.set(ucid, *pi);
					if (_plog)
						_plog->add(CLOG_DEFAULT_MOR, "ucid(%u) update TLS1.2 protocol success", ucid);
					onprotocol(pss->session::_ucid, pss->_protoc);
					return 1;
				}
#endif
#if (0 != ECNETSRV_WS)
				if (hasprotocol((*pi)->_listenid, EC_NET_SS_WS)
					&& (ec::strineq("head", (const char*)pu, 4)
						|| ec::strineq("get", (const char*)pu, 3))
					) { //update http
					ec::http::package r;
					if (r.parse((const char*)pu, size) < 0)
						return -1;
					session_ws* pss = new session_ws(*pi, pu, size);
					if (!pss)
						return -1;
					*pi = pss;
					_map.set(ucid, *pi);
					if (_plog)
						_plog->add(CLOG_DEFAULT_MOR, "ucid(%u) update HTTP protocol success", ucid);
					onprotocol(pss->base_ws::_ucid, pss->_protoc);
					return 1;
				}
#endif
				return on_update_basetcp(ucid, pi, pu, size);
			}

#if (0 != ECNETSRV_TLS)
			int update_basetls(uint32_t ucid, PNETSS* pi, const uint8_t* pu, size_t size) //update base TLS protocol. return -1 :error ; 0:no; 1:ok
			{
				if (size < 3)
					return 0;
#ifdef _LOG_PROTOCOL_UPDATE
				if (_plog) {
					char stmp[2048];
					int nsize = size > 400u ? 400 : (int)size;
					_plog->add(CLOG_DEFAULT_DBG, "ucid(%u) update tls_base protocol top400 %zu bytes:\n%s", ucid, size,
						_plog->bin2view(pu, nsize, stmp, sizeof(stmp)));
				}
#endif

#if (0 != ECNETSRV_WSS)
				if (hasprotocol((*pi)->_listenid, EC_NET_SS_WSS)
					&& (ec::strineq("head", (const char*)pu, 4)
						|| ec::strineq("get", (const char*)pu, 3))
					) { //update https
					ec::http::package r;
					if (r.parse((const char*)pu, size) < 0)
						return -1;
					session_wss* pss = new session_wss((session_tls *)(*pi), pu, size);
					if (!pss)
						return -1;
					*pi = pss;
					_map.set(ucid, *pi);
					if (_plog)
						_plog->add(CLOG_DEFAULT_MOR, "ucid(%u) update HTTPS protocol success", ucid);
					onprotocol(pss->base_ws::_ucid, pss->_protoc);
					return 1;
				}
#endif
				return on_update_basetls(ucid, pi, pu, size);
			}
#endif
		};
	}//namespace net
}//namespace ec
