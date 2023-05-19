/*!
\file ec_netsrv_ws.h
\author	jiangyong
\email  kipway@outlook.com
\update 2023.5.13
2023.5.13 remove ec::memory
net server http/ws session class

net::base_ws
	base websocket session

net::session_ws
	http/ws session

eclib 3.0 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/
#pragma once
#include "ec_base64.h"
#include "ec_sha1.h"
#include "ec_netss_base.h"
#include "ec_http.h"
#include "ec_wstips.h"
#include "ec_diskio.h"

namespace ec
{
	namespace net
	{
		class base_ws
		{
		public:
			base_ws(const base_ws&) = delete;
			base_ws& operator = (const base_ws&) = delete;

			base_ws(uint32_t ucid, ilog* plog) : _ucid(ucid), _nws(0), _wscompress(0),
				_comp(0), _opcode(WS_OP_TXT), _pwslog(plog)
			{
			}
		public:
			uint32_t _ucid;
			int _nws; // 0: http ; 1:ws
			int _wscompress; // ws_x_webkit_deflate_frame or ws_permessage_deflate
			bytes _wsmsg; // ws frame
			int _comp;// compress flag
			int _opcode;  // operate code
			ilog* _pwslog;
		protected:
			virtual int ws_iosend(const void* pdata, size_t size) = 0;
			virtual void onupdatews() = 0;
		protected:

			/*!
			\brief do received data bytes
			\param pdata [in] received data
			\param size [in] received data bytes
			\param pmsgout [out] output http Message
			\return -1:error; 0:success
			\remark if return 0 and pmsgout->size()==0, no http message, Waiting for more data。
			pmsgout byte0=0:http; 1:ws;  byte1 = opcode; byte2-byte7:res 0; byte8-n http/websocket(Indicated in _nws) message
			*/
			int ws_onrecvbytes(const void* pdata, size_t size, bytes* pmsgout, ec::parsebuffer &rbuf)
			{
				pmsgout->clear();
				int nr = DoReadData((const char*)pdata, size, pmsgout, rbuf);
				if (he_failed == nr)
					return -1;
				else if (he_ok == nr) {
					const uint8_t* pu = pmsgout->data();
					if (PROTOCOL_WS == pu[0]) {
						if (WS_OP_PING == pu[1]) {
							ws_send(pu + 8, pmsgout->size() - 8u, WS_OP_PONG);
							pmsgout->clear();
						}
						else if (WS_OP_PONG == pu[1]) {
							pmsgout->clear();
						}
					}
					return 0;
				}
				pmsgout->clear();
				return 0;
			};

			int ws_send(const void* pdata, size_t size, int optcode = WS_OP_TXT) // return -1:error
			{
				if (!_nws)
					return ws_iosend(pdata, size);
				else if (1 == _nws) {
					bool bsend;
					vstream vret;
					if (_wscompress == ws_x_webkit_deflate_frame) { //deflate-frame
						bsend = ws_make_perfrm(pdata, size, optcode, &vret);
					}
					else { // ws_permessage_deflate
						bsend = ws_make_permsg(pdata, size, optcode, &vret, (size > 128 && _wscompress) ? 1 : 0);
					}
					if (!bsend) {
						if (_pwslog)
							_pwslog->add(CLOG_DEFAULT_ERR, "send ucid(%u) make wsframe failed,size %u", _ucid, (unsigned int)size);
						return -1;
					}
					return ws_iosend(vret.data(), vret.size());
				}
				if (_pwslog)
					_pwslog->add(CLOG_DEFAULT_ERR, "ws send failed _protocol = %d", _nws);
				return -1;
			}

			bool DoUpgradeWebSocket(const char* skey, ec::http::package* pPkg)
			{
				try {
					if (_pwslog) {
						char stmp[128] = { 0 };
						str1k sa;
						if (pPkg->GetHeadFiled("Origin", stmp, sizeof(stmp))) {
							sa.append("\n\tOrigin: ").append(stmp);
						}
						if (pPkg->GetHeadFiled("Sec-WebSocket-Extensions", stmp, sizeof(stmp))) {
							sa.append("\n\tSec-WebSocket-Extensions: ").append(stmp);
						}
						_pwslog->add(CLOG_DEFAULT_MOR, "ucid(%u) upgrade websocket%s", _ucid, sa.c_str());
					}
					char sProtocol[128] = { 0 }, sVersion[128] = { 0 }, tmp[256] = { 0 }, sha1out[20] = { 0 }, base64out[32] = { 0 };
					pPkg->GetHeadFiled("Sec-WebSocket-Protocol", sProtocol, sizeof(sProtocol));
					pPkg->GetHeadFiled("Sec-WebSocket-Version", sVersion, sizeof(sVersion));
					if (atoi(sVersion) != 13) {
						if (_pwslog)
							_pwslog->add(CLOG_DEFAULT_MOR, "ws sVersion(%s) error :ucid=%d, ", sVersion, _ucid);
						ws_send(http_sret400, strlen(http_sret400));
						return pPkg->HasKeepAlive();
					}
					ec::string vret;

					vret.reserve(1024 * 4);
					const char* sc = "HTTP/1.1 101 Switching Protocols\x0d\x0a"\
						"Upgrade: websocket\x0d\x0a"\
						"Connection: Upgrade\x0d\x0a";
					vret.append(sc);

					strcpy(tmp, skey);
					strcat(tmp, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
					encode_sha1(tmp, (unsigned int)strlen(tmp), sha1out); //SHA1
					encode_base64(base64out, sha1out, 20);    //BASE64

					vret.append("Sec-WebSocket-Accept: ").append((const char*)base64out).append("\x0d\x0a");
					if (sProtocol[0]) {
						vret.append("Sec-WebSocket-Protocol: ").append((const char*)sProtocol).append("\x0d\x0a");
					}
					if (pPkg->GetHeadFiled("Host", tmp, sizeof(tmp))) {
						vret.append("Host: ").append(tmp, strlen(tmp)).append("\x0d\x0a");
					}
					_wscompress = 0;
					if (pPkg->GetHeadFiled("Sec-WebSocket-Extensions", tmp, sizeof(tmp))) {
						char st[64] = { 0 };
						size_t pos = 0, len = strlen(tmp);
						while (ec::strnext(";,", tmp, len, pos, st, sizeof(st))) {
							if (!ec::stricmp("permessage-deflate", st)) {
								vret.append("Sec-WebSocket-Extensions: permessage-deflate; server_no_context_takeover; client_no_context_takeover\x0d\x0a");
								_wscompress = ws_permessage_deflate;
								break;
							}
							else if (!ec::stricmp("x-webkit-deflate-frame", st)) {
								vret.append("Sec-WebSocket-Extensions: x-webkit-deflate-frame; no_context_takeover\x0d\x0a");
								_wscompress = ws_x_webkit_deflate_frame;
								break;
							}
						}
					}
					vret.append("\x0d\x0a");

					int ns = ws_send(vret.data(), vret.size());
					if (_pwslog) {
						for (auto& v : vret) {
							if ('\r' == v)
								v = '\x20';
						}
					}
					if (ns < 0) {
						if (_pwslog)
							_pwslog->add(CLOG_DEFAULT_MOR, "ucid(%d) upggrade WS failed\n%s", _ucid, vret.c_str());
						return false;
					}
					if (_pwslog)
						_pwslog->add(CLOG_DEFAULT_MOR, "ucid(%d) upggrade WS success\n%s", _ucid, vret.c_str());
					_nws = 1; //update websocket
					onupdatews();
					return ns > 0;
				}
				catch (...) {
					if (_pwslog)
						_pwslog->add(CLOG_DEFAULT_ERR, "DoUpgradeWebSocket exception");
					return false;
				}
			}

			void reset_msg()
			{
				_wsmsg.clear();
				_wsmsg.shrink_to_fit();
				_comp = 0;
				_opcode = WS_OP_TXT;
			}

			int  ParseOneFrame(const char* stxt, size_t usize, int& fin)// reuturn >0 is do bytes
			{
				int comp = 0;
				fin = 0;
				if (usize < 2)
					return 0;
				int i;
				size_t datalen = 0, sizedo = 0;
				size_t datapos = 2;
				unsigned char* pu = (unsigned char*)stxt;

				fin = pu[0] & 0x80;
				comp = (pu[0] & 0x40) ? 1 : 0;
				int bmask = pu[1] & 0x80;
				int payloadlen = pu[1] & 0x7F;

				if (!_wsmsg.size())
					_opcode = pu[0] & 0x0f;

				if (bmask)//client can not use mask
					datapos += 4;

				if (payloadlen == 126) {
					datapos += 2;
					if (usize < datapos)
						return he_waitdata;

					datalen = pu[2];
					datalen <<= 8;
					datalen |= pu[3];
				}
				else if (payloadlen == 127) {
					datapos += 8;
					if (usize < datapos)
						return he_waitdata;

					for (i = 0; i < 8; i++) {
						if (i > 0)
							datalen <<= 8;
						datalen |= pu[2 + i];
					}
				}
				else {
					datalen = payloadlen;
					if (usize < datapos)
						return he_waitdata;
				}
				if (datalen > MAXSIZE_WS_READ_FRAME || _wsmsg.size() + datalen > MAXSIZE_WS_READ_PKG)//outof size limit
					return -1;
				if (usize < datapos + datalen)
					return 0;
				if (bmask) {
					unsigned int umask = pu[datapos - 1];
					umask <<= 8;
					umask |= pu[datapos - 2];
					umask <<= 8;
					umask |= pu[datapos - 3];
					umask <<= 8;
					umask |= pu[datapos - 4];
					ec::xor_le(pu + datapos, (int)datalen, umask);
				}
				sizedo = datapos + datalen;

				if (!comp)
					_wsmsg.append(stxt + datapos, datalen);
				else {
					if (_wscompress == ws_x_webkit_deflate_frame) { //deflate_frame
						ec::string debuf;
						debuf.reserve(EC_SIZE_WS_FRAME);
						debuf.push_back('\x78');
						debuf.push_back('\x9c');
						debuf.append(stxt + datapos, datalen);
						if (!_wsmsg.size()) {
							if (Z_OK != ws_decode_zlib(debuf.data(), debuf.size(), &_wsmsg))
								return -1;
						}
						else {
							bytes tmp;
							tmp.reserve(4 * debuf.size());
							if (Z_OK != ws_decode_zlib(debuf.data(), debuf.size(), &tmp))
								return -1;
							_wsmsg.append(tmp.data(), tmp.size());
						}
					}
					else {
						_comp = 1;
						_wsmsg.clear();
						_wsmsg.push_back('\x78');
						_wsmsg.push_back('\x9c');
						_wsmsg.append(stxt + datapos, datalen);
					}
				}
				return (int)sizedo;
			}

			template<class _Out>
			int WebsocketParse(const char* stxt, size_t usize, size_t& sizedo, _Out* pout)//support multi-frame
			{
				const char* pd = stxt;
				int ndo = 0, fin = 0;
				sizedo = 0;
				while (sizedo < usize) {
					ndo = ParseOneFrame(pd, usize - sizedo, fin);
					if (ndo <= 0)
						break;
					sizedo += ndo;
					pd += ndo;
					if (fin) {// end frame
						pout->clear();
						if (_comp && _wscompress == ws_permessage_deflate) {
							if (Z_OK != ws_decode_zlib(_wsmsg.data(), _wsmsg.size(), pout)) {
								pout->clear();
								return he_failed;
							}
						}
						else
							pout->append(_wsmsg.data(), _wsmsg.size());
						if (WS_OP_PONG == _opcode) {
							pout->clear();
							reset_msg();
							return he_waitdata;
						}
						else if (WS_OP_PING == _opcode) {
							ws_send(pout->data(), pout->size(), WS_OP_PONG);
							pout->clear();
							reset_msg();
							return he_waitdata;
						}
						else if (WS_OP_CLOSE == _opcode || pout->empty()) {
							pout->clear();
							return he_waitdata;
						}
						reset_msg();
						return he_ok;
					}
				}
				pout->clear();
				if (ndo < 0)
					return he_failed;
				return he_waitdata;
			}

			template<class _Out>
			int DoReadData(const char* pdata, size_t usize, _Out* pmsgout, ec::parsebuffer& rbuf)
			{
				size_t sizedo = 0;
				pmsgout->clear();
				rbuf.append(pdata, usize);
				if (_nws == PROTOCOL_HTTP) {
					ec::http::package prs;
					int nr = prs.parse((const char*)rbuf.data_(), rbuf.size_());
					if (nr > 0) {
						if (prs.ismethod("GET")) {
							char skey[128];
							if (prs.GetWebSocketKey(skey, sizeof(skey))) { //websocket Upgrade
								bool bupws = DoUpgradeWebSocket(skey, &prs);
								rbuf.freehead(nr);
								return bupws ? he_waitdata : he_failed;
							}
						}
						pmsgout->append((uint8_t*)rbuf.data_(), nr);
						rbuf.freehead(nr);
						return he_ok;
					}
					if (nr < 0) {
						rbuf.free();//clear and free buffer
						pmsgout->clear();
						return he_failed;
					}
					pmsgout->clear();
					return he_waitdata;
				}
				int nr = WebsocketParse((const char*)rbuf.data_(), rbuf.size_(), sizedo, pmsgout);//websocket
				if (nr == he_failed)
					rbuf.free();//clear and free buffer
				else {
					if (sizedo) {
						rbuf.freehead(sizedo);
					}
				}
				return nr;
			}
		};

		/*!
		\brief session class for http and websocket
		*/
		class session_ws : public session, public base_ws
		{
		public:
			/*!
			\brief construct for update session
			*/
			session_ws(session&& ss) :
				session(std::move(ss)), base_ws(session::_ucid, _psslog)
			{
				_protoc = EC_NET_SS_HTTP;
			}
		protected:
			virtual int ws_iosend(const void* pdata, size_t size)
			{
				return session::iosend(pdata, size);
			};

			virtual void onupdatews()
			{
				_protoc = EC_NET_SS_WS;
				_status = EC_NET_ST_WORK;
			}
		public:
			virtual int onrecvbytes(const void* pdata, size_t size, bytes* pmsgout)
			{
				_timelastio = ::time(0);
				return ws_onrecvbytes(pdata, size, pmsgout, _rbuf);
			};

			virtual int send(const void* pdata, size_t size)
			{
				return ws_send(pdata, size);
			}
		};
	}//namespace net
}//namespace ec
