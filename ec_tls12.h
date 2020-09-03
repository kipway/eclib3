/*!
\file ec_tls12.h
\author	jiangyong
\email  kipway@outlook.com
\update 2020.8.29

eclib TLS1.2(rfc5246)  class
support:
CipherSuite TLS_RSA_WITH_AES_128_CBC_SHA256 = { 0x00,0x3C };
CipherSuite TLS_RSA_WITH_AES_256_CBC_SHA256 = { 0x00,0x3D };

will add MAC secrets = 20byte
CipherSuite TLS_RSA_WITH_AES_128_CBC_SHA = {0x00,0x2F};
CipherSuite TLS_RSA_WITH_AES_256_CBC_SHA = {0x00,0x35};

class ec::tls_session;
class ec::tls_session_cli;
class ec::tls_session_srv;

eclib 3.0 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

简介：
TLS1.2(RFC5246)服务端和客户端会话实现，支持上面列举的4个密码套件。
使用了openssl的密码库 libeay32.lib。
用于HTTPS和WSS通信。
*/
#pragma once

#define  tls_rec_fragment_len 16384

#ifndef _WIN32
#include <dlfcn.h>
#endif
#include <time.h>

#include "ec_memory.h"
#include "ec_event.h"
#include "ec_mutex.h"
#include "ec_array.h"
#include "ec_string.h"
#include "ec_stream.h"
#include "ec_log.h"

#ifdef _WIN32
#pragma comment(lib,"libeay32.lib")
#endif

#include "openssl/rand.h"
#include "openssl/x509.h"
#include "openssl/hmac.h"
#include "openssl/aes.h"
#include "openssl/pem.h"

/*!
\brief CipherSuite
*/
#define TLS_RSA_WITH_AES_128_CBC_SHA    0x2F
#define TLS_RSA_WITH_AES_256_CBC_SHA    0x35
#define TLS_RSA_WITH_AES_128_CBC_SHA256 0x3C
#define TLS_RSA_WITH_AES_256_CBC_SHA256 0x3D
#define TLS_COMPRESS_NONE   0

#define TLSVER_MAJOR        3
#define TLSVER_NINOR        3

#define TLS_CBCBLKSIZE  16292   // (16384-16-32-32 - 8)

#define TLS_SESSION_ERR		(-1)// error
#define TLS_SESSION_NONE    0
#define TLS_SESSION_OK		1   // need send data
#define TLS_SESSION_HKOK	2   // handshack ok
#define TLS_SESSION_APPDATA 3   // on app data

#define TLS_REC_BUF_SIZE (1024 * 18)
namespace ec
{
	template<class _Out>
	inline bool get_cert_pkey(const char* filecert, _Out* pout)//get ca public key
	{
		uint8_t stmp[8192];
		FILE* pf = fopen(filecert, "rb");
		if (!pf)
			return false;
		size_t size;
		size = fread(stmp, 1, sizeof(stmp), pf);
		fclose(pf);
		if (size >= sizeof(stmp))
			return false;
		X509* _px509;
		const unsigned char* p = stmp;
		_px509 = d2i_X509(0, &p, (long)size);//only use first Certificate
		if (!_px509)
			return false;
		pout->clear();
		pout->append(_px509->cert_info->key->public_key->data, (size_t)_px509->cert_info->key->public_key->length);

		X509_free(_px509);
		return true;
	}
	namespace tls {
		enum rec_contenttype {
			rec_change_cipher_spec = 20,
			rec_alert = 21,
			rec_handshake = 22,
			rec_application_data = 23,
			rec_max = 255
		};
		enum handshaketype {
			hsk_hello_request = 0,
			hsk_client_hello = 1,
			hsk_server_hello = 2,
			hsk_certificate = 11,
			hsk_server_key_exchange = 12,
			hsk_certificate_request = 13,
			hsk_server_hello_done = 14,
			hsk_certificate_verify = 15,
			hsk_client_key_exchange = 16,
			hsk_finished = 20,
			hsk_max = 255
		};

		class handshake // Handshake messages
		{
		public:
			handshake()
			{
			}
		public:
			array<uint8_t, 256>  _srv_hello;
			array<uint8_t, 16384> _srv_certificate;
			array<uint8_t, 256>  _srv_hellodone;

			array<uint8_t, 1024> _cli_hello;
			array<uint8_t, 640 > _cli_key_exchange;
			array<uint8_t, 512>  _cli_finished;
		public:
			static handshake* new_cls(memory* _pmem = nullptr)
			{
				if (!_pmem)
					return new handshake();
				handshake* p = (handshake*)_pmem->mem_malloc(sizeof(handshake));
				if (p)
					new(p)handshake();
				return p;
			}
			static void del_cls(handshake* p, memory * _pmem = nullptr)
			{
				if (!p)
					return;
				if (_pmem) {
					p->~handshake();
					_pmem->mem_free(p);
				}
				else
					delete p;
			}

			template<class _Out>
			void out(_Out* p, bool bfin = false)
			{
				p->clear();
				p->append(_cli_hello.data(), _cli_hello.size());
				p->append(_srv_hello.data(), _srv_hello.size());
				p->append(_srv_certificate.data(), _srv_certificate.size());
				p->append(_srv_hellodone.data(), _srv_hellodone.size());
				p->append(_cli_key_exchange.data(), _cli_key_exchange.size());
				if (bfin)
					p->append(_cli_finished.data(), _cli_finished.size());
			}
			void clear()
			{
				_srv_hello.clear();
				_srv_certificate.clear();
				_srv_hellodone.clear();
				_cli_hello.clear();
				_cli_key_exchange.clear();
				_cli_finished.clear();
			}
		};

		/*!
		\brief base class for TLS 1.2 session
		*/
		class session
		{
		public:
			using tlsrec = ec::array<uint8_t, TLS_REC_BUF_SIZE>;
			session(const session&) = delete;
			session& operator = (const session&) = delete;
			session(bool bserver, unsigned int ucid, memory* pmem, ilog* plog) : _pmem(pmem), _plog(plog),
				_ucid(ucid), _bserver(bserver), _breadcipher(false), _bsendcipher(false), _seqno_send(0), _seqno_read(0), _cipher_suite(0),
				_pkgtcp(pmem), _bhandshake_finished(false)
			{
				_pkgtcp.reserve(1024 * 20);
				resetblks();
				_hmsg = handshake::new_cls(_pmem);
			};

			session(session*p) : _pmem(p->_pmem), _plog(p->_plog), _ucid(p->_ucid), _bserver(p->_bserver), _breadcipher(p->_breadcipher),
				_bsendcipher(p->_bsendcipher), _seqno_send(p->_seqno_send), _seqno_read(p->_seqno_read), _cipher_suite(p->_cipher_suite),
				_pkgtcp(p->_pmem), _bhandshake_finished(p->_bhandshake_finished)
			{
				_pkgtcp.clear();
				_pkgtcp.append(p->_pkgtcp.data(), p->_pkgtcp.size());

				memcpy(_keyblock, p->_keyblock, sizeof(_keyblock));
				memcpy(_key_cwmac, p->_key_cwmac, sizeof(_key_cwmac));
				memcpy(_key_swmac, p->_key_swmac, sizeof(_key_swmac));
				memcpy(_key_cw, p->_key_cw, sizeof(_key_cw));
				memcpy(_key_sw, p->_key_sw, sizeof(_key_sw));

				_hmsg = p->_hmsg;
				p->_hmsg = nullptr; //move

				memcpy(_serverrand, p->_serverrand, sizeof(_serverrand));
				memcpy(_clientrand, p->_clientrand, sizeof(_clientrand));
				memcpy(_master_key, p->_master_key, sizeof(_master_key));
				memcpy(_key_block, p->_key_block, sizeof(_key_block));
			}
			virtual ~session()
			{
				if (_hmsg) {
					handshake::del_cls(_hmsg, _pmem);
					_hmsg = nullptr;
				}
			};
			inline uint32_t get_ucid()
			{
				return _ucid;
			}
			inline memory* getpmem() {
				return _pmem;
			}
		protected:
			memory * _pmem;
			ilog* _plog;

