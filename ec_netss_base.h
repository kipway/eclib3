/*!
\file ec_netsrv_base.h
\author	jiangyong
\email  kipway@outlook.com
\update 2022.8.4

session class for net::server.

net::session
	base class

UCID：Unique ID for the session
1-49 listen ID
50-999 connect out session ID
>=1000 connect in session ID

eclib 3.0 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/
#pragma once

#include <stdint.h>

#ifdef _WIN32
#	include <windows.h>
#	include <Winsock2.h>
#	include <mstcpip.h>
#	include <ws2tcpip.h>

#	ifndef pollfd
#		define  pollfd WSAPOLLFD
#   endif

#else
#	define USE_AFUNIX 1
#	include <unistd.h>

#	include <sys/time.h>
#	include <sys/types.h>
#	include <sys/socket.h>
#	include <sys/eventfd.h>
#	include <sys/ioctl.h>

#	include <errno.h>
#	include <poll.h>
#	include <fcntl.h>
#endif

#include "ec_crc.h"
#include "ec_string.h"
#include "ec_log.h"
#include "ec_netio.h"
#include "ec_map.h"
#include "ec_stream.h"

#define EC_NET_ST_ATTACK (-1)// session status attack
#define EC_NET_ST_PRE     0  // session status preparation
#define EC_NET_ST_CONNECT 1  // session status connected
#define EC_NET_ST_WORK    2  // session status work

#define EC_NET_SS_NONE   0x0000 // session none defined

#define EC_NET_SS_LISTEN 0x0001 // session protocol for listen
#define EC_NET_SS_TCP    0x1000 // session protocol base TCP
#define EC_NET_SS_HTTP   0x1100 // session protocol base HTTP
#define EC_NET_SS_WS     0x1200 // session protocol base websocket

#define EC_NET_SS_TLS    0x2000 // session protocol base TLS 1.2
#define EC_NET_SS_HTTPS  0x2100 // session protocol base HTTPS(TLS 1.2)
#define EC_NET_SS_WSS    0x2200 // session protocol base websocket(TLS 1.2)

#define EC_NET_SS_CONOUT 0x4000 // session protocol base connect out

#ifndef NET_SENDBUF_MAXSIZE // Maximum PKG Protocol packet size
#	define NET_SENDBUF_MAXSIZE (1024 * 1024 * 64)
#endif

#ifndef EC_NET_SEND_BLOCK_OVERSECOND // Maximum blocking time(seconds)
#	define EC_NET_SEND_BLOCK_OVERSECOND 10
#endif

#define EC_NET_LISTENID 1   // listen start id first TCP is 1 ,1-49 srvid = ucid
#define EC_NET_CONOUTID 50  // connect out start id

#define UCID_DYNAMIC_START 1000 // Dynamic start ID

#ifndef EC_NET_SNDBUF_BLOCKSIZE
#define EC_NET_SNDBUF_BLOCKSIZE (1024 * 32)
#endif

#ifndef EC_NET_SNDBUF_HEAPSIZE
#define EC_NET_SNDBUF_HEAPSIZE (1024 * 1024 * 8) // 8M heap size, grow by heap
#endif

namespace ec
{
	namespace net
	{
		class evtfd // udp event used to stop poll/Wsapoll wait, linux use eventfd
		{
		public:
			evtfd()
			{
				_fd = INVALID_SOCKET;
				memset(&_sinaddr, 0, sizeof(_sinaddr));
			}
			~evtfd()
			{
				close();
			}
			bool open()
			{
#ifdef _WIN32
				uint16_t port = (uint16_t)50000;
				SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
				if (s == INVALID_SOCKET)
					return false;
				long ul = 1; // set none block
				if (SOCKET_ERROR == ioctlsocket(s, FIONBIO, (unsigned long*)&ul)) {
					::closesocket(s);
					return false;
				}
				memset(&_sinaddr, 0, sizeof(_sinaddr));
				_sinaddr.sin_family = AF_INET;
				_sinaddr.sin_addr.s_addr = inet_addr("127.0.0.192");
				_sinaddr.sin_port = htons(port);
				while (SOCKET_ERROR == bind(s, (struct sockaddr *)&_sinaddr, (int)sizeof(_sinaddr))) {
					port--;
					_sinaddr.sin_port = htons(port);
					if (port < 9000) {
						::closesocket(s);
						return false;
					}
				}
				_fd = s;
				return true;
#else
				_fd = eventfd(1, EFD_NONBLOCK);
				return _fd >= 0;
#endif
			}
			void close()
			{
				if (_fd != INVALID_SOCKET) {
					::closesocket(_fd);
					_fd = INVALID_SOCKET;
				}
			}
			bool set_event()
			{
				if (_fd != INVALID_SOCKET) {
					uint64_t evt = 1;
#ifdef _WIN32
					return ::sendto(_fd, (char*)&evt, 8, 0, (struct sockaddr*)&_sinaddr, (socklen_t)sizeof(_sinaddr)) > 0;//send to itself
#else
					return ::write(_fd, &evt, sizeof(uint64_t)) > 0;
#endif
				}
				return false;
			}
			bool reset_event()
			{
				if (_fd != INVALID_SOCKET) {
					uint64_t evt = 0;
#ifdef _WIN32
					return ::recv(_fd, (char*)&evt, (int)sizeof(evt), 0) > 0;
#else
					return ::read(_fd, &evt, (int)sizeof(evt)) > 0;
#endif
				}
				return false;
			}
			inline SOCKET getfd()
			{
				return _fd;
			}
		private:
			SOCKET   _fd; //Non-block
			struct sockaddr_in _sinaddr;
		};

		class ssext_data // application session extension data
		{
		public:
			ssext_data() {
			}
			virtual ~ssext_data() {
			}
			virtual const char* classname() {
				return "ssext_data";
			}
		};

		class session // base connect session class
		{
		public:
			session& operator = (const session& v) = delete;
			session(const session& v) = delete;
			session(uint32_t listenid, uint32_t ucid, SOCKET  fd, uint32_t protoc, uint32_t status, memory* pmem, ilog* plog, blk_alloctor<>* pblkallocator) :
				_listenid(listenid), _protoc(protoc), _status(status), _fd(fd), _ucid(ucid)
				, _timesndblcok(0), _time_err(0)
				, _pause_r(0), _peerport(0), _rbuf(pmem), _pextdata(nullptr)
				, _pssmem(pmem), _psslog(plog)
				,_sndbuf(NET_SENDBUF_MAXSIZE, pblkallocator)
			{
				_ip[0] = 0;
				_timelastio = ::time(0);
				_timeconnect = ec::mstime();
			}
			/*
			session(session* p) : _listenid(p->_listenid), _protoc(p->_protoc), _status(p->_status), _fd(p->_fd), _ucid(p->_ucid)
				, _timelastio(p->_timelastio), _timesndblcok(p->_timesndblcok), _time_err(p->_time_err)
				, _pause_r(p->_pause_r), _peerport(p->_peerport), _timeconnect(p->_timeconnect), _pextdata(p->_pextdata)
				, _pssmem(p->_pssmem), _psslog(p->_psslog), _rbuf(std::move(p->_rbuf))
				, _sndbuf(std::move(p->_sndbuf))
			{
				memcpy(_ip, p->_ip, sizeof(_ip));
				p->_fd = INVALID_SOCKET;//move
				p->_pextdata = nullptr; //move
			}*/

			session(session&& v) noexcept
				: _rbuf(std::move(v._rbuf)), _sndbuf(std::move(v._sndbuf))
			{
				_listenid = v._listenid;
				_protoc = v._protoc;
				_status = v._status;
				_fd = v._fd;
				_ucid = v._ucid;
				_timelastio = v._timelastio;
				_timesndblcok = v._timesndblcok;
				_time_err = v._time_err;
				_pause_r = v._pause_r;
				_peerport = v._peerport;
				_timeconnect = v._timeconnect;
				_pextdata = v._pextdata;
				_pssmem = v._pssmem;
				_psslog = v._psslog;

				memcpy(_ip, v._ip, sizeof(_ip));
				v._fd = INVALID_SOCKET;//move
				v._pextdata = nullptr; //move
			}

			virtual ~session()
			{
#ifdef _DEBUG
				if (_psslog && _fd != INVALID_SOCKET) {
					if (_ucid < EC_NET_CONOUTID)
						_psslog->add(CLOG_DEFAULT_DBG, "ec::net::~session(), free _fd = %XH, server ucid = %u", (size_t)_fd, _ucid);

					else if (_ucid >= EC_NET_CONOUTID && _ucid < UCID_DYNAMIC_START)
						_psslog->add(CLOG_DEFAULT_DBG, "ec::net::~session(), free _fd = %XH, connect out protocol = %XH,ucid = %u", (size_t)_fd, _protoc, _ucid);

					else
						_psslog->add(CLOG_DEFAULT_DBG, "ec::net::~session(), free _fd = %XH, connect in protocol = %XH, from ip = %s, ucid = %u",
						(size_t)_fd, _protoc, _ip, _ucid);
				}
#endif
				closefd();
				if (_pextdata) {
					if (_psslog)
						_psslog->add(CLOG_DEFAULT_DBG, "ec::net::~session(), free extdata = %s, ucid = %u", _pextdata->classname(), _ucid);
					delete _pextdata;
					_pextdata = nullptr;
				}
			}
			void closefd()
			{
				if (_fd != INVALID_SOCKET) {
#ifdef _WIN32
					shutdown(_fd, SD_BOTH);
					::closesocket(_fd);
#else
					shutdown(_fd, SHUT_RDWR);
					::close(_fd);
#endif
					_fd = INVALID_SOCKET;
				}
			}
			void setextdata(ssext_data* pdata)
			{
				if (_pextdata)
					delete _pextdata;
				_pextdata = pdata;
			}

			template<class _ClsPtr>
			bool getextdata(const char* clsname, _ClsPtr& ptr)
			{
				if (!_pextdata || (clsname && stricmp(clsname, _pextdata->classname())))
					return false;
				ptr = (_ClsPtr)_pextdata;
				return true;
			}
		public:
			uint32_t _listenid;// the listen port seesion ucid[EC_NET_LISTENID,EC_NET_CONOUTID)
			uint32_t _protoc;  // session protocol
			int32_t  _status;  // session status
			SOCKET   _fd;
			uint32_t _ucid;    // Unique serial number
			char     _ip[40];  // Peer IP address
			time_t   _timelastio;  // time last receive/send
			time_t   _timesndblcok;// Send blocking start time
			time_t   _time_err;
			int      _pause_r; // 1:pause read; 0: normal read
			uint16_t _peerport;// peerport
			int64_t  _timeconnect; // GMT from 1970-1-1 millisecond(1/1000 second)
			ec::parsebuffer  _rbuf;
		private:
			ssext_data* _pextdata; //application session extension data
		protected:
			memory* _pssmem; // Memory allocator
			ilog*   _psslog;
			friend class session_tls;
			friend class session_ws;
			friend class session_wss;
			friend class base_ws;

