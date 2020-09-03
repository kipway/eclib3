/*!
\file ec_system.h
\author	jiangyong
\email  kipway@outlook.com
\update 2020.9.1

eclib, easy C/C++ Library

eclib 3.0 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

简介:
eclib从2015年开始的tomlib演变而来, 到现在的eclib3.0, easy C/C++ Library, 由一些列的h文件组成, 提供
C++11标准库以外的类和函数，主要有：

日期时间处理
字符串扩展处理，GBK-UTF8编码互转
CRC,MD5,SHA1散列值计算
base64编码/解码
json解析
google protocol buffer编码/解码
网络IO, TCP/TLS1.2, http/websocket, https/secret-websocket
config配置文件读写，csv文件扫描
log日志
windows/linux后台服务框架
扩展的array,vector,hashmap集合类
一些文件IO处理

eclib3.0使用时只需include就可使用，为了将全局函数的实现写在h文件里，使用了一些不必要的模板，其他少部分无
法使用模板实现的全局函数使用inline关键字。

eclib3目前在下面三个平台有应用：
x86/x64 CPU windows server 2008及以后
x86/x64 CPU Linux 内核3.10及以后
ARM CPU busybox嵌入式Linux, 内核3.10及以后

需要C++11支持，编译器Windows平台推荐VC2015及以后，VC2017最佳。Linux平台推荐G++ 4.8.5及以后。
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