			uint32_t _ucid;
			bool   _bserver, _breadcipher, _bsendcipher; // read/ write start use cipher

			uint64_t _seqno_send, _seqno_read;
			uint16_t _cipher_suite;

			bytes _pkgtcp;

			uint8_t _keyblock[256], _key_cwmac[32], _key_swmac[32];// client_write_MAC_key,server_write_MAC_key
			uint8_t _key_cw[32], _key_sw[32];   // client_write_key,server_write_key

			handshake *_hmsg;
			uint8_t  _serverrand[32], _clientrand[32], _master_key[48], _key_block[256];
			bool  _bhandshake_finished;
			void resetblks()
			{
				memset(_keyblock, 0, sizeof(_keyblock));
				memset(_serverrand, 0, sizeof(_serverrand));
				memset(_clientrand, 0, sizeof(_clientrand));
				memset(_master_key, 0, sizeof(_master_key));
				memset(_key_block, 0, sizeof(_key_block));
			}
		private:
			bool caldatahmac(uint8_t type, uint64_t seqno, const void* pd, size_t len, uint8_t* pkeymac, uint8_t *outmac)
			{ //Calculate the hashmac value of the TLS record
				tlsrec es;
				try {
					es < seqno < type < (char)TLSVER_MAJOR < (char)TLSVER_NINOR < (unsigned short)len;
					es.write(pd, len);
				}
				catch (...) {
					return false;
				}
				unsigned int mdlen = 0;
				if (_cipher_suite == TLS_RSA_WITH_AES_128_CBC_SHA || _cipher_suite == TLS_RSA_WITH_AES_256_CBC_SHA)
					return HMAC(EVP_sha1(), pkeymac, 20, es.data(), es.size(), outmac, &mdlen) != NULL;
				return HMAC(EVP_sha256(), pkeymac, 32, es.data(), es.size(), outmac, &mdlen) != NULL;
			}

			bool decrypt_record(const uint8_t*pd, size_t len, uint8_t* pout, int *poutsize)
			{
				size_t maclen = 32;
				if (_cipher_suite == TLS_RSA_WITH_AES_128_CBC_SHA || _cipher_suite == TLS_RSA_WITH_AES_256_CBC_SHA)
					maclen = 20;
				if (len < 53) // 5 + pading16(IV + maclen + datasize)
					return false;

				int i;
				unsigned char sout[1024 * 20], iv[AES_BLOCK_SIZE], *pkey = _key_sw, *pkmac = _key_swmac;
				AES_KEY aes_d;
				int nkeybit = 128;
				if (_cipher_suite == TLS_RSA_WITH_AES_256_CBC_SHA256 || _cipher_suite == TLS_RSA_WITH_AES_256_CBC_SHA)
					nkeybit = 256;

				if (_bserver) {
					pkey = _key_cw;
					pkmac = _key_cwmac;
				}
				memcpy(iv, pd + 5, AES_BLOCK_SIZE);//Decrypt
				if (AES_set_decrypt_key(pkey, nkeybit, &aes_d) < 0)
					return false;
				AES_cbc_encrypt((const unsigned char*)pd + 5 + AES_BLOCK_SIZE, (unsigned char*)sout, len - 5 - AES_BLOCK_SIZE, &aes_d, iv, AES_DECRYPT);

				unsigned int ufsize = sout[len - 5 - AES_BLOCK_SIZE - 1];//verify data MAC
				if (ufsize > 15)
					return false;

				size_t datasize = len - 5 - AES_BLOCK_SIZE - 1 - ufsize - maclen;
				if (datasize > tls_rec_fragment_len)
					return false;

				unsigned char mac[32], macsrv[32];
				memcpy(macsrv, &sout[datasize], maclen);
				if (!caldatahmac(pd[0], _seqno_read, sout, datasize, pkmac, mac))
					return false;
				for (i = 0; i < (int)maclen; i++) {
					if (mac[i] != macsrv[i])
						return false;
				}

				memcpy(pout, pd, 5);
				memcpy(pout + 5, sout, datasize);
				*(pout + 3) = ((datasize >> 8) & 0xFF);
				*(pout + 4) = (datasize & 0xFF);
				*poutsize = (int)datasize + 5;
				_seqno_read++;
				return true;
			}
		protected:
			template <class _Out>
			int MKR_WithAES_BLK(_Out *pout, uint8_t rectype, const uint8_t* sblk, size_t size)
			{
				int i;
				uint8_t* pkeyw = _key_cw, *pkeywmac = _key_cwmac;
				uint8_t IV[AES_BLOCK_SIZE];//rand IV

				tlsrec ss, es;
				uint8_t tmpe[TLS_REC_BUF_SIZE];

				uint8_t mac[32];
				if (_bserver) {
					pkeyw = _key_sw;
					pkeywmac = _key_swmac;
				}

				try {
					RAND_bytes(IV, AES_BLOCK_SIZE);
					ss << rectype << (uint8_t)TLSVER_MAJOR << (uint8_t)TLSVER_NINOR << (uint16_t)0;
					ss.write(IV, AES_BLOCK_SIZE);
				}
				catch (...) {
					return -1;
				}
				if (!caldatahmac(rectype, _seqno_send, sblk, size, pkeywmac, mac))
					return -1;

				size_t maclen = 32;
				if (_cipher_suite == TLS_RSA_WITH_AES_128_CBC_SHA || _cipher_suite == TLS_RSA_WITH_AES_256_CBC_SHA)
					maclen = 20;

				try {
					es.write(sblk, size); //content
					es.write(mac, maclen); //MAC
					size_t len = es.getpos() + 1;
					if (len % AES_BLOCK_SIZE) {
						for (i = 0; i < (int)(AES_BLOCK_SIZE - (len % AES_BLOCK_SIZE)) + 1; i++)//padding and   padding_length
							es << (char)(AES_BLOCK_SIZE - (len % AES_BLOCK_SIZE));
					}
					else
						es << (char)0; //padding_length

					AES_KEY aes_e;
					int nkeybit = 128;
					if (_cipher_suite == TLS_RSA_WITH_AES_256_CBC_SHA256 || _cipher_suite == TLS_RSA_WITH_AES_256_CBC_SHA)
						nkeybit = 256;
					if (AES_set_encrypt_key(pkeyw, nkeybit, &aes_e) < 0)
						return -1;

					AES_cbc_encrypt(es.data(), tmpe, es.size(), &aes_e, IV, AES_ENCRYPT);
					ss.write(tmpe, es.size());
					ss.setpos(3) < (uint16_t)(es.size() + sizeof(IV));
				}
				catch (int) {
					return -1;
				}
				pout->append(ss.data(), ss.size());
				_seqno_send++;
				return (int)ss.size();
			}

