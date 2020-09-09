/*!
\file ec_tlsclient.h
\author	jiangyong
\email  kipway@outlook.com
\update 2020.9.6

TLS1.2(rfc5246)
support:
CipherSuite TLS_RSA_WITH_AES_128_CBC_SHA256 = { 0x00,0x3C };
CipherSuite TLS_RSA_WITH_AES_256_CBC_SHA256 = { 0x00,0x3D };

will add MAC secrets = 20byte
CipherSuite TLS_RSA_WITH_AES_128_CBC_SHA = {0x00,0x2F};
CipherSuite TLS_RSA_WITH_AES_256_CBC_SHA = {0x00,0x35};

tls_c
	client TLS1.2 session class
	tcp_c -> tls_c

eclib 3.0 Copyright (c) 2017-2019, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/
#pragma once
#include "ec_tls12.h"
#include "ec_tcpclient.h"
namespace ec
{
	class tls_c : public tcp_c
	{
	public:
		tls_c(memory* pmem, ilog* plog) :
			_nstatus(TLS_SESSION_NONE)
			, _pmem(pmem)
			, _plog(plog)
			, _tls(0, pmem, plog)
		{
		}

		bool initca(const char* caserver) //init server ca,Trust mode can not be called
		{
			return _tls.SetServerCa(caserver);
		}

		virtual int sendbytes(const void* p, int nlen)
		{
			if (TLS_SESSION_HKOK != _nstatus)
				return tcp_c::sendbytes(p, nlen);
			bytes pkg(_pmem);
			pkg.reserve(128 * (nlen / TLS_CBCBLKSIZE) + nlen + 256 - nlen % 128);
			if (!_tls.MakeAppRecord(&pkg, p, nlen)) {
				close();
				return -1;
			}
			return tcp_c::sendbytes(pkg.data(), (int)pkg.size());
		}

		int get_tls_status()
		{
			return _nstatus;
		}
	private:
		int   _nstatus; //tls status;  TLS_SESSION_XXX
	protected:
		memory* _pmem;
		ilog* _plog;
	private:
		tls::sessionclient _tls;
	protected:
		virtual void ontlshandshake() = 0;
		virtual void ontlsdata(const uint8_t* p, int nbytes) = 0;
	protected:
		virtual void onconnected()
		{
			tcp_c::onconnected();
			_tls.Reset();
			bytes pkg(_pmem);
			pkg.reserve(1024 * 12);
			_tls.mkr_ClientHelloMsg(&pkg);
			tcp_c::sendbytes(pkg.data(), (int)pkg.size());
		}

		virtual void ondisconnected()
		{
			_nstatus = TLS_SESSION_NONE;
		}

		virtual void onreadbytes(const uint8_t* p, int nbytes)
		{
			bytes pkg(_pmem);
			pkg.reserve(1024 * 16);
			int nst = _tls.OnTcpRead(p, nbytes, &pkg);
			if (TLS_SESSION_ERR == nst || TLS_SESSION_OK == nst || TLS_SESSION_NONE == nst) {
				if (pkg.size())
					tcp_c::sendbytes(pkg.data(), (int)pkg.size());
			}
			else if (TLS_SESSION_HKOK == nst) {
				if (pkg.size())
					tcp_c::sendbytes(pkg.data(), (int)pkg.size());
				_nstatus = TLS_SESSION_HKOK;
				ontlshandshake();
			}
			else if (TLS_SESSION_APPDATA == nst) {
				ontlsdata(pkg.data(), (int)pkg.size());
			}
			if (TLS_SESSION_ERR == nst) {
				close();
			}
		}
	};
}