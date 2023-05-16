/*!
\file ec_aiohttps.h

eclib3 AIO
Asynchronous https/wss session

\author  jiangyong
*/

#pragma once
#include "ec_aiohttp.h"
#include "ec_aiotls.h"

namespace ec {
	namespace aio {
		class session_https : public session_tls, public basews
		{
		public:
			session_https(session_tls&& ss) : session_tls(std::move(ss))
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
		};
	}// namespace aio
}// namespace ec