			template <class _Out>
			bool mk_cipher(_Out *pout, uint8_t rectype, const uint8_t* pdata, size_t size)
			{
				size_t us = 0;
				while (us < size) {
					if (us + TLS_CBCBLKSIZE < size) {
						if (MKR_WithAES_BLK(pout, rectype, pdata + us, TLS_CBCBLKSIZE) < 0)
							return false;
						us += TLS_CBCBLKSIZE;
					}
					else {
						if (MKR_WithAES_BLK(pout, rectype, pdata + us, size - us) < 0)
							return false;
						break;
					}
				}
				return true;
			}

			template <class _Out>
			bool mk_nocipher(_Out *pout, int nprotocol, const void* pd, size_t size)
			{
				uint8_t s[TLS_REC_BUF_SIZE];
				const uint8_t *puc = (const uint8_t *)pd;
				size_t pos = 0, ss;

				s[0] = (uint8_t)nprotocol;
				s[1] = TLSVER_MAJOR;
				s[2] = TLSVER_NINOR;
				while (pos < size) {
					ss = TLS_CBCBLKSIZE;
					if (pos + ss > size)
						ss = size - pos;
					s[3] = (uint8_t)((ss >> 8) & 0xFF);
					s[4] = (uint8_t)(ss & 0xFF);
					pout->append(s, 5);
					pout->append(puc + pos, ss);
					pos += ss;
				}
				return true;
			}

			template <class _Out>
			bool make_package(_Out *pout, int nprotocol, const void* pd, size_t size)// make send package
			{
				if (_bsendcipher && *((uint8_t*)pd) != (uint8_t)tls::rec_alert)
					return mk_cipher(pout, (uint8_t)nprotocol, (const uint8_t*)pd, size);
				return mk_nocipher(pout, nprotocol, pd, size);
			}

			bool make_keyblock()
			{
				const char *slab = "key expansion";
				unsigned char seed[128];
				memcpy(seed, slab, strlen(slab));
				memcpy(&seed[strlen(slab)], _serverrand, 32);
				memcpy(&seed[strlen(slab) + 32], _clientrand, 32);
				if (!prf_sha256(_master_key, 48, seed, (int)strlen(slab) + 64, _key_block, 128))
					return false;
				SetCipherParam(_key_block, 128);
				return true;
			}

			template <class _Out>
			bool mkr_ClientFinished(_Out *pout)
			{
				const char* slab = "client finished";
				uint8_t hkhash[48];
				memcpy(hkhash, slab, strlen(slab));
				tlsrec  tmp;
				if (!_hmsg)
					return false;
				_hmsg->out(&tmp);
				uint8_t verfiy[32], sdata[32];
				SHA256(tmp.data(), tmp.size(), &hkhash[strlen(slab)]); //
				if (!prf_sha256(_master_key, 48, hkhash, (int)strlen(slab) + 32, verfiy, 32))
					return false;

				sdata[0] = tls::hsk_finished;
				sdata[1] = 0;
				sdata[2] = 0;
				sdata[3] = 12;
				memcpy(&sdata[4], verfiy, 12);

				_seqno_send = 0;
				_bsendcipher = true;

				if (make_package(pout, tls::rec_handshake, sdata, 16)) {
					_hmsg->_cli_finished.clear();
					_hmsg->_cli_finished.append(sdata, 16);
					return true;
				}
				return false;
			}

			template <class _Out>
			bool mkr_ServerFinished(_Out *pout)
			{
				const char* slab = "server finished";
				uint8_t hkhash[48];
				memcpy(hkhash, slab, strlen(slab));
				tlsrec tmp;
				if (!_hmsg)
					return false;
				_hmsg->out(&tmp, true);
				uint8_t verfiy[32], sdata[32];
				SHA256(tmp.data(), tmp.size(), &hkhash[strlen(slab)]); //
				if (!prf_sha256(_master_key, 48, hkhash, (int)strlen(slab) + 32, verfiy, 32))
					return false;

				sdata[0] = tls::hsk_finished;
				sdata[1] = 0;
				sdata[2] = 0;
				sdata[3] = 12;
				memcpy(&sdata[4], verfiy, 12);

				_seqno_send = 0;
				_bsendcipher = true;

				return make_package(pout, tls::rec_handshake, sdata, 16);
			}

			template <class _Out>
			void Alert(uint8_t level, uint8_t desval, _Out* pout)
			{
				pout->clear();
				uint8_t u[8] = { (uint8_t)tls::rec_alert, TLSVER_MAJOR, TLSVER_NINOR, 0, 2, level, desval, 0 };
				pout->append(u, 7);
			}
		public:
			template <class _Out>
			bool MakeAppRecord(_Out* po, const void* pd, size_t size)
			{
				if (!_bhandshake_finished || !pd || !size)
					return false;
				po->clear();
				return make_package(po, tls::rec_application_data, pd, size);
			}

			virtual void Reset()
			{
				_bhandshake_finished = false;
				_breadcipher = false;
				_bsendcipher = false;

				_seqno_send = 0;
				_seqno_read = 0;
				_cipher_suite = 0;

				_pkgtcp.clear();
				if (_hmsg)
					_hmsg->clear();
				else
					_hmsg = handshake::new_cls(_pmem);
				resetblks();
			}

			static bool prf_sha256(const uint8_t* key, int keylen, const uint8_t* seed, int seedlen, uint8_t *pout, int outlen)
			{
				int nout = 0;
				uint32_t mdlen = 0;
				uint8_t An[32], Aout[32], An_1[32];
				if (!HMAC(EVP_sha256(), key, (int)keylen, seed, seedlen, An_1, &mdlen)) // A1
					return false;
				uint8_t as[1024];
				uint8_t *ps = (uint8_t *)as;
				while (nout < outlen) {
					memcpy(ps, An_1, 32);
					memcpy(ps + 32, seed, seedlen);
					if (!HMAC(EVP_sha256(), key, (int)keylen, ps, 32 + seedlen, Aout, &mdlen))
						return false;
					if (nout + 32 < outlen) {
						memcpy(pout + nout, Aout, 32);
						nout += 32;
					}
					else {
						memcpy(pout + nout, Aout, outlen - nout);
						nout = outlen;
						break;
					}
					if (!HMAC(EVP_sha256(), key, (int)keylen, An_1, 32, An, &mdlen)) // An
						return false;
					memcpy(An_1, An, 32);
				}
				return true;
			}

