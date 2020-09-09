/*!
\file ec_wssclient.h
\author	jiangyong
\email  kipway@outlook.com
\update 2020.9.6

wss_c
	a Client websocket class based on HTTPS (TLS1.2)
	tcp_c -> tls_c -> wss_c

eclib 3.0 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/
#pragma once
#include "ec_tlsclient.h"
#include "ec_wsclient.h"

namespace ec
{
	class wss_c : public tls_c
	{
	public:
		wss_c(memory* pmem, ilog* plog) : tls_c(pmem, plog)
			, _nstatus(0)
			, _rbuf(pmem)
			, _ws(pmem, plog)

		{
			_rbuf.reserve(1024 * 4);
		}

		int get_ws_status()
		{
			return _nstatus;
		}
	private:
		int _nstatus; // 0: none handshaked;  1:handshaked
		bytes _rbuf;
		websocketclient _ws;
	protected:
		virtual void onwshandshake() = 0;
		virtual void onwsdata(const uint8_t* p, int nbytes) = 0;
	public:
		inline void initws(const char* srequrl, const char* shost, const char* sprotocol)
		{
			_ws.init(srequrl, shost, sprotocol);
		}

		virtual int sendbytes(const void* p, int nlen)
		{
			if (!_nstatus)
				return tls_c::sendbytes(p, nlen);
			bytes vs(_pmem);
			vs.reserve(1024 + nlen - nlen % 512);
			if (_ws.makeWsPackage(p, nlen, &vs) < 0) {
				close();
				return -1;
			}
			return tls_c::sendbytes(vs.data(), (int)vs.size());
		}
	protected:
		virtual void ontlshandshake()
		{
			bytes pkg(_pmem);
			pkg.reserve(1024 * 4);
			_ws.makeRequest(pkg);
			_rbuf.clear();
			tls_c::sendbytes(pkg.data(), (int)pkg.size());
		}

		virtual void ondisconnected()
		{
			tls_c::ondisconnected();
			_nstatus = 0;
		}

		virtual void ontlsdata(const uint8_t* p, int nbytes)
		{
			int nr = 0;
			bytes pkg( _pmem);
			pkg.reserve(1024 * 16);
			_rbuf.append(p, nbytes);
			if (!_nstatus) {
				nr = _ws.doRequest(_rbuf, pkg);
				if (nr < 0) {
					close();
					return;
				}
				else if (nr == 0)
					return;
				_nstatus = 1;
				onwshandshake();
				if (!_rbuf.size())
					return;
			}
			pkg.clear();
			nr = _ws.doWsData(_rbuf, &pkg);
			if (nr < 0) {
				close();
				_nstatus = 0;
				return;
			}
			while (pkg.size()) {
				onwsdata(pkg.data(), (int)pkg.size());
				pkg.clear();
				nr = _ws.doWsData(_rbuf, &pkg);
				if (nr < 0) {
					close();
					_nstatus = 0;
					return;
				}
			}
			_rbuf.shrink_to_fit();
		}
	};
}