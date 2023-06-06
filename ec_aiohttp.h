/*!
\file ec_aiohttp.h

eclib3 AIO
Asynchronous http/ws session

\author  jiangyong
\update 
  2023-5-21 update for http download big file

eclib 3.0 Copyright (c) 2017-2023, kipway
Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/

#pragma once

#include "ec_aiosession.h"
#include "ec_base64.h"
#include "ec_sha1.h"
#include "ec_http.h"
#include "ec_wstips.h"
#include "ec_diskio.h"

namespace ec {
	namespace aio {
		class basews {
		public:
			basews() : _nws(PROTOCOL_HTTP)
				, _wscompress(0)
				, _comp(0), _opcode(WS_OP_TXT) {
			}

		protected:
			int _nws; // 0: http ; 1:ws
			int _wscompress; // ws_x_webkit_deflate_frame or ws_permessage_deflate
			bytes _wsmsg; // ws frame
			int _comp;// compress flag
			int _opcode;  // operate code

			int ws_send(int nfd, const void* pdata, size_t size, ec::ilog* plog, int optcode = WS_OP_TXT) //if https, rewrite it
			{
				if (PROTOCOL_HTTP == _nws)
					return session_send(pdata, size, plog);
				else if (PROTOCOL_WS == _nws) {
					bool bsend;
					ec::vstream vret;
					vret.reserve(1024 + size - size % 512);
					if (_wscompress == ws_x_webkit_deflate_frame) { //deflate-frame
						bsend = ws_make_perfrm(pdata, size, optcode, &vret);
					}
					else { // ws_permessage_deflate
						bsend = ws_make_permsg(pdata, size, optcode, &vret, size > 128 && 0 != _wscompress);
					}
					if (!bsend) {
						if (plog)
							plog->add(CLOG_DEFAULT_ERR, "fd(%d) send make wsframe failed,size %u", nfd, (unsigned int)size);
						return -1;
					}
					return session_send(vret.data(), vret.size(), plog);
				}
				if (plog)
					plog->add(CLOG_DEFAULT_ERR, "ws send failed _protocol = %d", _nws);
				return -1;
			}
			virtual void onupdatews() = 0;
			virtual int session_send(const void* pdata, size_t size, ec::ilog* plog) = 0;

			bool DoUpgradeWebSocket(int nfd, const char* skey, ec::http::package* pPkg, ec::ilog* plog)
			{
				if (plog) {
					char stmp[128] = { 0 };
					str1k sa;
					if (pPkg->GetHeadFiled("Origin", stmp, sizeof(stmp))) {
						sa.append("\n\tOrigin: ").append(stmp);
					}
					if (pPkg->GetHeadFiled("Sec-WebSocket-Extensions", stmp, sizeof(stmp))) {
						sa.append("\n\tSec-WebSocket-Extensions: ").append(stmp);
					}
					plog->add(CLOG_DEFAULT_MOR, "ucid(%u) upgrade websocket%s", nfd, sa.c_str());
				}
				char sProtocol[128] = { 0 }, sVersion[128] = { 0 }, tmp[256] = { 0 }, sha1out[20] = { 0 }, base64out[32] = { 0 };
				pPkg->GetHeadFiled("Sec-WebSocket-Protocol", sProtocol, sizeof(sProtocol));
				pPkg->GetHeadFiled("Sec-WebSocket-Version", sVersion, sizeof(sVersion));
				if (atoi(sVersion) != 13) {
					if (plog)
						plog->add(CLOG_DEFAULT_MOR, "ws sVersion(%s) error :ucid=%d, ", sVersion, nfd);
					const char* sreterr_400 = "http/1.1 400  Bad Request!\r\nConnection:keep-alive\r\n\r\n";
					return ws_send(nfd, sreterr_400, strlen(sreterr_400), plog) >= 0;
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

				int ns = ws_send(nfd, vret.data(), vret.size(), plog);
				if (plog) {
					for (auto& v : vret) {
						if ('\r' == v)
							v = '\x20';
					}
				}
				if (ns < 0) {
					if (plog)
						plog->add(CLOG_DEFAULT_MOR, "ucid(%d) upgrade WS failed\n%s", nfd, vret.c_str());
					return false;
				}
				if (plog)
					plog->add(CLOG_DEFAULT_MOR, "ucid(%d) upgrade WS success\n%s", nfd, vret.c_str());
				_nws = PROTOCOL_WS; //update websocket
				onupdatews();
				return ns > 0;
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
			int WebsocketParse(int nfd, const char* stxt, size_t usize, size_t& sizedo, _Out* pout, ec::ilog* plog)//support multi-frame
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
							ws_send(nfd, pout->data(), pout->size(), plog, WS_OP_PONG);
							pout->clear();
							reset_msg();
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
			int DoReadData(int nfd, const char* pdata, size_t usize, _Out* pmsgout, ec::ilog* plog, ec::parsebuffer &rbuf)
			{
				size_t sizedo = 0;
				rbuf.append(pdata, usize);
				pmsgout->clear();
				if (_nws == PROTOCOL_HTTP) {
					ec::http::package prs;
					int nr = prs.parse((const char*)rbuf.data_(), rbuf.size_());
					if (nr > 0) {
						if (prs.ismethod("GET")) {
							char skey[128];
							if (prs.GetWebSocketKey(skey, sizeof(skey))) { //websocket Upgrade
								bool bupws = DoUpgradeWebSocket(nfd, skey, &prs, plog);
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
				int nr = WebsocketParse(nfd, (const char*)rbuf.data_(), rbuf.size_(), sizedo, pmsgout, plog);//websocket
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

		class session_http : public session, public basews
		{
		protected:
			long long _downpos; //下载文件位置
			long long _sizefile;//文件总长度
			ec::string _downfilename;
		public:
			session_http(session&& ss) : session(std::move(ss)), _downpos(0), _sizefile(0)
			{
				_protocol = EC_AIO_PROC_HTTP;
			}
		protected:
			virtual void onupdatews() {
				_protocol = EC_AIO_PROC_WS;
			}

			virtual int session_send(const void* pdata, size_t size, ec::ilog* plog) {
				return session::sendasyn(pdata, size, plog);
			}
		public:
			virtual int onrecvbytes(const void* pdata, size_t size, ec::ilog* plog, ec::bytes* pmsgout)
			{
				int nr = DoReadData(_fd, (const char*)pdata, size, pmsgout, plog, _rbuf);
				if (he_failed == nr)
					return EC_AIO_MSG_ERR;
				else if (he_ok == nr) {
					if (PROTOCOL_HTTP == _nws)
						return EC_AIO_MSG_HTTP;
					else if (PROTOCOL_WS == _nws)
						return EC_AIO_MSG_WS;
					return EC_AIO_MSG_NUL;
				}
				return EC_AIO_MSG_NUL; //wait
			};

			// return -1:error; or (int)size
			virtual int sendasyn(const void* pdata, size_t size, ec::ilog* plog)
			{
				return ws_send(_fd, pdata, size, plog);
			}
			virtual bool onSendCompleted() //return false will disconnected
			{
				if (_protocol != EC_AIO_PROC_HTTP || !_sizefile || _downfilename.empty())
					return true;
				if (_downpos >= _sizefile) {
					_downpos = 0;
					_sizefile = 0;
					_downfilename.clear();
					return true;
				}
				ec::string sbuf;
				long long lread = 1024 * 30;
				if (_downpos + lread > _sizefile)
					lread = _sizefile - _downpos;
				sbuf.reserve((size_t)lread);
				if (!io::lckread(_downfilename.c_str(), &sbuf, _downpos, lread))
					return false;
				if (sbuf.empty()) {
					_downpos = 0;
					_sizefile = 0;
					_downfilename.clear();
					return true;
				}
				_downpos += (long long)sbuf.size();
				if (_downpos >= _sizefile) {
					_downpos = 0;
					_sizefile = 0;
					_downfilename.clear();
				}
				return session::sendasyn(sbuf.data(), sbuf.size(), nullptr) >= 0;
			}

			virtual void setHttpDownFile(const char* sfile, long long pos, long long filelen)
			{
				if (sfile && *sfile)
					_downfilename = sfile;
				else
					_downfilename.clear();
				_downpos = pos;
				_sizefile = filelen;
			}

			virtual bool hasSendJob() {
				return _sizefile && _downfilename.size();
			};
		};
	}//namespace aio
}//namespace ec