			void SetCipherParam(uint8_t *pkeyblock, int nsize)
			{
				memcpy(_keyblock, pkeyblock, nsize);
				if (_cipher_suite == TLS_RSA_WITH_AES_128_CBC_SHA256) {
					memcpy(_key_cwmac, _keyblock, 32);
					memcpy(_key_swmac, &_keyblock[32], 32);
					memcpy(_key_cw, &_keyblock[64], 16);
					memcpy(_key_sw, &_keyblock[80], 16);
				}
				else if (_cipher_suite == TLS_RSA_WITH_AES_256_CBC_SHA256) {
					memcpy(_key_cwmac, _keyblock, 32);
					memcpy(_key_swmac, &_keyblock[32], 32);
					memcpy(_key_cw, &_keyblock[64], 32);
					memcpy(_key_sw, &_keyblock[96], 32);
				}
				else if (_cipher_suite == TLS_RSA_WITH_AES_128_CBC_SHA) {
					memcpy(_key_cwmac, _keyblock, 20);
					memcpy(_key_swmac, &_keyblock[20], 20);
					memcpy(_key_cw, &_keyblock[40], 16);
					memcpy(_key_sw, &_keyblock[56], 16);
				}
				else if (_cipher_suite == TLS_RSA_WITH_AES_256_CBC_SHA) {
					memcpy(_key_cwmac, _keyblock, 20);
					memcpy(_key_swmac, &_keyblock[20], 20);
					memcpy(_key_cw, &_keyblock[40], 32);
					memcpy(_key_sw, &_keyblock[72], 32);
				}
			}

			template <class _Out>
			bool mkr_ClientHelloMsg(_Out* pout)
			{
				RAND_bytes(_clientrand, sizeof(_clientrand));
				if (!_hmsg)
					return false;
				_hmsg->_cli_hello.clear();
				try {
					_hmsg->_cli_hello << ((uint8_t)tls::hsk_client_hello);  // msg type  1byte
					_hmsg->_cli_hello << ((uint8_t)0) << (uint8_t)0 << (uint8_t)0; // msg len  3byte
					_hmsg->_cli_hello << (uint8_t)TLSVER_MAJOR << (uint8_t)TLSVER_NINOR;
					_hmsg->_cli_hello.write(_clientrand, 32);// random 32byte
					_hmsg->_cli_hello << (uint8_t)0;    // SessionID = NULL   1byte
					_hmsg->_cli_hello < (uint16_t)0x08; // cipher_suites
					_hmsg->_cli_hello << (uint8_t)0 << (uint8_t)TLS_RSA_WITH_AES_256_CBC_SHA256;
					_hmsg->_cli_hello << (uint8_t)0 << (uint8_t)TLS_RSA_WITH_AES_128_CBC_SHA256;
					_hmsg->_cli_hello << (uint8_t)0 << (uint8_t)TLS_RSA_WITH_AES_256_CBC_SHA;
					_hmsg->_cli_hello << (uint8_t)0 << (uint8_t)TLS_RSA_WITH_AES_128_CBC_SHA;
					_hmsg->_cli_hello < (uint16_t)0x10; // compression_methods
				}
				catch (...) {
					return false;
				}

				*(_hmsg->_cli_hello.data() + 3) = (uint8_t)(_hmsg->_cli_hello.size() - 4);
				return make_package(pout, tls::rec_handshake, _hmsg->_cli_hello.data(), _hmsg->_cli_hello.size());
			}

			/*!
			\brief do input bytes from tcp
			return <0 : error if pout not empty is sendback Alert pkg; >0: parse records and pout has decode message
			*/
			template <class _Out>
			int  OnTcpRead(const void* pd, size_t size, _Out* pout) // return TLS_SESSION_XXX
			{
				_pkgtcp.append((const uint8_t*)pd, size);
				uint8_t *p = _pkgtcp.data(), uct, tmp[tls_rec_fragment_len + 2048];
				uint16_t ulen;
				int nl = (int)_pkgtcp.size(), nret = TLS_SESSION_NONE, ndl = 0;
				while (nl >= 5) { // type(1byte) version(2byte) length(2byte);
					uct = *p;
					ulen = p[3];
					ulen = (ulen << 8) + p[4];
					if (uct < (uint8_t)tls::rec_change_cipher_spec || uct >(uint8_t)tls::rec_application_data ||
						p[1] != TLSVER_MAJOR || ulen > tls_rec_fragment_len + 64 || p[2] > TLSVER_NINOR) {
						if (_plog) {
							char stmp[1024];
							int nsize = nl > 128 ? 128 : nl;
							_plog->add(CLOG_DEFAULT_DBG, "ucid(%u) TLS record error top128 %d bytes.\n%s", _ucid, nl,
								bin2view(p, nsize, stmp, sizeof(stmp)));
						}
						if (!_breadcipher)
							Alert(2, 70, pout);//protocol_version(70)
						return TLS_SESSION_ERR;
					}
					if (ulen + 5 > nl)
						break;
					if (_breadcipher) {
						if (decrypt_record(p, ulen + 5, tmp, &ndl)) {
							nret = dorecord(tmp, ndl, pout);
							if (nret == TLS_SESSION_ERR)
								return nret;
						}
						else {
							if (_plog) {
								char stmp[1024];
								int nsize = ulen + 5 > 128 ? 128 : ulen + 5;
								_plog->add(CLOG_DEFAULT_DBG, "ucid(%u) Alert decode_error(50) : record size %u, top128\n%s", _ucid, ulen + 5,
									bin2view(p, nsize, stmp, sizeof(stmp)));
							}
							return TLS_SESSION_ERR;
						}
					}
					else {
						nret = dorecord(p, (int)ulen + 5, pout);
						if (nret == TLS_SESSION_ERR)
							return nret;
					}
					nl -= (int)ulen + 5;
					p += (int)ulen + 5;
				}
				_pkgtcp.erase(0, _pkgtcp.size() - nl);
				_pkgtcp.shrink_to_fit();
				return nret;
			}
		protected:
			virtual int dorecord(const uint8_t* prec, size_t sizerec, bytes* pout) = 0;
		};

		class sessionclient : public session // session for client
		{
		public:
			sessionclient(uint32_t ucid, memory* pmem, ilog* plog) : session(false, ucid, pmem, plog), _pkgm(pmem)
			{
				_prsa = 0;
				_pevppk = 0;
				_px509 = 0;
				_pubkeylen = 0;
				_pkgm.reserve(TLS_REC_BUF_SIZE);
			}
			virtual ~sessionclient()
			{
				if (_prsa)
					RSA_free(_prsa);
				if (_pevppk)
					EVP_PKEY_free(_pevppk);
				if (_px509)
					X509_free(_px509);
				_prsa = 0;
				_pevppk = 0;
				_px509 = 0;
			}
		protected:
			RSA *_prsa;
			EVP_PKEY *_pevppk;
			X509* _px509;
			int _pubkeylen;//The server pubkey length，0 for not use
			unsigned char _pubkey[1024];//The server pubkey is used to verify the server legitimacy
		private:
			bytes _pkgm;
		public:
			bool SetServerPubkey(int len, const unsigned char *pubkey)
			{
				if (!pubkey || len > (int)sizeof(_pubkey))
					return false;
				_pubkeylen = len;
				memcpy(_pubkey, pubkey, len);
				return true;
			}

			bool SetServerCa(const char* scafile)
			{
				array<uint8_t, 8192> pkey;
				if (!get_cert_pkey(scafile, &pkey))
					return false;
				return SetServerPubkey((int)pkey.size(), pkey.data());
			}

			virtual void Reset()
			{
				session::Reset();
				if (_prsa)
					RSA_free(_prsa);
				if (_pevppk)
					EVP_PKEY_free(_pevppk);
				if (_px509)
					X509_free(_px509);
				_prsa = 0;
				_pevppk = 0;
				_px509 = 0;
				_pkgm.clear();
			}

