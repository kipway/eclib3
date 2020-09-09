/*!
\file ec_netsrv_ws.h
\author	jiangyong
\email  kipway@outlook.com
\update 2020.9.6

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

			base_ws(session* ps) : _ucid(ps->_ucid), _nws(0), _wscompress(0),
				_txt(ps->_pssmem), _wsmsg(ps->_pssmem),
				_comp(0), _opcode(WS_OP_TXT), _pwsmem(ps->_pssmem), _pwslog(ps->_psslog)
			{
				_txt.reserve(1024 * 16);
				_wsmsg.reserve(1024 * 16);
			}
		public:
			uint32_t _ucid;
			int _nws; // 0: http ; 1:ws
			int _wscompress; // ws_x_webkit_deflate_frame or ws_permessage_deflate
			string _txt;   // tmp
			bytes _wsmsg; // ws frame
			int _comp;// compress flag
			int _opcode;  // operate code
			memory *_pwsmem;
			ilog *_pwslog;
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
			int ws_onrecvbytes(const void* pdata, size_t size, bytes* pmsgout)
			{
				pmsgout->clear();
				int nr = DoReadData((const char*)pdata, size, pmsgout);
				if (he_failed == nr)
					return -1;
				else if (he_ok == nr)
					return 0;
				pmsgout->clear();
				return 0;
			};

			int ws_send(const void* pdata, size_t size) // return -1:error
			{
				if (!_nws)
					return ws_iosend(pdata, size);
				else if (1 == _nws) {
					bool bsend;
					bytes vret(_pwsmem);
					vret.reserve(1024 + size - size % 512);
					if (_wscompress == ws_x_webkit_deflate_frame) { //deflate-frame
						bsend = ws_make_perfrm(pdata, size, WS_OP_TXT, &vret);
					}
					else { // ws_permessage_deflate
						bsend = ws_make_permsg(pdata, size, WS_OP_TXT, &vret, size > 128 && _wscompress);
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
		public:
			bool httprequest(ec::mimecfg* pmine, const char* sroot, ec::http::package* pPkg) //default httprequest
			{
				if (pPkg->ismethod("GET")) {
					char skey[128];
					if (pPkg->GetWebSocketKey(skey, sizeof(skey))) //websocket Upgrade
						return DoUpgradeWebSocket(skey, pPkg);
					return  DoGetAndHead(pmine, sroot, pPkg);
				}
				else if (pPkg->ismethod("HEAD"))
					return  DoGetAndHead(pmine, sroot, pPkg, false);
				httpreterr(http_sret400, 400);
				return pPkg->HasKeepAlive();
			}

			bool parserange(const char* stxt, size_t txtlen, int64_t &lpos, int64_t &lsize)
			{
				char suints[8] = { 0 }, spos[16] = { 0 }, ssize[8] = { 0 };
				size_t pos = 0;
				if (!strnext('=', stxt, txtlen, pos, suints, sizeof(suints)) || !strieq("bytes", suints))
					return false;
				if (!strnext('-', stxt, txtlen, pos, spos, sizeof(spos)))
					return false;
				lpos = atoll(spos);
				if (!strnext(',', stxt, txtlen, pos, ssize, sizeof(ssize)))
					lsize = 0;
				else
					lsize = atoll(ssize);
				return true;
			}

			bool DoGetAndHead(ec::mimecfg* pmine, const char* sroot, ec::http::package* pPkg, bool bGet = true)
			{
				char sfile[1024], tmp[1024];

				sfile[0] = '\0';
				tmp[0] = '\0';

				if (!pPkg->GetUrl(sfile, sizeof(sfile)))
					return false;
				ec::url2utf8(sfile, tmp, (int)sizeof(tmp));
				const char *su = tmp;
				if (tmp[0] == '/') {
					su++;
					if (!*su)
						snprintf(sfile, sizeof(sfile), "%sindex.html", sroot);
					else
						snprintf(sfile, sizeof(sfile), "%s%s", sroot, su);
				}
				else
					snprintf(sfile, sizeof(sfile), "%s%s", sroot, su);

				if (http::isdir(sfile)) {
					httpreterr(http_sret404, 404);
					return pPkg->HasKeepAlive();
				}
				long long flen = ec::io::filesize(sfile);
				if (flen < 0) {
					httpreterr(http_sret404, 404);
					return pPkg->HasKeepAlive();
				}
				if (!bGet) // head
					return DoHead(sfile, pPkg);

				int64_t rangpos = 0, rangsize = 0;
				if (pPkg->GetHeadFiled("Range", tmp, sizeof(tmp))) { // "Range: bytes=0-1023"
					if (!parserange(tmp, strlen(tmp), rangpos, rangsize)) {
						httpreterr(http_sret413, 413);
						return pPkg->HasKeepAlive();
					}
				}
				if (rangsize > HTTP_MAX_RANG_SIZE)
					rangsize = HTTP_MAX_RANG_SIZE;
				if (!rangpos && !rangsize) { // get all
					if (flen > HTTP_MAX_RANG_SIZE) {
						httpreterr(http_sret413, 413);
						return pPkg->HasKeepAlive();
					}
					else
						return DoGet(pmine, sfile, pPkg);
				}
				return DoGetRang(pmine, sfile, pPkg, rangpos, rangsize, flen);
			}

			void httpreterr(const char* sret, int errcode)
			{
				int nret = ws_iosend(sret, strlen(sret));
				if (_pwslog) {
					if (nret >= 0)
						_pwslog->add(CLOG_DEFAULT_DBG, "http write ucid(%u): error %d", _ucid, errcode);
					else
						_pwslog->add(CLOG_DEFAULT_DBG, "http write ucid(%u) failed.", _ucid);
				}
			}

			static bool iszipfile(const char *sext)
			{
				const char* s[] = { ".zip", ".rar", ".tar", ".7z", ".gz", ".jpg", ".jpeg", ".gif", ".arj", ".jar" };
				size_t i = 0;
				for (i = 0; i < sizeof(s) / sizeof(const char*); i++) {
					if (ec::strieq(sext, s[i]))
						return true;
				}
				return false;
			}
		private:
			bool DoHead(const char* sfile, ec::http::package* pPkg)
			{
				char tmp[4096];
				tmp[0] = '\0';

				long long flen = ec::io::filesize(sfile);
				if (flen < 0) {
					httpreterr(http_sret404, 404);
					return pPkg->HasKeepAlive();
				}
				string answer(_pwsmem);
				answer.reserve(1024 * 32);

				answer.append("HTTP/1.1 200 ok\r\n");
				answer.append("Server: eclib websocket server\r\n");

				if (pPkg->HasKeepAlive())
					answer.append("Connection: keep-alive\r\n");

				answer.append("Accept-Ranges: bytes\r\n");

				sprintf(tmp, "Content-Length: %lld\r\n\r\n", flen);
				answer.append(tmp);
				if (_pwslog)
					_pwslog->add(CLOG_DEFAULT_DBG, "http head write ucid(%u) size %zu\n%s", _ucid, answer.size(), answer.c_str());
				return ws_send(answer.data(), answer.size()) > 0;
			}

			bool DoGet(ec::mimecfg* pmine, const char* sfile, ec::http::package* pPkg)
			{
				char tmp[4096];
				tmp[0] = '\0';

				string answer(_pwsmem);
				answer.reserve(1024 * 32);
				answer.append("HTTP/1.1 200 ok\r\n").append("Server: eclib websocket server\r\n");

				if (pPkg->HasKeepAlive())
					answer.append("Connection: keep-alive\r\n");

				char sminetype[32] = { 0 };
				const char* sext = http::file_extname(sfile);
				if (sext && *sext && pmine->getmime(sext, tmp, sizeof(tmp))) {
					answer.append("Content-type: ").append(tmp).append("\r\n");
					size_t ipos = 0;
					ec::strnext('/', tmp, strlen(tmp), ipos, sminetype, sizeof(sminetype));
				}
				else
					answer.append("Content-type: application/octet-stream\r\n");

				string filetmp(_pwsmem);
				filetmp.reserve(1024 * 16);
				if (!io::lckread(sfile, &filetmp)) {
					httpreterr(http_sret404, 404);
					return pPkg->HasKeepAlive();
				}
				int necnode = 0;
				if (filetmp.size() > 1024 && (ec::strieq(sminetype, "text") || (strieq(sminetype, "application") && !iszipfile(sext)))
					&& pPkg->GetHeadFiled("Accept-Encoding", tmp, sizeof(tmp))) {
					char sencode[16] = { 0 };
					size_t pos = 0;
					while (ec::strnext(";,", tmp, strlen(tmp), pos, sencode, sizeof(sencode))) {
						if (!ec::stricmp("gzip", sencode)) {
							answer.append("Content-Encoding: gzip\r\n");
							necnode = 2;
							break;
						}
						if (!ec::stricmp("deflate", sencode)) {
							answer.append("Content-Encoding: deflate\r\n");
							necnode = 1;
						}
					}
				}
				if (necnode) {
					size_t poslen = answer.size(), sizecl;
					sprintf(tmp, "Content-Length: %9zu\r\n\r\n", filetmp.size());
					sizecl = strlen(tmp);
					answer.append(tmp, sizecl);
					if (Z_OK != http::package::encode_body(filetmp.data(), filetmp.size(), &answer, necnode == 2)) {
						if (_pwslog)
							_pwslog->add(CLOG_DEFAULT_ERR, "ucid(%u) ws_encode_zlib failed", _ucid);
						return false;
					}
					filetmp.clear();
					filetmp.shrink_to_fit();
					sprintf(tmp, "Content-Length: %9zu\r\n\r\n", (answer.size() - poslen - sizecl));
					size_t z1 = strlen(tmp);
					while (z1 < sizecl) {
						tmp[z1] = '\x20';
						z1++;
						tmp[z1] = '\0';
					}
					memcpy(answer.data() + poslen, tmp, sizecl);	// reset Content-Length
				}
				else {
					sprintf(tmp, "Content-Length: %zu\r\n\r\n", filetmp.size());
					answer.append(tmp, strlen(tmp));
					answer.append(filetmp.data(), filetmp.size());
				}
				if (_pwslog)
					_pwslog->add(CLOG_DEFAULT_DBG, "http write ucid(%u) size %zu", _ucid, answer.size());
				return ws_send(answer.data(), answer.size()) > 0;
			}

			bool DoGetRang(ec::mimecfg* pmine, const char* sfile, ec::http::package* pPkg, int64_t lpos, int64_t lsize, int64_t lfilesize)
			{
				char tmp[4096];
				tmp[0] = '\0';

				string answer(_pwsmem);
				answer.reserve(1024 * 32);
				answer.append("HTTP/1.1 206 Partial Content\r\n").append("Server: eclib websocket server\r\n");

				if (pPkg->HasKeepAlive())
					answer.append("Connection: keep-alive\r\n");

				answer.append("Accept-Ranges: bytes\r\n");

				const char* sext = http::file_extname(sfile);
				if (sext && *sext && pmine->getmime(sext, tmp, sizeof(tmp))) {
					answer.append("Content-type: ").append(tmp).append("\r\n");
				}
				else
					answer.append("Content-type: application/octet-stream\r\n");

				string filetmp(_pwsmem);
				filetmp.reserve(1024 * 32);
				if (!io::lckread(sfile, &filetmp, lpos, lsize)) {
					httpreterr(http_sret404, 404);
					return pPkg->HasKeepAlive();
				}

				snprintf(tmp, sizeof(tmp), "Content-Range: bytes %lld-%lld/%lld\r\n", (long long)lpos, (long long)(lpos + filetmp.size() - 1), (long long)lfilesize);
				answer.append(tmp);
				snprintf(tmp, sizeof(tmp), "Content-Length: %zu\r\n\r\n", filetmp.size());
				answer.append(tmp);

				answer.append(filetmp.data(), filetmp.size());
				if (_pwslog)
					_pwslog->add(CLOG_DEFAULT_DBG, "http write rang rang %lld-%lld/%lld", (long long)lpos, (long long)(lpos + filetmp.size() - 1), (long long)lfilesize);
				return ws_send(answer.data(), answer.size()) > 0;
			}
			bool DoUpgradeWebSocket(const char *skey, ec::http::package* pPkg)
			{
				if (_pwslog) {
					char stmp[128] = { 0 };
					array<char, 1024> sa;
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
				string vret(_pwsmem);
				vret.reserve(1024 * 4);
				const char*sc = "HTTP/1.1 101 Switching Protocols\x0d\x0a"\
					"Upgrade: websocket\x0d\x0a"\
					"Connection: Upgrade\x0d\x0a";
				vret.append(sc);

				strcpy(tmp, skey);
				strcat(tmp, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
				encode_sha1(tmp, (unsigned int)strlen(tmp), sha1out); //SHA1
				encode_base64(base64out, sha1out, 20);    //BASE64

				vret.append("Sec-WebSocket-Accept: ").append(base64out).append("\x0d\x0a");
				if (sProtocol[0]) {
					vret.append("Sec-WebSocket-Protocol: ").append(sProtocol).append("\x0d\x0a");
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
							vret.append("Sec-WebSocket-Extensions: x-webkit-deflate-frame; no_context_takeover\x0d\x0a", 2);
							_wscompress = ws_x_webkit_deflate_frame;
							break;
						}
					}
				}
				vret.append("\x0d\x0a");
				_txt.clear();
				_txt.shrink_to_fit();
				int ns = ws_send(vret.data(), vret.size());
				if (_pwslog) {
					for (auto &v : vret) {
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

			void reset_msg()
			{
				_wsmsg.clear();
				_comp = 0;
				_opcode = WS_OP_TXT;
			}

			int  ParseOneFrame(const char* stxt, size_t usize, int &fin)// reuturn >0 is do bytes
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
						string debuf(_pwsmem);
						debuf.reserve(EC_SIZE_WS_FRAME);
						debuf.push_back('\x78');
						debuf.push_back('\x9c');
						debuf.append(stxt + datapos, datalen);
						if (!_wsmsg.size()) {
							if (Z_OK != ws_decode_zlib(debuf.data(), debuf.size(), &_wsmsg))
								return -1;
						}
						else {
							bytes tmp(_pwsmem);
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
			int WebsocketParse(const char* stxt, size_t usize, size_t &sizedo, _Out* pout)//support multi-frame
			{
				const char *pd = stxt;
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
			int DoReadData(const char* pdata, size_t usize, _Out* pmsgout)
			{
				size_t sizedo = 0;
				pmsgout->clear();
				_txt.append(pdata, usize);
				if (_nws == PROTOCOL_HTTP) {
					ec::http::package prs;
					int nr = prs.parse(_txt.data(), _txt.size());
					if (nr > 0) {
						if (prs.ismethod("GET")) {
							char skey[128];
							if (prs.GetWebSocketKey(skey, sizeof(skey))) { //websocket Upgrade
								bool bupws = DoUpgradeWebSocket(skey, &prs);
								_txt.erase(0, nr);
								_txt.shrink_to_fit();
								return bupws ? he_waitdata : he_failed;
							}
						}
						pmsgout->append((uint8_t*)_txt.data(), nr);
						_txt.erase(0, nr);
						_txt.shrink_to_fit();
						return he_ok;
					}
					if (nr < 0) {
						_txt.clear();
						pmsgout->clear();
						return he_failed;
					}
					pmsgout->clear();
					_txt.shrink_to_fit();
					return he_waitdata;
				}
				int nr = WebsocketParse(_txt.data(), _txt.size(), sizedo, pmsgout);//websocket
				if (nr == he_failed)
					_txt.clear();
				else {
					if (sizedo) {
						_txt.erase(0, sizedo);
						_txt.shrink_to_fit();
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
			session_ws(session* ps, const uint8_t* pu, size_t size) :
				session(ps), base_ws(ps)
			{
				_protoc = EC_NET_SS_HTTP;
				_txt.append(pu, size);
				ps->_fd = INVALID_SOCKET;// move
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
				return ws_onrecvbytes(pdata, size, pmsgout);
			};

			virtual int send(const void* pdata, size_t size)
			{
				return ws_send(pdata, size);
			}
		};
	}//namespace net
}//namespace ec
