/*!
\file ec_system.h
\author	jiangyong
\email  kipway@outlook.com
\update 2022.5.26

eclib, easy C/C++ Library,based on C++11

windows
	VC2015 and later, windows vista/2008 and later
linux
	GCC/G++ 4.8.5 and later, kernel version 3.10 and later

eclib 3.0 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/

#pragma once

#ifdef _WIN32
#ifndef _CRT_SECURE_NO_WARNINGS
#	define _CRT_SECURE_NO_WARNINGS 1
#endif
#	pragma warning (disable : 4995)
#	pragma warning (disable : 4996)
#	pragma warning (disable : 4200)
#	ifndef _WIN32_WINNT
#		define _WIN32_WINNT 0x0600	//0x600 = vista/2008; 0x601=win7 ;  0x0501=windows xp ;  0x0502=windows 2003
#	endif
#	include <process.h>
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>

#	ifndef _NOTUSE_WINSOCKET
#		include <Winsock2.h>
#		pragma comment(lib,"Ws2_32.lib")
#	endif

#else
#	include <termios.h>
#	include <unistd.h>

#define _tzset tzset

#ifndef SOCKET
#	define SOCKET int
#endif

#ifndef INVALID_SOCKET
#	define INVALID_SOCKET    (-1)
#endif

#ifndef SOCKET_ERROR
#	define SOCKET_ERROR      (-1)
#endif

#ifndef closesocket
#	define closesocket(a) close(a)
#endif

#ifndef TIMEVAL
#	define TIMEVAL struct timeval
#endif

#endif

#ifndef NULL
#	ifdef __cplusplus
#		define NULL 0
#	else
#		define NULL ((void *)0)
#endif
#endif

#include <cstdlib>
#include <cstdio>
#include <cstdint>