		private:
			template <class _Out>
			bool mkr_ClientKeyExchange(_Out *po)
			{
				if (!_hmsg)
					return false;
				unsigned char premasterkey[48], out[512];
				premasterkey[0] = 3;
				premasterkey[1] = 3;
				RAND_bytes(&premasterkey[2], 46); //calculate pre_master_key

				const char* slab = "master secret";//calculate master_key
				unsigned char seed[128];
				memcpy(seed, slab, strlen(slab));
				memcpy(&seed[strlen(slab)], _clientrand, 32);
				memcpy(&seed[strlen(slab) + 32], _serverrand, 32);
				if (!prf_sha256(premasterkey, 48, seed, (int)strlen(slab) + 64, _master_key, 48))
					return false;

				if (!make_keyblock()) //calculate key_block
					return false;

				int nbytes = RSA_public_encrypt(48, premasterkey, out, _prsa, RSA_PKCS1_PADDING);
				if (nbytes < 0)
					return false;
				_hmsg->_cli_key_exchange.clear();
				uint8_t uh[4] = { (uint8_t)(tls::hsk_client_key_exchange), (uint8_t)(((uint32_t)nbytes >> 16) & 0xFF),
								  (uint8_t)(((uint32_t)nbytes >> 8) & 0xFF),	(uint8_t)((uint32_t)nbytes & 0xFF)
				};
				_hmsg->_cli_key_exchange.append(uh, 4);
				_hmsg->_cli_key_exchange.append(out, nbytes);
				return make_package(po, tls::rec_handshake, _hmsg->_cli_key_exchange.data(), _hmsg->_cli_key_exchange.size());
			}

			bool OnServerHello(unsigned char* phandshakemsg, size_t size)
			{
				if (!_hmsg)
					return false;
				if (size > _hmsg->_srv_hello.capacity())
					return false;
				_hmsg->_srv_hello.clear();
				_hmsg->_srv_hello.append(phandshakemsg, size);

				if (_hmsg->_srv_hello.size() < 40u)
					return false;
				unsigned char* puc = _hmsg->_srv_hello.data();
				uint32_t ulen = puc[1];
				ulen = (ulen << 8) + puc[2];
				ulen = (ulen << 8) + puc[3];

				puc += 6;
				memcpy(_serverrand, puc, 32);
				puc += 32;

				int n = *puc++;
				puc += n;

				if (n + 40 > (int)_hmsg->_srv_hello.size())
					return false;

				_cipher_suite = *puc++;
				_cipher_suite = (_cipher_suite << 8) | *puc++;
				return true;
			}

			bool OnServerCertificate(unsigned char* phandshakemsg, size_t size)
			{
				if (!_hmsg)
					return false;
				_hmsg->_srv_certificate.clear();
				_hmsg->_srv_certificate.append(phandshakemsg, size);

				if (!_hmsg->_srv_certificate.size())
					return false;
				const unsigned char* p = _hmsg->_srv_certificate.data();//, *pend = 0;

				uint32_t ulen = p[7];
				ulen = (ulen << 8) + p[8];
				ulen = (ulen << 8) + p[9];
				p += 10;
				_px509 = d2i_X509(NULL, &p, (long)ulen);//only use first Certificate
				if (!_px509)
					return false;
				if (_pubkeylen) { // need to verify the server legitimacy
					bool bok = true;
					int i;
					if (_px509->cert_info->key->public_key->length != _pubkeylen)
						bok = false;
					else {
						for (i = 0; i < _px509->cert_info->key->public_key->length; i++) {
							if (_px509->cert_info->key->public_key->data[i] != _pubkey[i]) {
								bok = false;
								break;
							}
						}
					}
					if (!bok) {
						X509_free(_px509);
						_px509 = 0;
						return false;
					}
				}
				_pevppk = X509_get_pubkey(_px509);
				if (!_pevppk) {
					X509_free(_px509);
					_px509 = 0;
					return false;
				}
				_prsa = EVP_PKEY_get1_RSA(_pevppk);
				if (!_prsa) {
					EVP_PKEY_free(_pevppk);
					X509_free(_px509);
					_pevppk = 0;
					_px509 = 0;
					return false;
				}
				return  true;
			}

			template <class _Out>
			bool  OnServerHelloDone(uint8_t* phandshakemsg, size_t size, _Out* pout)
			{
				if (!_hmsg)
					return false;
				_hmsg->_srv_hellodone.clear();
				_hmsg->_srv_hellodone.append(phandshakemsg, size);
				if (!mkr_ClientKeyExchange(pout))
					return false;
				unsigned char change_cipher_spec = 1;// send change_cipher_spec
				make_package(pout, tls::rec_change_cipher_spec, &change_cipher_spec, 1);
				if (!mkr_ClientFinished(pout))
					return false;
				return true;
			}

			template <class _Out>
			bool OnServerFinished(uint8_t* phandshakemsg, size_t size, _Out* pout)
			{
				if (!_hmsg)
					return false;
				const char* slab = "server finished";
				uint8_t hkhash[48];
				memcpy(hkhash, slab, strlen(slab));
				array<uint8_t, TLS_REC_BUF_SIZE> tmp;
				_hmsg->out(&tmp, true);
				uint8_t verfiy[32];
				SHA256(tmp.data(), tmp.size(), &hkhash[strlen(slab)]); //
				if (!prf_sha256(_master_key, 48, hkhash, (int)strlen(slab) + 32, verfiy, 32))
					return false;

				int i;
				for (i = 0; i < 12; i++) {
					if (verfiy[i] != phandshakemsg[4 + i]) {
						Alert(2, 40, pout);//handshake_failure(40)
						return false;
					}
				}
				handshake::del_cls(_hmsg, _pmem);//Handshake completed, delete Handshake message
				_hmsg = nullptr;
				return true;
			}

		protected:
			virtual int dorecord(const uint8_t* prec, size_t sizerec, bytes * pout) // return TLS_SESSION_XXX
			{
				const uint8_t* p = (const uint8_t*)prec;
				uint16_t ulen = p[3];
				ulen = (ulen << 8) + p[4];

				if (p[0] == tls::rec_handshake)
					return dohandshakemsg(p + 5, sizerec - 5, pout);
				else if (p[0] == tls::rec_alert) {
					if (_plog) {
						char so[512];
						_plog->add(CLOG_DEFAULT_WRN, "TLS client Alert level = %d, AlertDescription = %d,size = %zu\n%s", p[5], p[6], sizerec,
							bin2view(prec, sizerec, so, sizeof(so)));
					}
				}
				else if (p[0] == tls::rec_change_cipher_spec) {
					_breadcipher = true;
					_seqno_read = 0;
					if (_plog)
						_plog->add(CLOG_DEFAULT_DBG, "TLS client server change_cipher_spec");
				}
				else if (p[0] == tls::rec_application_data) {
					pout->append(p + 5, (int)sizerec - 5);
					return TLS_SESSION_APPDATA;
				}
				return TLS_SESSION_NONE;
			}

