/*!
\file ec_wstips.h
\author	jiangyong
\email  kipway@outlook.com
\update 2021.8.24

functions used by websocket

send frame size : 62K
read frame size : 4M
read package size: 32M
compress extended: permessage_deflate(google chrome,firefox) and x_webkit_deflate_frame(Safari)

eclib 3.0 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/
#pragma once
#include "zlib/zlib.h"

#ifndef HTTP_MAX_RANG_SIZE
#define HTTP_MAX_RANG_SIZE (1024 * 1024 * 8u)
#endif

#define EC_SIZE_WS_FRAME (1024 * 62) // out put WS frame size

#ifndef MAXSIZE_WS_READ_FRAME
#	define MAXSIZE_WS_READ_FRAME (4 * 1024 * 1024) // read max ws frame size
#endif

#ifndef MAXSIZE_WS_READ_PKG
#	define MAXSIZE_WS_READ_PKG   (32 * 1024 * 1024) // read max ws package size
#endif

#define HTTPENCODE_NONE    0
#define HTTPENCODE_DEFLATE 1

#define PROTOCOL_HTTP   0
#define PROTOCOL_WS     1

#define WS_FINAL	  0x80
#define WS_OP_CONTINUE  0
#define WS_OP_TXT		1
#define WS_OP_BIN		2
#define WS_OP_CLOSE	    8
#define WS_OP_PING		9
#define WS_OP_PONG		10

#define ws_permessage_deflate		1  // for google chrome ,firefox
#define ws_x_webkit_deflate_frame   2  // for ios safari

#define SIZE_WSZLIBTEMP 32768
namespace ec
{
	template <class _Out>
	int ws_encode_zlib(const void *pSrc, size_t size_src, _Out* pout)//pout first two byte x78 and x9c,the end  0x00 x00 xff xff, no  adler32
	{
		z_stream stream;
		int err;
		char outbuf[SIZE_WSZLIBTEMP];

		stream.next_in = (z_const Bytef *)pSrc;
		stream.avail_in = (uInt)size_src;

		stream.zalloc = (alloc_func)0;
		stream.zfree = (free_func)0;
		stream.opaque = (voidpf)0;

		err = deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY);
		if (err != Z_OK)
			return err;
		uLong uout = 0;
		stream.avail_out = 0;
		while (!stream.avail_out) {
			stream.next_out = (unsigned char*)outbuf;
			stream.avail_out = (unsigned int)sizeof(outbuf);
			err = deflate(&stream, Z_SYNC_FLUSH);
			if (err != Z_OK)
				break;
			try {
				pout->append(outbuf, stream.total_out - uout);
			}
			catch(...){
				err = Z_MEM_ERROR;
				break;
			}
			uout += stream.total_out - uout;
		}
		deflateEnd(&stream);
		return err;
	}

	template <class _Out>
	int ws_decode_zlib(const void *pSrc, size_t size_src, _Out* pout)//pSrc begin with 0x78 x9c, has no end 0x00 x00 xff xff
	{
		z_stream stream;
		int err;
		unsigned char outbuf[SIZE_WSZLIBTEMP];

		stream.next_in = (z_const Bytef *)pSrc;
		stream.avail_in = (uInt)size_src;

		stream.zalloc = (alloc_func)0;
		stream.zfree = (free_func)0;
		stream.opaque = (voidpf)0;

		err = inflateInit(&stream);
		if (err != Z_OK)
			return err;
		uLong uout = 0;
		while (stream.avail_in > 0) {
			stream.next_out = (unsigned char*)outbuf;
			stream.avail_out = (unsigned int)sizeof(outbuf);
			err = inflate(&stream, Z_SYNC_FLUSH);
			if (err != Z_OK)
				break;
			try {
				pout->append(outbuf, stream.total_out - uout);
			}
			catch (...) {
				err = Z_MEM_ERROR;
				break;
			}
			uout += stream.total_out - uout;
		}
		inflateEnd(&stream);
		return err;
	}

	template <class _Out>
	bool ws_make_permsg(const void* pdata, size_t sizes, unsigned char wsopt, _Out* pout, int ncompress, uint32_t umask = 0) //multi-frame,permessage_deflate
	{
		unsigned char uc;
		const uint8_t* pds = (const uint8_t*)pdata;
		size_t slen = sizes;
		bytes tmp(pout->get_allocator());
		tmp.reserve(2048 + sizes - sizes % 1024);
		if (ncompress && sizes >= 128) {
			if (Z_OK != ws_encode_zlib(pdata, sizes, &tmp) || tmp.size() < 6)
				return false;
			pds = tmp.data() + 2;
			slen = tmp.size() - 6;
		}
		size_t ss = 0, us;
		pout->clear();
		do {
			uc = 0;
			if (0 == ss) { //first frame
				uc = 0x0F & wsopt;
				if (ncompress && sizes >= 128)
					uc |= 0x40;
			}
			us = EC_SIZE_WS_FRAME;
			if (ss + EC_SIZE_WS_FRAME >= slen) { // end frame
				uc |= 0x80;
				us = slen - ss;
			}
			pout->push_back(uc);
			if (us < 126) {
				uc = (unsigned char)us;
				if (umask)
					uc |= 0x80;
				pout->push_back(uc);
			}
			else if (uc < 65536) {
				uc = 126;
				if (umask)
					uc |= 0x80;
				pout->push_back(uc);
				pout->postoend();
				(*pout) < (uint16_t)(us);
			}
			else { // < 4G
				uc = 127;
				if (umask)
					uc |= 0x80;
				pout->push_back((uint8_t)uc);
				pout->postoend();
				(*pout) < (uint64_t)(us);
			}
			if (umask) {
				pout->postoend();
				(*pout) << umask;
			}
			pout->append(pds + ss, us);
			if (umask)
				ec::xor_le(pout->data() + pout->getpos(), (int)us, umask);
			ss += us;
		}while (ss < slen);
		return true;
	}

	template <class _Out>
	bool ws_make_perfrm(const void* pdata, size_t sizes, unsigned char wsopt, _Out* pout, uint32_t umask = 0)//multi-frame,deflate-frame, for ios safari
	{
		const uint8_t* pds = (const uint8_t*)pdata;
		uint8_t* pf;
		size_t slen = sizes;
		bytes tmp(pout->get_allocator());
		tmp.reserve(EC_SIZE_WS_FRAME);
		unsigned char uc;
		size_t ss = 0, us, fl;
		pout->clear();
		do{
			uc = 0;
			us = ((sizes - ss) >= EC_SIZE_WS_FRAME) ? EC_SIZE_WS_FRAME : sizes - ss;

			if (0 == ss)//first frame
				uc = 0x0F & wsopt;
			if (us > 256)
				uc |= 0x40;
			if (ss + EC_SIZE_WS_FRAME >= slen) { //end frame
				uc |= 0x80;
				us = slen - ss;
			}
			pout->push_back((uint8_t)uc);
			if (uc & 0x40) {
				tmp.clear();
				if (Z_OK != ws_encode_zlib(pds + ss, us, &tmp) || tmp.size() < 6)
					return false;
				pf = tmp.data() + 2;
				fl = tmp.size() - 6;
			}
			else {
				pf = (uint8_t*)pds + ss;
				fl = us;
			}

			if (fl < 126) {
				uc = (unsigned char)fl;
				if (umask)
					uc |= 0x80;
				pout->push_back(uc);
			}
			else if (uc < 65536) {
				uc = 126;
				if (umask)
					uc |= 0x80;
				pout->push_back(uc);
				pout->postoend();
				(*pout) < (uint16_t)fl;
			}
			else { // < 4G
				uc = 127;
				if (umask)
					uc |= 0x80;
				pout->push_back(uc);
				pout->postoend();
				(*pout) < (uint64_t)fl;
			}
			if (umask) {
				pout->postoend();
				(*pout) << umask;
			}
			pout->append(pf, fl);
			if (umask)
				ec::xor_le(pout->data() + pout->getpos(), (int)fl, umask);
			ss += us;
		}while (ss < slen);
		return true;
	}
}// ec

