/*!
\file ec_aiotls.h

eclib3 AIO
Asynchronous TLS1.2 session

\author  jiangyong
*/
#pragma once
#include "ec_tls12.h"
#include "ec_aiosession.h"
namespace ec {
	namespace aio {
		class session_tls : public session
		{
		public:
			session_tls(uint32_t ucid, session&& ss, const void* pcer, size_t cerlen,
				const void* pcerroot, size_t cerrootlen,
				std::mutex* pRsaLck, RSA* pRsaPrivate, ilog* plog) : session(std::move(ss))
				, _tls(ucid, pcer, cerlen, pcerroot, cerrootlen, pRsaLck, pRsaPrivate, plog)
			{
				_protocol = EC_AIO_PROC_TLS;
				_tls.appendreadbytes(_rbuf.data_(), _rbuf.size_());
				_rbuf.free();
			}

			session_tls(session_tls&& ss) : session(std::move(ss)), _tls(std::move(ss._tls))
			{
			}

			/*!
			\brief do receive bytes
			\param pdata [in] received byte stream
			\param size [in] received byte stream size(bytes)
			\param pmsgout [out] application layer message parsed from byte stream
			\return msgtype
				-1: error, need close
				0:  null message;
				>0: message type;
			*/
			virtual int onrecvbytes(const void* pdata, size_t size, ec::ilog* plog, ec::bytes* pmsgout)
			{
				int nr = EC_AIO_MSG_ERR;
				pmsgout->clear();
				int nst = _tls.OnTcpRead(pdata, size, pmsgout);
				if (TLS_SESSION_ERR == nst || TLS_SESSION_OK == nst || TLS_SESSION_NONE == nst) {
					nr = TLS_SESSION_ERR == nst ? EC_AIO_MSG_ERR : EC_AIO_MSG_NUL;
					if (pmsgout->size()) {
						if (session::sendasyn(pmsgout->data(), pmsgout->size(), plog) < 0)
							nr = EC_AIO_MSG_ERR;
					}
					pmsgout->clear();
					return nr;
				}
				else if (TLS_SESSION_HKOK == nst) {
					nr = EC_AIO_MSG_NUL;
					if (pmsgout->size()) {
						if (session::sendasyn(pmsgout->data(), pmsgout->size(), plog) < 0)
							return EC_AIO_MSG_ERR;
					}
					_status = EC_AIO_FD_TLSHANDOK;
					return onrecvbytes(nullptr, 0, plog, pmsgout);//继续解析数据。
				}
				else if (TLS_SESSION_APPDATA == nst) {
					nr = EC_AIO_MSG_TCP;
				}
				return nr;
			};

			// return -1:error; or (int)size
			virtual int sendasyn(const void* pdata, size_t size, ec::ilog* plog)
			{
				bytes tlspkg;
				tlspkg.reserve(size + 88 * (1 + size / TLS_CBCBLKSIZE));
				if (_tls.MakeAppRecord(&tlspkg, pdata, size))
					return session::sendasyn(tlspkg.data(), tlspkg.size(), plog);
				return -1;
			}

		protected:
			ec::tls::sessionserver _tls;
		};
	}// namespace aio
}//namespace tls