			template <class _Out>
			int dohandshakemsg(const uint8_t* prec, size_t sizerec, _Out* pout)
			{
				_pkgm.append((const unsigned char*)prec, sizerec);
				int nl = (int)_pkgm.size(), nret = TLS_SESSION_NONE;
				unsigned char* p = _pkgm.data();
				while (nl >= 4) {
					uint32_t ulen = p[1];
					ulen = (ulen << 8) + p[2];
					ulen = (ulen << 8) + p[3];
					if (ulen > 1024 * 16)
						return TLS_SESSION_ERR;
					if ((int)ulen + 4 > nl)
						break;
					switch (p[0]) {
					case tls::hsk_server_hello:
						if (!OnServerHello(p, ulen + 4)) {
							if (_plog)
								_plog->add(CLOG_DEFAULT_DBG, "TLS client sever hello package error, size=%u", ulen + 4);
							return TLS_SESSION_ERR;
						}
						break;
					case tls::hsk_certificate:
						if (!OnServerCertificate(p, ulen + 4))
							return TLS_SESSION_ERR;
						break;
					case tls::hsk_server_key_exchange:
						if (_plog)
							_plog->add(CLOG_DEFAULT_DBG, "TLS client hsk_server_key_exchange size=%u", ulen + 4);
						break;
					case tls::hsk_certificate_request:
						if (_plog)
							_plog->add(CLOG_DEFAULT_DBG, "TLS client hsk_certificate_request size=%u", ulen + 4);
						break;
					case tls::hsk_server_hello_done:
						if (!OnServerHelloDone(p, ulen + 4, pout))
							return TLS_SESSION_ERR;
						break;
					case tls::hsk_finished:
						if (_plog)
							_plog->add(CLOG_DEFAULT_DBG, "TLS client hsk_finished size=%u", ulen + 4);
						if (!OnServerFinished(p, ulen + 4, pout))
							return TLS_SESSION_ERR;
						if (_plog)
							_plog->add(CLOG_DEFAULT_DBG, "TLS client server hsk_finished chech success");
						_bhandshake_finished = true;
						nret = TLS_SESSION_HKOK;
						break;
					default:
						if (_plog)
							_plog->add(CLOG_DEFAULT_ERR, "TLS client unkown msgtype = %u", p[0]);
						return TLS_SESSION_ERR;
					}
					nl -= (int)ulen + 4;
					p += (int)ulen + 4;
				}
				_pkgm.erase(0, _pkgm.size() - nl);
				_pkgm.shrink_to_fit();
				return nret;
			}
		};

		class sessionserver : public session // session for server
		{
		public:
			sessionserver(uint32_t ucid, const void* pcer, size_t cerlen,
				const void* pcerroot, size_t cerrootlen, std::mutex *pRsaLck, RSA* pRsaPrivate, memory* pmem, ilog* plog
			) : session(true, ucid, pmem, plog),
				_pkgm(pmem)
			{
				_pcer = pcer;
				_cerlen = cerlen;
				_pcerroot = pcerroot;
				_cerrootlen = cerrootlen;
				_pRsaLck = pRsaLck;
				_pRsaPrivate = pRsaPrivate;
				memset(_sip, 0, sizeof(_sip));
				_pkgm.reserve(TLS_REC_BUF_SIZE);
			}
			sessionserver(sessionserver*p) : session(p), _pkgm(p->_pmem)
			{
				_pRsaLck = p->_pRsaLck;
				_pRsaPrivate = p->_pRsaPrivate;

				_pcer = p->_pcer;
				_cerlen = p->_cerlen;
				_pcerroot = p->_pcerroot;
				_cerrootlen = p->_cerrootlen;
				memcpy(_sip, p->_sip, sizeof(_sip));

				_pkgm.reserve(TLS_REC_BUF_SIZE);
				_pkgm.append(p->_pkgm.data(), p->_pkgm.size());
			}
			virtual ~sessionserver()
			{
			}
		protected:
			std::mutex * _pRsaLck;
			RSA* _pRsaPrivate;

			const void* _pcer;
			size_t _cerlen;
			const void* _pcerroot;
			size_t _cerrootlen;
			char _sip[32];
		private:
			bytes _pkgm;
		public:
			inline void SetIP(const char* sip)
			{
				strlcpy(_sip, sip, sizeof(_sip));
			}

			inline void getip(char *sout, size_t sizeout)
			{
				strlcpy(sout, _sip, sizeout);
			}

		protected:
			bool MakeServerHello()
			{
				if (!_hmsg)
					return false;
				RAND_bytes(_serverrand, sizeof(_serverrand));
				_hmsg->_srv_hello.clear();
				try {
					_hmsg->_srv_hello << (uint8_t)tls::hsk_server_hello;
					_hmsg->_srv_hello << (uint16_t)0 << (uint8_t)0;
					_hmsg->_srv_hello << (uint8_t)TLSVER_MAJOR << (uint8_t)TLSVER_NINOR;
					_hmsg->_srv_hello.write(_serverrand, 32);// random 32byte
					_hmsg->_srv_hello << (uint8_t)4;
					_hmsg->_srv_hello < _ucid;
					_hmsg->_srv_hello << (uint8_t)0;
					_hmsg->_srv_hello << (uint8_t)(_cipher_suite & 0xFF);
					_hmsg->_srv_hello << (uint8_t)0;
				}
				catch (...) {
					return false;
				}
				*(_hmsg->_srv_hello.data() + 3) = (uint8_t)(_hmsg->_srv_hello.size() - 4);
				return true;
			}

			bool MakeCertificateMsg()
			{
				if (!_hmsg)
					return false;
				_hmsg->_srv_certificate.clear();
				_hmsg->_srv_certificate.push_back((uint8_t)tls::hsk_certificate);
				_hmsg->_srv_certificate.push_back((uint8_t)0);
				_hmsg->_srv_certificate.push_back((uint8_t)0);
				_hmsg->_srv_certificate.push_back((uint8_t)0);//1,2,3

				uint32_t u;
				if (_pcerroot && _cerrootlen) {
					u = (uint32_t)(_cerlen + _cerrootlen + 6);
					_hmsg->_srv_certificate.push_back((uint8_t)((u >> 16) & 0xFF));
					_hmsg->_srv_certificate.push_back((uint8_t)((u >> 8) & 0xFF));
					_hmsg->_srv_certificate.push_back((uint8_t)(u & 0xFF));//4,5,6

					u = (uint32_t)_cerlen;
					_hmsg->_srv_certificate.push_back((uint8_t)((u >> 16) & 0xFF));
					_hmsg->_srv_certificate.push_back((uint8_t)((u >> 8) & 0xFF));
					_hmsg->_srv_certificate.push_back((uint8_t)(u & 0xFF));//7,8,9
					_hmsg->_srv_certificate.append((const uint8_t*)_pcer, _cerlen);

					u = (uint32_t)_cerrootlen;
					_hmsg->_srv_certificate.push_back((uint8_t)((u >> 16) & 0xFF));
					_hmsg->_srv_certificate.push_back((uint8_t)((u >> 8) & 0xFF));
					_hmsg->_srv_certificate.push_back((uint8_t)(u & 0xFF));
					_hmsg->_srv_certificate.append((const uint8_t*)_pcerroot, _cerrootlen);
				}
				else {
					u = (uint32_t)_cerlen + 3;
					_hmsg->_srv_certificate.push_back((uint8_t)((u >> 16) & 0xFF));
					_hmsg->_srv_certificate.push_back((uint8_t)((u >> 8) & 0xFF));
					_hmsg->_srv_certificate.push_back((uint8_t)(u & 0xFF));//4,5,6

					u = (uint32_t)_cerlen;
					_hmsg->_srv_certificate.push_back((uint8_t)((u >> 16) & 0xFF));
					_hmsg->_srv_certificate.push_back((uint8_t)((u >> 8) & 0xFF));
					_hmsg->_srv_certificate.push_back((uint8_t)(u & 0xFF));//7,8,9
					_hmsg->_srv_certificate.append((const uint8_t*)_pcer, _cerlen);
				}

				u = (uint32_t)_hmsg->_srv_certificate.size() - 4;
				*(_hmsg->_srv_certificate.data() + 1) = (uint8_t)((u >> 16) & 0xFF);
				*(_hmsg->_srv_certificate.data() + 2) = (uint8_t)((u >> 8) & 0xFF);
				*(_hmsg->_srv_certificate.data() + 3) = (uint8_t)((u >> 0) & 0xFF);
				return true;
			}

