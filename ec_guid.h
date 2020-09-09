/*!
\file c_guid.h

\author	jiangyong
\email  kipway@outlook.com
\update 2020.9.6

cGuid
	UUID generator

eclib 3.0 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/
#ifndef C_GUID_H
#define C_GUID_H

#include <time.h>
#include "ec_netmac.h"
#include "ec_md5.h"
#ifdef _WIN32
#include <process.h>
#endif
namespace ec
{
	struct t_guid
	{
		unsigned int   v1;
		unsigned short v2;
		unsigned short v3;
		unsigned char  v4[8];
	};

	class cGuid
	{
	protected:
#ifdef _WIN32
		struct t_guidinfo
		{
			FILETIME	    ts;     // 时标,8字节
			unsigned int    pid;    // 进程pid
			unsigned int    seqno;  // 进程内递增序列号
			unsigned char   mac[8]; // 网卡mac地址
		} _uinfo;
#else
		struct t_guidinfo
		{
			timespec        ts;     // 时标,16字节
			unsigned int    pid;    // 进程pid
			unsigned int    seqno;   // 进程内递增序列号
			unsigned char   mac[8]; // 网卡mac地址
		} _uinfo;
#endif
	public:
		cGuid() {
			memset(&_uinfo, 0, sizeof(_uinfo));
			if (!ec::getnetmac(_uinfo.mac, 1))
			{
				size_t i;
				for (i = 0; i < sizeof(_uinfo.mac); i++)
					_uinfo.mac[i] = (unsigned char)(0xC1 + i);
			}
			_uinfo.pid = getpid();
			_uinfo.seqno = 1;
		}
		void uuid(t_guid *pguid)
		{
#ifdef _WIN32
			GetSystemTimeAsFileTime(&_uinfo.ts);
#else
			clock_gettime(CLOCK_REALTIME, &_uinfo.ts);
#endif
			_uinfo.seqno++;
			ec::encode_md5(&_uinfo, sizeof(_uinfo), (unsigned char*)pguid);
		}

		void uuid2(unsigned char guid[16], unsigned int seqno)
		{
#ifdef _WIN32
			GetSystemTimeAsFileTime(&_uinfo.ts);
#else
			clock_gettime(CLOCK_REALTIME, &_uinfo.ts);
#endif
			_uinfo.seqno = seqno;
			ec::encode_md5(&_uinfo, sizeof(_uinfo), guid);
		}
		static void guidstr(ec::t_guid * p, char *sout, size_t outlen) // outlen > 36
		{
			snprintf(sout, outlen, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", p->v1, p->v2, p->v3, p->v4[0], p->v4[1],
				p->v4[2], p->v4[3], p->v4[4], p->v4[5], p->v4[6], p->v4[7]);
		}
	};
}

#endif
