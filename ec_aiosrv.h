/*
* @file ec_aiosrv.h
* @brief a new net server ,IOCP in windows, EPOLL in linux
* 
* @author jiangyong
* 
* class ec::aio::netserver
* 
* update 2023-2-8 first version
* 
* eclib 3.0 Copyright (c) 2017-2023, kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/

#pragma once

#ifdef _WIN32
#include "ec_netiocp.h"
#else
#include "ec_netepoll.h"
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

namespace ec {
	namespace aio {
#ifdef _WIN32
		using netserver_ = serveriocp_;
#else
		using netserver_ = serverepoll_;
#endif
		class netserver : public netserver_
		{
		protected:
			ec::blk_alloctor<> _sndbufblks; //共享发送缓冲分配区
			ec::hashmap<int, psession, kep_session, del_session > _mapsession;//会话连接
#if (0 != EC_AIOSRV_TLS)
			tls::srvca _ca;  // certificate
#endif
		public:
			netserver(ec::memory* piomem, ec::ilog* plog) : netserver_(piomem, plog)
				, _sndbufblks(EC_AIO_SNDBUF_BLOCKSIZE - EC_ALLOCTOR_ALIGN, EC_AIO_SNDBUF_HEAPSIZE / EC_AIO_SNDBUF_BLOCKSIZE)
			{
			}
			virtual ~netserver() {
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
			void runtime(int waitmsec, int64_t& currentmsec)
			{
				timerjob(currentmsec);//定时任务，维持上游连接和发送心跳消息
				netserver_::runtime_(waitmsec);
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

			/**
			 * @brief 经session编码打包后提交到发送缓冲,如果可能会立即发送。
			 * @param fd 
			 * @param pdata 
			 * @param size 
			 * @return >=0 实际发送的字节数(0 可能小于size，剩下的提交到发送缓冲) ; -1: 发送错误，断开并回收连接，并调用 onDisconnected
			*/
			int sendtofd(int fd, const void* pdata, size_t size)
			{
				psession pss = nullptr;
				if (!_mapsession.get(fd, pss))
					return -1;
				if(pss->sendasyn(pdata, size, _plog) < 0)
					return -1;
				return postsend(fd);
			}

			/**
			 * @brief 异步连接, 会建立一个默认的tcp session, 连接成功后会使用onTcpConnectOut通知
			 * @return 返回keyfd
			*/
			int tcpconnect(uint16_t port, const char* sip)
			{
				ec::net::socketaddr netaddr;
				if (netaddr.set(port, sip) < 0)
					return -1;
				int addrlen = 0;
				struct sockaddr* paddr = netaddr.getsockaddr(&addrlen);
				if (!paddr)
					return -1;
				
				int fd = connect_asyn(paddr, addrlen);
				if (fd < 0){
					_plog->add(CLOG_DEFAULT_ERR, "connect tcp://%s:%u failed.", netaddr.viewip(), port);
					return -1;
				}
				psession pss = new session(&_sndbufblks, fd, _piomem);
				if (!pss) {
					_plog->add(CLOG_DEFAULT_ERR, "new session memory error");
					close_(fd);
					return -1;
				}
#ifndef _WIN32
				if (epoll_add_tcpout(fd) < 0) {
					delete pss;
					return -1;
				}
#endif
				_mapsession.set(fd, pss);
				return fd;
			}

		protected:
			virtual void onprotocol(int fd, int nproco) {};

			/**
			 * @brief 处理消息,已经分包完成
			 * @param fd 虚拟fd
			 * @param sbuf 消息包
			 * @param msgtype 消息类型 EC_AIO_MSG_XXX defined in ec_aiosession.h
			 * @return 
			*/
			virtual int domessage(int fd, ec::bytes& sbuf, int msgtype) = 0;

#if (0 != EC_AIOSRV_TLS)
			virtual ec::tls::srvca* getCA(int fdlisten) {
				return &_ca;
			}
#endif
			virtual void timerjob(int64_t currentms)
			{
			}

			/**
			 * @brief 连接即将关闭，应用层有关联此kfd的做清理操作。
			 * @param kfd keyfd
			 * @remark 调用时fd还没有断开。
			*/
			virtual void onDisconnect(int kfd) {
			}

			virtual void onDisconnected(int fd)
			{
				_plog->add(CLOG_DEFAULT_DBG, "netserver::onDisconnected fd(%d)", fd);
				_mapsession.erase(fd);
			}
			
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

			/**
			 * @brief 接收数据
			 * @param kfd   keyfd
			 * @param pdata Received data
			 * @param size  Received data size
			 * @return 0:OK; -1:error，will be close 
			*/
			virtual int onReceived(int kfd, const void* pdata, size_t size)
			{
				psession pss = nullptr;
				if (!_mapsession.get(kfd, pss))
					return -1;
				ec::bytes msg(_piomem);
				int msgtype = pss->onrecvbytes(pdata, size, _plog, &msg);
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
					if (postsend(kfd) < 0)
						return -1;
					msg.clear();
					msgtype = pss->onrecvbytes(nullptr, 0, _plog, &msg);
				}
				if (msgtype == EC_AIO_MSG_ERR) {
					_plog->add(CLOG_DEFAULT_ERR, "fd(%d) read error message.", pss->_fd);
					return -1;
				}
				return postsend(kfd) < 0 ? -1 : 0;
			}

			/**
			 * @brief get session
			 * @param kfd keyfd
			 * @return nullptr or psession with kfd
			*/
			virtual psession getSession(int kfd)
			{
				psession ps = nullptr;
				if (!_mapsession.get(kfd, ps)) {
					return nullptr;
				}
				return ps;
			}

			virtual void onAccept(int fd, const char* sip, uint16_t port, int fdlisten)
			{
				_plog->add(CLOG_DEFAULT_DBG, "onAccept fd(%d), listenfd(%d)", fd, fdlisten);
				psession pss = new session(&_sndbufblks, fd, _piomem, fdlisten);
				if (!pss)
					return;
				pss->_status = EC_AIO_FD_CONNECTED;
				ec::strlcpy(pss->_peerip, sip, sizeof(pss->_peerip));
				pss->_peerport = port;
				_mapsession.set(fd, pss);
			}
		};
	}//namespace aio
}//namespace ec