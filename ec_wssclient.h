/*!
\file ec_wssclient.h
\author	jiangyong
\email  kipway@outlook.com
\update
  2023.7.5  remove ec::memory
  2023.6.25 add sendPingMsgMsg

wss_c
	a Client websocket class based on HTTPS (TLS1.2)
	tcp_c -> tls_c -> wss_c

eclib 3.0 Copyright (c) 2017-2021, kipway
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
		wss_c(ilog* plog) : tls_c(plog) , _nstatus(0) , _ws(plog)
		{
		}

		int get_ws_status()
		{
			return _nstatus;
		}
	private:
		int _nstatus; // 0: none handshaked;  1:handshaked
		parsebuffer _rbuf;
		websocketclient _ws;
	protected:
		virtual void onwshandshake() = 0;
		virtual int onwsdata(const uint8_t* p, int nbytes) = 0;//return 0:OK ; -1:error will disconnect;
	public:
		inline void initws(const char* srequrl, const char* shost, const char* sprotocol)
		{
			_ws.init(srequrl, shost, sprotocol);
		}

		virtual int sendbytes(const void* p, int nlen)
		{
			if (!_nstatus)
				return tls_c::sendbytes(p, nlen);
			vstream vs;
			vs.reserve(1024 + nlen - nlen % 512);
			if (_ws.makeWsPackage(p, nlen, &vs) < 0) {
				close();
				return -1;
			}
			return tls_c::sendbytes(vs.data(), (int)vs.size());
		}

		/**
		 * @brief send ping messgae
		 * @param sutf8 text to send
		 * @return -1: error; 0:none handshaked; >0 package size sended;
		*/
		int sendPingMsg(const char* sutf8)
		{
			if (!_nstatus)
				return 0;
			const char* s = (sutf8 && *sutf8) ? sutf8 : "ping";
			vstream vs;
			if (_ws.makeWsPackage(s, strlen(s), &vs, WS_OP_PING) < 0) {
				close();
				return -1;
			}
			return tls_c::sendbytes(vs.data(), (int)vs.size());
		}
	protected:
		virtual void ontlshandshake()
		{
			bytes pkg;
			pkg.reserve(500);
			_ws.makeRequest(pkg);
			tls_c::sendbytes(pkg.data(), (int)pkg.size());
		}

		virtual void ondisconnected()
		{
			tls_c::ondisconnected();
			_nstatus = 0;
			_rbuf.free();
		}

		virtual void ontlsdata(const uint8_t* p, int nbytes)
		{
			int nr = 0, nopcode = 0;
			bytes pkg;
			_rbuf.append(p, nbytes);
			if (!_nstatus) {
				nr = _ws.doRequest(_rbuf);
				if (nr < 0) {
					close();
					return;
				}
				else if (nr == 0)
					return;
				_nstatus = 1;
				onwshandshake();
				if (_rbuf.empty())
					return;
			}
			nr = _ws.doWsData(_rbuf, &pkg, &nopcode);
			while (1 == nr) {
				if (WS_OP_PING == nopcode) {
					if (sendwsbytes(pkg.data(), (int)pkg.size(), WS_OP_PONG) < 0)
						return;
				}
				else {
					nr = onwsdata(pkg.data(), (int)pkg.size());
					if (nr < 0)
						break;
				}
				pkg.clear();
				nopcode = 0;
				nr = _ws.doWsData(_rbuf, &pkg, &nopcode);
			}
			if (nr < 0) {
				close();
				_nstatus = 0;
			}
		}

		int sendwsbytes(const void* p, int nlen, int opcode)
		{
			vstream vs;
			vs.reserve(1024 + nlen - nlen % 512);
			if (_ws.makeWsPackage(p, nlen, &vs, opcode) < 0) {
				close();
				return -1;
			}
			return tls_c::sendbytes(vs.data(), (int)vs.size());
		}
	};
}