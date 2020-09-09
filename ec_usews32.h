/*
\file ec_usews32.h
\author	jiangyong
\email  kipway@outlook.com
\update 2020.9.6

cUseWS_32
	wrapper class for windows socket 2.2

eclib 3.0 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway/eclib

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/

#pragma once
namespace ec{
	#ifdef _WIN32
	#include <windows.h>
	#include <winsock2.h>
	class cUseWS_32
	{
	public:
		cUseWS_32(){
			m_bInit = false;
			unsigned short wVersionRequested;
			WSADATA wsaData;
			int err;

			wVersionRequested = MAKEWORD( 2,2 );

			err = WSAStartup( wVersionRequested, &wsaData );
			if ( err != 0 )
				return;

			if ( LOBYTE( wsaData.wVersion ) != 2 ||
				HIBYTE( wsaData.wVersion ) != 2 )
				WSACleanup( );
			else
				m_bInit = true;
		};
		~cUseWS_32(){
			if(m_bInit)
				WSACleanup( );
		};
	protected:
		bool m_bInit;

	public:
		static bool Init()
		{
			unsigned short wVersionRequested;
			WSADATA wsaData;
			int err;

			wVersionRequested = MAKEWORD( 2,2 );

			err = WSAStartup( wVersionRequested, &wsaData );
			if ( err != 0 )
				return false;

			if ( LOBYTE( wsaData.wVersion ) != 2 ||
				HIBYTE( wsaData.wVersion ) != 2 )
			{
				WSACleanup( );
				return false;
			}
			return true;
		}
		static bool Exit()
		{
			WSACleanup( );
			return true;
		}
	};
	#endif
};

