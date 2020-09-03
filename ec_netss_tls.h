/*!
\file ec_netsrv_tls.h
\author	jiangyong
\email  kipway@outlook.com
\update 2020.9.2

eclib net server tls session class

class ec::net::session_tls;

eclib 3.0 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

简介：
tls1.2协议的接入会话。

密码套件支持4种：

CipherSuite TLS_RSA_WITH_AES_128_CBC_SHA256 = { 0x00,0x3C };
CipherSuite TLS_RSA_WITH_AES_256_CBC_SHA256 = { 0x00,0x3D };
CipherSuite TLS_RSA_WITH_AES_128_CBC_SHA = {0x00,0x2F};
CipherSuite TLS_RSA_WITH_AES_256_CBC_SHA = {0x00,0x35};
*/

#pragma once
#include <mutex>
#include "ec_tls12.h"
#include "ec_netss_base.h"

namespace ec
{
	namespace net
	{
		class session_tls : public net::session, public tls::sessionserver
		{
		public:
			/*!
			\brief construct for update session
			*/
			session_tls(net::session* ps, const void* pcer, size_t cerlen,
				const void* pcerroot, size_t cerrootlen, std::mutex *pRsaLck, RSA* pRsaPrivate) :
				net::session(ps),
				tls::sessionserver(ps->_ucid, pcer, cerlen, pcerroot, cerrootlen, pRsaLck, pRsaPrivate, ps->_pssmem, ps->_psslog)
			{
				_pkgtcp.append(ps->_rbuf.data(), ps->_rbuf.size());
				_protoc = EC_NET_SS_TLS;
				_status = EC_NET_ST_CONNECT;
				ps->_rbuf.clear();
				ps->_rbuf.shrink_to_fit();
			}

			/*!
			\brief construct for move session
			*/
			session_tls(session_tls* ps) : net::session(ps), tls::sessionserver(ps)
			{
				_pkgtcp.append(ps->_rbuf.data(), ps->_rbuf.size());
				_protoc = EC_NET_SS_TLS;
				_status = EC_NET_ST_CONNECT;
				ps->_rbuf.clear();
				ps->_rbuf.shrink_to_fit();
			}
		public:
			virtual void setip(const char* sip)
			{
				net::session::setip(sip);
				SetIP(sip);
			};

			virtual int onrecvbytes(const void* pdata, size_t size, bytes* pmsgout)
			{
				int nr = -1;
				pmsgout->clear();
				int nst = OnTcpRead(pdata, size, pmsgout);
				if (TLS_SESSION_ERR == nst || TLS_SESSION_OK == nst || TLS_SESSION_NONE == nst) {
					nr = TLS_SESSION_ERR == nst ? -1 : 0;
					if (pmsgout->size()) {
						if (iosend(pmsgout->data(), (int)pmsgout->size()) < 0)
							nr = -1;
					}
					pmsgout->clear();
					return nr;
				}
				else if (TLS_SESSION_HKOK == nst) {
					nr = 0;
					if (pmsgout->size()) {
						if (iosend(pmsgout->data(), (int)pmsgout->size()) < 0)
							nr = -1;
					}
					if (nr != -1)
						_status = EC_NET_ST_WORK;
					pmsgout->clear();
					return nr;
				}
				else if (TLS_SESSION_APPDATA == nst)
					nr = 0;
				return nr;
			}

			virtual int send(const void* pdata, size_t size, int timeoutmsec = 1000)
			{
				bytes tlspkg(_pmem);
				tlspkg.reserve(size + 1024 - size % 1024);
				if (MakeAppRecord(&tlspkg, pdata, size))
					return iosend(tlspkg.data(), (int)tlspkg.size());
				return -1;
			}
		};
	}//namespace net
}//namespace ec