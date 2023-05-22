/*!
\file ec_netss_wss.h
\author	jiangyong
\email  kipway@outlook.com
\update 2023-5-21
  2023-5-21 support big file download
  2023-5-13 remove ec::memory
net::session_wss
	websocket over HTTPS(TLS1.2) session

eclib 3.0 Copyright (c) 2017-2023, kipway
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
		protected:
			long long _downpos; //下载文件位置
			long long _sizefile;//文件总长度
			ec::string _downfilename;
		public:
			/*!
			\brief construct for update session
			*/
			session_wss(session_tls&& ss) : session_tls(std::move(ss)),
				base_ws(session_tls::_ucid, _psslog), _downpos(0), _sizefile(0)
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
				bytes data;
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

			virtual bool onSendCompleted() //return false will disconnected
			{
				if (_protoc != EC_NET_SS_HTTPS || !_sizefile || _downfilename.empty())
					return true;
				ec::string sbuf;
				sbuf.reserve(1024 * 30);
				if (!io::lckread(_downfilename.c_str(), &sbuf, _downpos, 1024 * 30))
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
				return session_tls::iosend(sbuf.data(), sbuf.size()) >= 0;
			}

			virtual void setHttpDownFile(const char* sfile, long long pos, long long filelen)
			{
				_downfilename = sfile;
				_downpos = pos;
				_sizefile = filelen;
			}

			virtual bool hasSendJob() {
				return _sizefile && _downfilename.size();
			};
		};//session_wss
	}//namespace net
}//namespace ec