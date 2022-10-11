/*!
\file ec_netsrv_tls.h
\author	jiangyong
\email  kipway@outlook.com
\update 2022.8.4

net::session_tls
	TLS1.2 session.

	Supported cipher suites:
	CipherSuite TLS_RSA_WITH_AES_128_CBC_SHA256 = { 0x00,0x3C };
	CipherSuite TLS_RSA_WITH_AES_256_CBC_SHA256 = { 0x00,0x3D };
	CipherSuite TLS_RSA_WITH_AES_128_CBC_SHA = {0x00,0x2F};
	CipherSuite TLS_RSA_WITH_AES_256_CBC_SHA = {0x00,0x35};

eclib 3.0 Copyright (c) 2017-2022, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/

#pragma once
#include <mutex>
#include "ec_tls12.h"
#include "ec_netss_base.h"

namespace ec
{
	namespace net
	{
		class session_tls : public session
		{
		protected:
			ec::tls::sessionserver _tls;
		public:
			/*!
			\brief construct for update session
			*/
			session_tls(session&& ss, const void* pcer, size_t cerlen,
				const void* pcerroot, size_t cerrootlen, std::mutex *pRsaLck, RSA* pRsaPrivate) :
				session(std::move(ss)),
				_tls(ss._ucid, pcer, cerlen, pcerroot, cerrootlen, pRsaLck, pRsaPrivate, ss._pssmem, ss._psslog)
			{
				_tls.appendreadbytes(_rbuf.data_(), _rbuf.size_());
				_rbuf.free();
				_protoc = EC_NET_SS_TLS;
				_status = EC_NET_ST_CONNECT;
				_tls.SetIP(session::_ip);
			}

			/*!
			\brief construct for move session
			*/
			session_tls(session_tls&& ss) : session(std::move(ss)), _tls(std::move(ss._tls))
			{
			}
		public:
			virtual void setip(const char* sip)
			{
				net::session::setip(sip);
				_tls.SetIP(sip);
			};

			// pmsgout save appdata
			virtual int onrecvbytes(const void* pdata, size_t size, bytes* pmsgout)
			{
				int nr = -1;
				pmsgout->clear();
				int nst = _tls.OnTcpRead(pdata, size, pmsgout);
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
							return TLS_SESSION_ERR;
					}
					_status = EC_NET_ST_WORK;
					return onrecvbytes(nullptr, 0, pmsgout);//继续解析数据。
				}
				else if (TLS_SESSION_APPDATA == nst) {
					nr = 0;
				}
				return nr;
			}

			virtual int send(const void* pdata, size_t size, int timeoutmsec = 1000)
			{
				bytes tlspkg(_pssmem);
				tlspkg.reserve(size + 1024 - size % 1024);
				if (_tls.MakeAppRecord(&tlspkg, pdata, size))
					return iosend(tlspkg.data(), (int)tlspkg.size());
				return -1;
			}
		};
	}//namespace net
}//namespace ec