/*!
\file ec_aiohttps.h

eclib3 AIO
Asynchronous https/wss session

\author  jiangyong
\update
  2023-5-21 update for http download big file

eclib 3.0 Copyright (c) 2017-2023, kipway
Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/

#pragma once
#include "ec_aiohttp.h"
#include "ec_aiotls.h"

namespace ec {
	namespace aio {
		class session_https : public session_tls, public basews
		{
		protected:
			long long _downpos; //下载文件位置
			long long _sizefile;//文件总长度
			ec::string _downfilename;
		public:
			session_https(session_tls&& ss) : session_tls(std::move(ss)), _downpos(0), _sizefile(0)
			{
				_protocol = EC_AIO_PROC_HTTPS;
			}
		protected:
			virtual void onupdatews() {
				_protocol = EC_AIO_PROC_WSS;
			}
			virtual int session_send(const void* pdata, size_t size, ec::ilog* plog) {
				return session_tls::sendasyn(pdata, size, plog);
			}
		public:
			virtual int onrecvbytes(const void* pdata, size_t size, ec::ilog* plog, ec::bytes* pmsgout)
			{
				int nr = 0;
				pmsgout->clear();
				if (pdata && size) {
					nr = session_tls::onrecvbytes(pdata, size, plog, pmsgout);
					if (EC_AIO_MSG_TCP != nr)
						return nr;
				}
				nr = DoReadData(_fd, (const char*)pmsgout->data(), pmsgout->size(), pmsgout, plog, _rbuf);
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
				if (_protocol != EC_AIO_PROC_HTTPS || !_sizefile || _downfilename.empty())
					return true;
				if (_downpos >= _sizefile) {
					_downpos = 0;
					_sizefile = 0;
					_downfilename.clear();
					return true;
				}
				ec::string sbuf;
#ifdef _MEM_TINY
				long long lread = 1024 * 30;
#else
				long long lread = 1024 * 120;
#endif
				if (_downpos + lread > _sizefile)
					lread = _sizefile - _downpos;
				if (!io::lckread(_downfilename.c_str(), &sbuf, _downpos, lread, _sizefile))
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
				return session_tls::sendasyn(sbuf.data(), sbuf.size(), nullptr) >= 0;
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
	}// namespace aio
}// namespace ec