#ifdef EC_SS_EXTCLASS1
			friend class EC_SS_EXTCLASS1;
#endif
#ifdef EC_SS_EXTCLASS2
			friend class EC_SS_EXTCLASS2;
#endif
#ifdef EC_SS_EXTCLASS3
			friend class EC_SS_EXTCLASS3;
#endif
#ifdef EC_SS_EXTCLASS4
			friend class EC_SS_EXTCLASS4;
#endif
#ifdef EC_SS_EXTCLASS5
			friend class EC_SS_EXTCLASS5;
#endif
#ifdef EC_SS_EXTCLASS6
			friend class EC_SS_EXTCLASS6;
#endif
		private:
			ec::io_buffer<> _sndbuf;
		public:
			/*!
			\brief do receive bytes
			\param pdata [in] received byte stream
			\param size [in] received byte stream size(bytes)
			\param pmsgout [out] application layer message parsed from byte stream
			\return 0: ok ; -1: error need close
			\remark if has not received enough data bytes for a message, return 0 and pmsgout is set empty.
			*/
			virtual int onrecvbytes(const void* pdata, size_t size, bytes* pmsgout)
			{
				pmsgout->clear();
				pmsgout->append(pdata, size);
				_timelastio = ::time(0);
				return 0;
			};

			/*！
			\brief send application layer message
			\param pdata [in] message byte stream
			\param size [in] pdata size(bytes)
			\return return -1:error ; >=0 the number of bytes actually sent, and the rest is added to the send buffer.
			*/
			virtual int send(const void* pdata, size_t size)
			{
				return iosend(pdata, size);
			}

			/*！
			\brief IO send Non-blocking
			\param pdata [in] data
			\param size [in] pdata size(bytes)
			\return return -1:error ; >=0 the number of bytes actually sent, and the rest is added to the send buffer.
			*/

			int iosend(const void* pkg, size_t pkgsize)
			{
				if (_sndbuf.empty()) {
					int ns = send_non_block(_fd, pkg, (int)pkgsize);
					if (ns < 0)
						return -1;
					return _sndbuf.append((const uint8_t*)pkg + ns, pkgsize - ns) ? (int)pkgsize : -1;
				}
				return (!_sndbuf.append((const uint8_t*)pkg, pkgsize) || sendbuf() < 0) ? -1 : (int)pkgsize;
			}

			int  sendbuf() // return -1 error; >= 0 send size
			{
				if (_sndbuf.empty())
					return 0;
				int nr = 0, ns;
				const void* pd = nullptr;
				size_t zlen = 0;

				pd = _sndbuf.get(zlen);
				while (pd && zlen) {
					ns = send_non_block(_fd, pd, (int)(zlen));
					if (ns < 0)
						return -1;
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

			inline size_t sndbufsize()// Returns the number of bytes in the send buffer
			{
				return _sndbuf.size();
			}

			bool sndbufempty() {
				return _sndbuf.empty();
			}
			virtual void setip(const char* sip)
			{
				strlcpy(_ip, sip, sizeof(_ip));
			};
		};

		/*!
		\breif base clase for listen port session
		*/
		class listen_session : public session
		{
		public:
			listen_session(uint32_t ucid, SOCKET  fd, memory* pmem, ilog* plog, blk_alloctor<>* pblkallocator) :
				session(ucid, ucid, fd, EC_NET_SS_LISTEN, EC_NET_ST_WORK, pmem, plog, pblkallocator)
			{
			}

			virtual int onrecvbytes(const void* pdata, size_t size, bytes* pmsgout)
			{
				pmsgout->clear();
				_timelastio = ::time(0);
				return 0;
			}
		};

		/*!
		\brief session class for udp server
		*/
		class session_udpsrv : public session
		{
		public:
			session_udpsrv(uint32_t ucid, SOCKET  fd, memory* pmem, ilog* plog, blk_alloctor<>* pblkallocator) :
				session(ucid, ucid, fd, EC_NET_SS_LISTEN, EC_NET_ST_PRE, pmem, plog, pblkallocator)
			{
			}
		public:
			virtual int onrecvbytes(const void* pdata, size_t size, bytes* pmsgout)
			{
				_timelastio = ::time(0);
				pmsgout->clear();
				pmsgout->append(pdata, size);
				return 0;
			};

			virtual int send(const void* pdata, size_t size)
			{
				return 0;
			}
		};

		typedef session* PNETSS;
		struct keq_netss {
			bool operator()(uint32_t key, const PNETSS &val)
			{
				return key == val->_ucid;
			}
		};
		struct del_netss {
			void operator()(PNETSS& val)
			{
				if (val) {
					delete val;
					val = nullptr;
				}
			}
		};
	}//namespace net
}//namespace ec
