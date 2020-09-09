/*!
\file ec_tcpclient.h
\author	jiangyong
\email  kipway@outlook.com
\update 2020.9.6

tcp_c
	a class for tcp client, support socks5 proxy, asynchronous connection

eclib 3.0 Copyright (c) 2017-2030, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/
#pragma once
#include <stdint.h>
#include <thread>
#include <time.h>
#include "ec_string.h"
#include "ec_netio.h"
namespace ec
{
	class tcp_c
	{
	public:
		enum st_sock {
			st_invalid = 0,
			st_connect = 1, // connecting...
			st_s5handshake = 2,  // socks5 handshake...
			st_s5request = 3,  // socks5 requesting...
			st_connected = 4   // connected
		};
	protected:
		st_sock	_status;
		SOCKET _sock;
		char _sip[40]; //dest IP or socks5 proxy IP
		uint16_t _uport; //dest port or socks5 proxy port

		char _s5domain[40]; //socks5 domain parameter
		uint16_t _s5port; //socks5 port parameter

		int _timeover_connect_sec;//connect timeout seconds
	private:
		time_t _timeconnect; //start connect time.
		bytes _rs5buf; // buf for socks5 request
		bool _btcpnodelay;  // default false
		bool _btcpkeepalive; // default true
	public:
		tcp_c() : _status(st_invalid)
			, _sock(INVALID_SOCKET)
			, _sip{ 0 }
			, _uport(0)
			, _s5domain{ 0 }
			, _s5port(0)
			, _btcpnodelay(false)
			, _btcpkeepalive(true)
		{
			_timeconnect = ::time(nullptr);
			_timeover_connect_sec = 8;
			_rs5buf.reserve(1024);
		}

		void set_tcp(bool bnodelay, bool bkeepalive) //call before  open
		{
			_btcpnodelay = bnodelay;
			_btcpkeepalive = bkeepalive;
		}

		virtual ~tcp_c()
		{
			if (INVALID_SOCKET == _sock) {
				::closesocket(_sock);
				_sock = INVALID_SOCKET;
			}
			_status = st_invalid;
		}
		/*!
		\brief connect asynchronous , support socks5 proxy
		\param sip dest IP or socks5 proxy IP
		\param uport dest port or socks5 proxy port
		\param timeoverseconds timeout seconds
		\param sdomain socks5 domain parameter，nullptr not use socks5,domain format "google.com"
		\param s5portsocks5 port parameter, 0 not use socks5
		*/
		bool open(const char *sip, uint16_t uport, int timeoverseconds = 8, const char* sdomain = nullptr, uint16_t s5port = 0)
		{
			if (INVALID_SOCKET != _sock)
				return true;

			_timeover_connect_sec = timeoverseconds;
			_timeconnect = ::time(nullptr);

			if (!sip || !*sip || !uport)
				return false;

			ec::strlcpy(_sip, sip, sizeof(_sip));
			_uport = uport;

			ec::strlcpy(_s5domain, sdomain, sizeof(_s5domain));
			_s5port = s5port;

			int st = -1;
			_sock = net::tcpconnectasyn(_sip, _uport, st);
			if (INVALID_SOCKET == _sock)
				return false;

			_rs5buf.clear();
			_rs5buf.reserve(1024);
			if (st) { //connecting
#ifdef _WIN32
				if (WSAEWOULDBLOCK != WSAGetLastError()) {
					::closesocket(_sock);
					_sock = INVALID_SOCKET;
					return false;
				}
#else
				if (EINPROGRESS != errno) {
					::closesocket(_sock);
					_sock = INVALID_SOCKET;
					return false;
				}
#endif
				_status = st_connect;//connecting...
				return true;
			}

			if (_s5domain[0] && _s5port) {
				if (!sendsock5handshake()) {
					connectfailed();
					return false;
				}
				_status = st_s5handshake;
				return true;
			}

			_status = st_connected;
			onconnected();
			return true;
		}

		void close()
		{
			if (INVALID_SOCKET != _sock) {
				::closesocket(_sock);
				_sock = INVALID_SOCKET;
				_status = st_invalid;
				ondisconnected();
			}
		}

		virtual int sendbytes(const void* p, int nlen)
		{
			if (INVALID_SOCKET == _sock || st_connected != _status)
				return -1;
			int ns = net::tcpsend(_sock, p, nlen);
			if (ns < 0)
				close();
			return ns;
		}

		inline int get_tcp_status()
		{
			return _status;
		}

	protected:
		virtual void onconnected()
		{
			if (_btcpnodelay)
				ec::net::tcpnodelay(_sock);
			if (_btcpkeepalive)
				ec::net::setkeepalive(_sock);
		}

		virtual void onconnectfailed()
		{
		}

		virtual void ondisconnected()
		{
		}

		virtual void onreadbytes(const uint8_t* p, int nbytes) = 0;

		virtual void onidle()
		{
		}
	public:
		/*!
		\brief run time
		\param nmsec waitIO event millisecond(1/1000 second)
		*/
		void runtime(int nmsec)
		{
			onidle();
			if (st_invalid == _status || INVALID_SOCKET == _sock) {
				if (nmsec > 0)
					std::this_thread::sleep_for(std::chrono::milliseconds(nmsec));
				return;
			}
			if (st_connect == _status)
				doconnect(nmsec);
			else if (st_s5handshake == _status)
				dosocks5handshake(nmsec);
			else if (st_s5request == _status)
				dosocks5request(nmsec);
			else if (st_connected == _status)
				doread(nmsec);
		}

	private:
		void connectfailed()
		{
			::closesocket(_sock);
			_sock = INVALID_SOCKET;
			_status = st_invalid;
			onconnectfailed();
		}

		bool sendsock5handshake()
		{
			uint8_t frm[4];
			frm[0] = 0x05;//VER
			frm[1] = 0x01;//NMETHODS
			frm[2] = 0x00;//NO AUTHENTICATION REQUIRED

			if (3 != net::tcpsend(_sock, frm, 3))
				return false;
			return true;
		}

		bool sendsock5request()
		{
			uint8_t frm[256], ul = (uint8_t)strlen(_s5domain);
			frm[0] = 0x05;//ver
			frm[1] = 0x01;//connect
			frm[2] = 0x00;//res
			frm[3] = 0x03;//domain
			frm[4] = ul;
			memcpy(&frm[5], _s5domain, ul);
			frm[5 + ul] = (uint8_t)((_s5port & 0xff00) >> 8);
			frm[5 + ul + 1] = (uint8_t)(_s5port & 0xff);

			if (ul + 7 != net::tcpsend(_sock, frm, 7 + ul))
				return false;
			return true;
		}

		void dosocks5handshake(int nmsec)
		{
			uint8_t sbuf[256];
			int nr = net::tcpread(_sock, sbuf, sizeof(sbuf), nmsec);
			if (nr < 0) {
				connectfailed();
				return;
			}
			else if (!nr) {
				if (::time(nullptr) - _timeconnect > _timeover_connect_sec) { //time over
					connectfailed();
					return;
				}
				return;
			}
			_rs5buf.append(sbuf, nr);
			if (_rs5buf.size() < 2)
				return;
			if (_rs5buf[0] != 5u || _rs5buf[1] != 0) { // handshake failed
				connectfailed();
				return;
			}
			_rs5buf.clear();
			if (!sendsock5request()) {
				connectfailed();
				return;
			}
			_status = st_s5request;
		}

		void dosocks5request(int nmsec)
		{
			uint8_t sbuf[256];
			int nr = net::tcpread(_sock, sbuf, sizeof(sbuf), nmsec);
			if (nr < 0) {
				connectfailed();
				return;
			}
			else if (!nr) {
				if (::time(nullptr) - _timeconnect > _timeover_connect_sec) { //time over
					connectfailed();
					return;
				}
				return;
			}
			_rs5buf.append(sbuf, nr);
			if (_rs5buf.size() < 7)
				return;
			if (_rs5buf[0] != 5u || _rs5buf[1] != 0) {
				connectfailed();
				return;
			}

			uint8_t ul = _rs5buf[4];
			if (_rs5buf.size() >= 7u + ul) {
				_rs5buf.erase(0, ul + 7u);
				_status = st_connected;
				onconnected();
				if (_rs5buf.size()) {
					onreadbytes(_rs5buf.data(), (int)_rs5buf.size());
					_rs5buf.clear();
					_rs5buf.shrink_to_fit();
				}
			}
		}

		void doread(int nmsec)
		{
			uint8_t sbuf[1024 * 32];
			int nr = net::tcpread(_sock, sbuf, sizeof(sbuf), nmsec);
			if (nr < 0) {
				close();
				return;
			}
			if (nr > 0)
				onreadbytes(sbuf, nr);
		}

		void doconnect(int nmsec)
		{
			TIMEVAL tv01 = { nmsec / 1000, 1000 * (nmsec % 1000) };
			fd_set fdw, fde;
			FD_ZERO(&fdw);
			FD_ZERO(&fde);
			FD_SET(_sock, &fdw);
			FD_SET(_sock, &fde);
			int ne;
#ifdef _WIN32
			ne = ::select(0, NULL, &fdw, &fde, &tv01);
#else
			ne = ::select(_sock + 1, NULL, &fdw, &fde, &tv01);
#endif
			if (ne < 0 || FD_ISSET(_sock, &fde)) {
				connectfailed();
				return;
			}
			if (!ne || !FD_ISSET(_sock, &fdw)) {
				if (::time(nullptr) - _timeconnect > _timeover_connect_sec) { //timeover
					connectfailed();
					return;
				}
				return; //continue wait
			}

#ifndef _WIN32
			int serr = 0;
			socklen_t serrlen = sizeof(serr);
			getsockopt(_sock, SOL_SOCKET, SO_ERROR, (void *)&serr, &serrlen);
			if (serr) {
				connectfailed();
				return;
			}
#endif
			//connect success
			if (_s5domain[0] && _uport) {
				if (!sendsock5handshake()) {
					connectfailed();
					return;
				}
				_status = st_s5handshake;
				return;
			}

			_status = st_connected;
			onconnected();
		}
	};
}