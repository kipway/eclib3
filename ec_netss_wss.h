/*!
\file ec_netsrv_wss.h
\author	jiangyong
\email  kipway@outlook.com
\update 2022.8.4

net::session_wss
	websocket over HTTPS(TLS1.2) session

eclib 3.0 Copyright (c) 2017-2018, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/
#pragma once

#include "ec_netss_tls.h"
#include "ec_netss_ws.h"

namespace ec
{
	namespace net
	{
		/*!
		\brief session class for https and websocket base TLS1.2
		*/
		class session_wss : public session_tls, public base_ws
		{
		public:
			/*!
			\brief construct for update session
			*/
			session_wss(session_tls&& ss) : session_tls(std::move(ss)),
				base_ws(session_tls::_ucid, _pssmem, _psslog)
			{
				_protoc = EC_NET_SS_HTTPS;
			}
		protected:
			virtual int ws_iosend(const void* pdata, size_t size)
			{
				return session_tls::send(pdata, size);
			};

			virtual void onupdatews()
			{
				_protoc = EC_NET_SS_WSS; //update websocket
				_status = EC_NET_ST_WORK;
			}
		public:
			virtual int onrecvbytes(const void* pdata, size_t size, bytes* pmsgout)
			{
				_timelastio = ::time(nullptr);
				bytes data(_pssmem);
				data.reserve(1024 * 20);
				int nr = session_tls::onrecvbytes(pdata, size, &data);
				if (nr < 0)
					return nr;
				return ws_onrecvbytes(data.data(), data.size(), pmsgout, _rbuf);
			};

			virtual int send(const void* pdata, size_t size)
			{
				return ws_send(pdata, size);
			}
		};//session_wss
	}//namespace net
}//namespace ec