			template <class _Out>
			bool OnClientHello(uint8_t* phandshakemsg, size_t size, _Out* po)
			{
				if (!_hmsg || size > _hmsg->_cli_hello.capacity())
					return false;
				_hmsg->_cli_hello.clear();
				_hmsg->_cli_hello.append(phandshakemsg, size);

				unsigned char* puc = phandshakemsg, uct;
				size_t ulen = puc[1];
				ulen = (ulen << 8) + puc[2];
				ulen = (ulen << 8) + puc[3];

				if (size != ulen + 4 || size < 12 + 32) {
					Alert(2, 10, po);//unexpected_message(10)
					return false;
				}
				if (puc[4] != TLSVER_MAJOR || puc[5] < TLSVER_NINOR) {
					if (_plog)
						_plog->add(CLOG_DEFAULT_DBG, "ucid(%u) client Hello Ver %d.%d", _ucid, puc[4], puc[5]);
					Alert(2, 70, po);//protocol_version(70),
					return false;
				}
				stream ss(phandshakemsg, size);
				unsigned short i, cipherlen = 0;
				try {
					ss.setpos(6).read(_clientrand, 32) >> uct; //session id len
					if (uct > 0)
						ss.setpos(ss.getpos() + uct);
					ss > cipherlen;
				}
				catch (int) {
					return false;
				}
				if (ss.getpos() + cipherlen > size) {
					Alert(2, 10, po);//unexpected_message(10)
					return false;
				}
				_cipher_suite = 0;
				unsigned char* pch = phandshakemsg + ss.getpos();
				if (_plog) {
					char so[512];
					_plog->add(CLOG_DEFAULT_DBG, "ucid(%u) client ciphers : \n%s ", _ucid, bin2view(pch, cipherlen, so, sizeof(so)));
				}
				for (i = 0; i < cipherlen; i += 2) {
					if (pch[i] == 0 && (pch[i + 1] == TLS_RSA_WITH_AES_128_CBC_SHA256 || pch[i + 1] == TLS_RSA_WITH_AES_256_CBC_SHA256
						|| pch[i + 1] == TLS_RSA_WITH_AES_128_CBC_SHA || pch[i + 1] == TLS_RSA_WITH_AES_256_CBC_SHA)
						) {
						_cipher_suite = pch[i + 1];
						break;
					}
				}
				if (!_cipher_suite) {
					Alert(2, 40, po);//handshake_failure(40)
					return false;
				}
				if (_plog)
					_plog->add(CLOG_DEFAULT_DBG, "ucid(%u) server cipher = (%02x,%02x)", _ucid, (_cipher_suite >> 8) & 0xFF, _cipher_suite & 0xFF);

				MakeServerHello();
				MakeCertificateMsg();
				uint8_t umsg[4] = { tls::hsk_server_hello_done, 0, 0, 0 };
				make_package(po, tls::rec_handshake, _hmsg->_srv_hello.data(), _hmsg->_srv_hello.size());// ServerHello
				make_package(po, tls::rec_handshake, _hmsg->_srv_certificate.data(), _hmsg->_srv_certificate.size());//Certificate
				_hmsg->_srv_hellodone.clear();
				_hmsg->_srv_hellodone.append(umsg, 4);
				make_package(po, tls::rec_handshake, umsg, 4);//ServerHelloDone
				return true;
			}

			template <class _Out>
			bool OnClientKeyExchange(const uint8_t* pmsg, size_t sizemsg, _Out* po)
			{
				if (!_hmsg || sizemsg > _hmsg->_cli_key_exchange.capacity())
					return false;
				_hmsg->_cli_key_exchange.clear();
				_hmsg->_cli_key_exchange.append(pmsg, sizemsg);

				uint32_t ulen = pmsg[1];//private key decode
				ulen = (ulen << 8) | pmsg[2];
				ulen = (ulen << 8) | pmsg[3];

				if (ulen + 4 != sizemsg) {
					Alert(2, 10, po);//unexpected_message(10)
					return false;
				}

				int nbytes = 0;
				unsigned char premasterkey[48];
				if (ulen % 16) {
					uint32_t ulen = pmsg[4];//private key decode
					ulen = (ulen << 8) | pmsg[5];
					_pRsaLck->lock();
					nbytes = RSA_private_decrypt((int)ulen, pmsg + 6, premasterkey, _pRsaPrivate, RSA_PKCS1_PADDING);
					_pRsaLck->unlock();
				}
				else {
					_pRsaLck->lock();
					nbytes = RSA_private_decrypt((int)ulen, pmsg + 4, premasterkey, _pRsaPrivate, RSA_PKCS1_PADDING);
					_pRsaLck->unlock();
				}

				if (nbytes != 48) {
					Alert(2, 21, po);//decryption_failed(21),
					return false;
				}

				const char* slab = "master secret";//calculate master_key
				uint8_t seed[128];
				memcpy(seed, slab, strlen(slab));
				memcpy(&seed[strlen(slab)], _clientrand, 32);
				memcpy(&seed[strlen(slab) + 32], _serverrand, 32);
				if (!prf_sha256(premasterkey, 48, seed, (int)strlen(slab) + 64, _master_key, 48)) {
					Alert(2, 80, po);//internal_error(80),
					return false;
				}

				if (!make_keyblock()) { //calculate key_block
					Alert(2, 80, po);//internal_error(80),
					return false;
				}
				return true;
			}

			template <class _Out>
			bool OnClientFinish(const uint8_t* pmsg, size_t sizemsg, _Out* po)
			{
				if (!_hmsg)
					return false;
				const char* slab = "client finished";
				uint8_t hkhash[48];
				memcpy(hkhash, slab, strlen(slab));
				tlsrec tmp;
				_hmsg->out(&tmp);
				unsigned char verfiy[32];
				SHA256(tmp.data(), tmp.size(), &hkhash[strlen(slab)]); //
				if (!prf_sha256(_master_key, 48, hkhash, (int)strlen(slab) + 32, verfiy, 32)) {
					Alert(2, 80, po);//internal_error(80),
					return false;
				}

				size_t len = pmsg[1];
				len = (len << 8) | pmsg[2];
				len = (len << 8) | pmsg[3];

				if (len + 4 != sizemsg || len != 12) {
					Alert(2, 10, po);//unexpected_message(10)
					return false;
				}
				int i;
				for (i = 0; i < 12; i++) {
					if (verfiy[i] != pmsg[4 + i]) {
						Alert(2, 40, po);//handshake_failure(40)
						return false;
					}
				}

				unsigned char change_cipher_spec = 1;//send change_cipher_spec
				make_package(po, tls::rec_change_cipher_spec, &change_cipher_spec, 1);

				_seqno_send = 0;
				_bsendcipher = true;
				_hmsg->_cli_finished.clear();
				_hmsg->_cli_finished.append(pmsg, sizemsg);
				if (_plog)
					_plog->add(CLOG_DEFAULT_DBG, "ucid(%u) rec_change_cipher_spec success!", _ucid);
				if (!mkr_ServerFinished(po))
					return false;
				if (_plog)
					_plog->add(CLOG_DEFAULT_DBG, "ucid(%u) ClientFinished success!", _ucid);
				handshake::del_cls(_hmsg, _pmem);//Handshake completed, delete Handshake message
				_hmsg = nullptr;
				return true;
			}

			virtual int dorecord(const uint8_t* prec, size_t sizerec, bytes* po) // return TLS_SESSION_XXX
			{
				const unsigned char* p = (const unsigned char*)prec;
				uint16_t ulen = p[3];
				ulen = (ulen << 8) + p[4];

				if (p[0] == tls::rec_handshake)
					return dohandshakemsg(p + 5, sizerec - 5, po);
				else if (p[0] == tls::rec_alert) {
					if (_plog) {
						char so[512];
						_plog->add(CLOG_DEFAULT_WRN, "ucid(%u) Alert level = %d,AlertDescription = %d,size = %zu\n%s", _ucid, p[5], p[6], sizerec,
							bin2view(prec, sizerec, so, sizeof(so)));
					}
				}
				else if (p[0] == tls::rec_change_cipher_spec) {
					_breadcipher = true;
					_seqno_read = 0;
					if (_plog)
						_plog->add(CLOG_DEFAULT_DBG, "ucid(%u) server change_cipher_spec", _ucid);
				}
				else if (p[0] == tls::rec_application_data) {
					po->append(p + 5, sizerec - 5u);
					return TLS_SESSION_APPDATA;
				}
				return TLS_SESSION_NONE;
			}

			template <class _Out>
			int dohandshakemsg(const uint8_t* prec, size_t sizerec, _Out* po)
			{
				_pkgm.append((const unsigned char*)prec, sizerec);
				int nl = (int)_pkgm.size(), nret = TLS_SESSION_NONE;
				unsigned char* p = _pkgm.data();
				while (nl >= 4) {
					uint32_t ulen = p[1];
					ulen = (ulen << 8) + p[2];
					ulen = (ulen << 8) + p[3];
					if (ulen > 8192) {
						if (_plog)
							_plog->add(CLOG_DEFAULT_ERR, "ucid(%u) read handshake message datasize error size=%u", _ucid, ulen);
						return TLS_SESSION_ERR;
					}
					if ((int)ulen + 4 > nl)
						break;
					switch (p[0]) {
					case tls::hsk_client_hello:
						if (_plog)
							_plog->add(CLOG_DEFAULT_DBG, "ucid(%u) read hsk_client_hello size=%u", _ucid, ulen + 4);
						if (!OnClientHello(p, ulen + 4, po)) {
							if (_plog)
								_plog->add(CLOG_DEFAULT_ERR, "ucid(%u) client hsk_client_hello failed", _ucid);
							return -1;
						}
						break;
					case tls::hsk_client_key_exchange:
						if (_plog)
							_plog->add(CLOG_DEFAULT_DBG, "ucid(%u) read hsk_client_key_exchange size=%u", _ucid, ulen + 4);
						if (!OnClientKeyExchange(p, ulen + 4, po)) {
							if (_plog)
								_plog->add(CLOG_DEFAULT_ERR, "ucid(%u) client hsk_client_key_exchange failed", _ucid);
							return TLS_SESSION_ERR;
						}
						break;
					case tls::hsk_finished:
						if (_plog)
							_plog->add(CLOG_DEFAULT_DBG, "ucid(%u) read hsk_finished size=%u", _ucid, ulen + 4);
						if (!OnClientFinish(p, ulen + 4, po)) {
							if (_plog)
								_plog->add(CLOG_DEFAULT_ERR, "ucid(%u) client hsk_finished failed", _ucid);
							return -1;
						}
						_bhandshake_finished = true;
						nret = TLS_SESSION_HKOK;
						break;
					default:
						if (_plog)
							_plog->add(CLOG_DEFAULT_DBG, "ucid(%u) unkown msgtype=%u", _ucid, p[0]);
						return TLS_SESSION_ERR;
					}
					nl -= (int)ulen + 4;
					p += (int)ulen + 4;
				}
				_pkgm.erase(0, _pkgm.size() - nl);
				_pkgm.shrink_to_fit();
				return nret;
			}
		};

		class srvca
		{
		public:
			RSA * _pRsaPub;
			RSA* _pRsaPrivate;

			EVP_PKEY *_pevppk;
			X509* _px509;

			std::bytes _pcer;
			std::bytes _prootcer;

			std::mutex _csRsa;
		public:
			srvca() : _pRsaPub(nullptr), _pRsaPrivate(nullptr), _pevppk(nullptr), _px509(nullptr)
			{
				_pcer.reserve(4096);
				_prootcer.reserve(8192);
			}
			~srvca()
			{
				if (_pRsaPrivate)
					RSA_free(_pRsaPrivate);
				if (_pRsaPub)
					RSA_free(_pRsaPub);
				if (_pevppk)
					EVP_PKEY_free(_pevppk);
				if (_px509)
					X509_free(_px509);
				_pRsaPub = nullptr;
				_pRsaPrivate = nullptr;
				_pevppk = nullptr;
				_px509 = nullptr;
			}
			bool InitCert(const char* filecert, const char* filerootcert, const char* fileprivatekey)
			{
				unsigned char stmp[4096];
				FILE* pf = fopen(filecert, "rb");
				if (!pf)
					return false;
				size_t size;
				_pcer.clear();
				_prootcer.clear();
				while (!feof(pf)) {
					size = fread(stmp, 1, sizeof(stmp), pf);
					_pcer.append(stmp, size);
				}
				fclose(pf);

				if (filerootcert && *filerootcert) {
					pf = fopen(filerootcert, "rb");
					if (!pf)
						return false;

					while (!feof(pf)) {
						size = fread(stmp, 1, sizeof(stmp), pf);
						_prootcer.append(stmp, size);
					}
					fclose(pf);
				}

				pf = fopen(fileprivatekey, "rb");
				if (!pf)
					return false;

				_pRsaPrivate = PEM_read_RSAPrivateKey(pf, 0, NULL, NULL);
				fclose(pf);

				const unsigned char* p = _pcer.data();
				_px509 = d2i_X509(NULL, &p, (long)_pcer.size());//only use first Certificate
				if (!_px509)
					return false;

				_pevppk = X509_get_pubkey(_px509);
				if (!_pevppk) {
					X509_free(_px509);
					_px509 = 0;
					return false;
				}
				_pRsaPub = EVP_PKEY_get1_RSA(_pevppk);
				if (!_pRsaPub) {
					EVP_PKEY_free(_pevppk);
					X509_free(_px509);
					_pevppk = 0;
					_px509 = 0;
					return false;
				}
				return true;
			}
		};
	}// tls
}// ec
