/*!
\file ec_service.h
\author	jiangyong
\email  kipway@outlook.com
\update 2020.8.29

service frame for windows and Linux

eclib 3.0 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway/eclib

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

简介：
支持windows/linux的后台服务服务框架。
定义_DEBUG 宏时编译为前台命令行版方便调试，否则编译为后台服务程序。

用法：
#include "ec_system.h"
#include "ec_service.h"

#ifndef _WIN32
#include <iconv.h>
#define _T(a) a
typedef  char TCHAR;
#endif

const TCHAR* g_service = _T("logsrv");
const TCHAR* g_spid = _T("/var/run/logsrv.pid");

const TCHAR* g_sver = _T("ver 1.0.0 alpha1,build 2020.8.26 ");
#ifdef _WIN32
const TCHAR* g_sbuild = _T("logsrv for windows server 2008");  //   service 描述
#else
const TCHAR* g_sbuild = _T("logsrv for centos 7");
#endif

const int   g_damonmsgkey = 20826;

class CRunCls //实现这样一个运行类
{
public:
	CRunCls()
	{		
	}
	~CRunCls()
	{
	};
	
public:
	bool start()
	{		
		// todo
		return true;
	}
	bool stop()
	{
		// todo
		return true;
	}
	void docmd(int argc, char** argv)
	{
	}
	void test(int ch)
	{
	}
	void runtime()
	{
		// todo
	}
};

EC_SERVICE_FRAME(CRunCls, g_service, g_spid, g_damonmsgkey, g_sbuild, g_sver)

*/

#pragma once

#include <stdio.h>
#include <stdarg.h>
#include <memory.h>
#include "ec_usews32.h"
#include "ec_string.h"

#ifdef _WIN32
#include "tchar.h"
#include <conio.h>
#include <Windows.h>

namespace ec {
	template <class _CLS>
	class CNtService
	{
	public:
		CNtService() {};
		virtual ~CNtService() {};
		void Init(LPCTSTR sName, LPCTSTR sDispName, LPCTSTR sDescription)
		{
			m_bShutDown = FALSE;
			_tcscpy(m_szServiceName, sName);
			_tcscpy(m_szServiceDispName, sDispName);
			_tcscpy(m_szServiceDes, sDescription);

			m_hServiceStatus = NULL;
			m_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
			m_status.dwCurrentState = SERVICE_STOPPED;
			m_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
			m_status.dwWin32ExitCode = 0;
			m_status.dwServiceSpecificExitCode = 0;
			m_status.dwCheckPoint = 0;
			m_status.dwWaitHint = 0;
		}
	public:
		static CNtService<_CLS>* _pobj;
		TCHAR m_szServiceName[256];
		TCHAR m_szServiceDispName[256];
		TCHAR m_szServiceDes[256];

		SERVICE_STATUS_HANDLE	m_hServiceStatus;
		SERVICE_STATUS			m_status;
		DWORD					m_dwThreadID;
		bool					m_bShutDown;

		void ServiceMain(DWORD dwArgc, LPTSTR* lpszArgv)
		{
			m_status.dwCurrentState = SERVICE_START_PENDING;
			m_hServiceStatus = RegisterServiceCtrlHandler(m_szServiceName, _Handler);
			if (m_hServiceStatus == NULL)
				return;
			SetServiceStatus(SERVICE_START_PENDING);

			m_status.dwWin32ExitCode = S_OK;
			m_status.dwCheckPoint = 0;
			m_status.dwWaitHint = 0;

			Run();
			SetServiceStatus(SERVICE_STOPPED);
		}

		void Handler(DWORD dwOpcode)
		{
			switch (dwOpcode) {
			case SERVICE_CONTROL_STOP:
				SetServiceStatus(SERVICE_STOP_PENDING);
				PostThreadMessage(m_dwThreadID, WM_QUIT, 0, 0);
				break;
			case SERVICE_CONTROL_PAUSE:
				break;
			case SERVICE_CONTROL_CONTINUE:
				break;
			case SERVICE_CONTROL_INTERROGATE:
				break;
			case SERVICE_CONTROL_SHUTDOWN:
				m_bShutDown = TRUE;
				SetServiceStatus(SERVICE_STOP_PENDING);
				PostThreadMessage(m_dwThreadID, WM_QUIT, 0, 0);
				break;
			default:
				break;
			}
		}

		void Start()
		{
			SERVICE_TABLE_ENTRY st[] =
			{
				{ m_szServiceName, _ServiceMain },
				{ NULL, NULL }
			};
			StartServiceCtrlDispatcher(st);	
		};

		BOOL IsInstalled()
		{
			BOOL bResult = FALSE;

			SC_HANDLE hSCM = ::OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);

			if (hSCM != NULL) {
				SC_HANDLE hService = ::OpenService(hSCM, m_szServiceName, SERVICE_QUERY_CONFIG);
				if (hService != NULL) {
					bResult = TRUE;
					::CloseServiceHandle(hService);
				}
				::CloseServiceHandle(hSCM);
			}
			return bResult;
		}

		BOOL Install()
		{
			if (IsInstalled())
				return TRUE;

			SC_HANDLE hSCM = ::OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
			if (hSCM == NULL) {
				MessageBox(NULL, _T("Couldn't open service manager"), m_szServiceName, MB_OK);
				return FALSE;
			}

			TCHAR szFilePath[_MAX_PATH];
			::GetModuleFileName(NULL, szFilePath, _MAX_PATH);

			TCHAR szBinfile[_MAX_PATH];
			_tcscpy(szBinfile, szFilePath);
			_tcscat(szBinfile, _T(" -service"));

			SC_HANDLE hService = ::CreateService(
				hSCM, m_szServiceName, m_szServiceDispName,
				GENERIC_ALL, SERVICE_WIN32_OWN_PROCESS,
				SERVICE_AUTO_START,
				SERVICE_ERROR_IGNORE,
				szBinfile, NULL, NULL, NULL, NULL, NULL);

			if (hService == NULL) {
				::CloseServiceHandle(hSCM);
				MessageBox(NULL, _T("Couldn't create service"), m_szServiceName, MB_OK);
				return FALSE;
			}

			{
				SC_ACTION  Actions;
				Actions.Type = SC_ACTION_RESTART;
				Actions.Delay = 60 * 1000; //1 minute

				SERVICE_FAILURE_ACTIONS act;
				memset(&act, 0, sizeof(act));
				act.dwResetPeriod = 0;
				act.lpRebootMsg = nullptr;
				act.lpCommand = nullptr;
				act.cActions = 1;
				act.lpsaActions = &Actions;

				if (!ChangeServiceConfig2(hService, SERVICE_CONFIG_FAILURE_ACTIONS, &act))
					MessageBox(NULL, _T("Configuration failure recovery failed!\nplease manually configure service failure recovery!\n"), m_szServiceName, MB_OK);
			}

			TCHAR sKey[_MAX_PATH];
			_tcscpy(sKey, _T("SYSTEM\\CurrentControlSet\\Services\\"));
			_tcscat(sKey, m_szServiceName);

			HKEY hkey;
			if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, sKey, 0, KEY_WRITE | KEY_READ, &hkey) == ERROR_SUCCESS) {
				RegSetValueEx(hkey, _T("Description"), NULL, REG_SZ, (LPBYTE)(m_szServiceDes), (lstrlen(m_szServiceDes) + 1) * sizeof(TCHAR));
				RegCloseKey(hkey);
			}

			::CloseServiceHandle(hService);
			::CloseServiceHandle(hSCM);

			MessageBox(NULL, _T("install service success!"), m_szServiceName, MB_OK);
			return TRUE;
		}
		BOOL Uninstall()
		{
			if (!IsInstalled())
				return TRUE;

			SC_HANDLE hSCM = ::OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);

			if (hSCM == NULL) {
				MessageBox(NULL, _T("Couldn't open service manager"), m_szServiceName, MB_OK);
				return FALSE;
			}

			SC_HANDLE hService = ::OpenService(hSCM, m_szServiceName, SERVICE_STOP | DELETE);

			if (hService == NULL) {
				::CloseServiceHandle(hSCM);
				MessageBox(NULL, _T("Couldn't open service"), m_szServiceName, MB_OK);
				return FALSE;
			}
			SERVICE_STATUS status;
			::ControlService(hService, SERVICE_CONTROL_STOP, &status);

			BOOL bDelete = ::DeleteService(hService);
			::CloseServiceHandle(hService);
			::CloseServiceHandle(hSCM);

			if (bDelete) {
				MessageBox(NULL, _T("delete service success!"), m_szServiceName, MB_OK);
				return TRUE;
			}

			MessageBox(NULL, _T("Service could not be deleted"), m_szServiceName, MB_OK);
			return FALSE;
		}
		void LogEvent(UINT ueventid, LPCTSTR pszFormat, ...)
		{
			TCHAR    chMsg[256];
			HANDLE  hEventSource;
			LPTSTR  lpszStrings[1];
			va_list pArg;

			va_start(pArg, pszFormat);
			_vstprintf(chMsg, pszFormat, pArg);
			va_end(pArg);

			lpszStrings[0] = chMsg;

			hEventSource = RegisterEventSource(NULL, m_szServiceName);
			if (hEventSource != NULL) {
				ReportEvent(hEventSource, EVENTLOG_INFORMATION_TYPE, 0, ueventid, NULL, 1, 0, (LPCTSTR*)&lpszStrings[0], NULL);
				DeregisterEventSource(hEventSource);
			}

		}
		void SetServiceStatus(DWORD dwState)
		{
			m_status.dwCurrentState = dwState;
			::SetServiceStatus(m_hServiceStatus, &m_status);
		}

		BOOL RegisterServer()
		{
			Uninstall();
			return Install();
		}
		inline BOOL UnregisterServer()
		{
			return Uninstall();
		}

	private:
		static void WINAPI _ServiceMain(DWORD dwArgc, LPTSTR* lpszArgv)
		{
			CNtService::_pobj->ServiceMain(dwArgc, lpszArgv);
		};
		static void WINAPI _Handler(DWORD dwOpcode)
		{
			CNtService::_pobj->Handler(dwOpcode);
		}
	protected:
		void Run()
		{
			_CLS* psvr = new _CLS();
			if (!psvr)
				return;
			if (!psvr->start()) {
				psvr->stop();
				delete psvr;
				exit(-1);
				return;
			}
			msgloop(psvr);
			psvr->stop();
			delete psvr;
		}
		void msgloop(_CLS* pcls)
		{
			m_dwThreadID = GetCurrentThreadId();
			SetServiceStatus(SERVICE_RUNNING);

			MSG msg;
			while (1) {
				if (!PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
					pcls->runtime();
					continue;
				}
				if (msg.message == WM_QUIT)
					break;
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			m_dwThreadID = 0;
		};
	};
}//ec

#endif

#ifdef _DEBUG
#ifndef _WIN32
#include <sys/select.h>
int _kbhit(void)
{
	struct timeval tv;
	fd_set read_fd;

	tv.tv_sec = 0;
	tv.tv_usec = 0;
	FD_ZERO(&read_fd);
	FD_SET(0, &read_fd);

	if (select(1, &read_fd, NULL, NULL, &tv) == -1)
		return 0;

	if (FD_ISSET(0, &read_fd))
		return 1;

	return 0;
}
#define getch getchar
#else
ec::cUseWS_32 usews32;
#endif

#define EC_SERVICE_FRAME(RUNCLS,SSERVICE,SPID,MSGKEY,SBUILD,SVER) int main(int argc, char* argv[])\
{\
	RUNCLS *srv = new RUNCLS;\
	if(argc != 1){\
		srv->docmd(argc, argv);\
		delete srv;\
		return 0;\
	}\
	if (!srv->start()){\
		printf("srv.Start failed!\n");\
		delete srv;\
		return 0;\
	}\
	printf("start success,press 'q' to exit \n");\
	while (1){\
		srv->runtime();\
		if (!_kbhit())\
			continue;\
		int ch = getch();\
		if (ch == 'q' || ch == 'Q')\
			break;\
		else\
			srv->test(ch);\
	}\
	srv->stop();\
	delete srv;\
	return 0;\
}\

#else // release

#ifdef _WIN32
#define EC_SERVICE_FRAME(RUNCLS,SSERVICE,SPID,MSGKEY,SBUILD,SVER)  ec::CNtService<RUNCLS> _server;\
ec::CNtService<RUNCLS>* ec::CNtService<RUNCLS>::_pobj = &_server;\
LPCTSTR FindOneOf(LPCTSTR p1, LPCTSTR p2){\
	while (p1 != NULL && *p1 != NULL){\
		LPCTSTR p = p2;\
		while (p != NULL && *p != NULL){\
			if (*p1 == *p)\
				return CharNext(p1);\
			p = CharNext(p);\
		}\
		p1 = CharNext(p1);\
	}\
	return NULL;\
}\
\
void ShowMsg()\
{\
	TCHAR smsg[1024]={0};\
	_tcscpy(smsg, _T("please install first \n"));\
	_tcscat(smsg, SSERVICE);\
	_tcscat(smsg, _T(" -install\n"));\
	_tcscat(smsg, SBUILD);\
	_tcscat(smsg, " \n");\
	_tcscat(smsg, SVER);\
	MessageBox(0, smsg, SSERVICE, MB_OK);\
}\
\
int APIENTRY _tWinMain(HINSTANCE hInstance,\
	HINSTANCE hPrevInstance,\
	LPTSTR     lpCmdLine,\
	int       nCmdShow)\
{\
	ec::cUseWS_32 usews32;\
	_server.Init(SSERVICE, SSERVICE, SBUILD);\
	TCHAR szTokens[] = _T("-/");\
	LPCTSTR lpszToken = FindOneOf(lpCmdLine, szTokens);\
	if (lpszToken == NULL) {\
		ShowMsg();\
		return 0;\
	}\
	else {\
		if (!lstrcmpi(lpszToken, _T("uninstall")))\
			_server.UnregisterServer();\
		else if (!lstrcmpi(lpszToken, _T("install"))) {\
			if (!_server.RegisterServer())\
				MessageBox(NULL, _T("install failed,Please make sure you have admin rights!"), SSERVICE, MB_OK);\
		}\
		else if (!lstrcmpi(lpszToken, _T("service"))) {\
			_server.Start();\
			return 0;\
		}\
		else\
			ShowMsg();\
	}\
	return 0;\
}\

#else // linux

#include "ec_daemon.h"
#define EC_SERVICE_FRAME(RUNCLS,SSERVICE,SPID,MSGKEY,SBUILD,SVER)\
ec::daemon<RUNCLS>_server;\
template<>\
ec::daemon<RUNCLS>* ec::daemon<RUNCLS>::_pdaemon = &_server;\
int main(int argc, char** argv)\
{\
	printf("\n");\
	_server.Init(SPID, SSERVICE, SVER, MSGKEY);\
	if (argc == 2){\
		if (!strcasecmp(argv[1], "-start"))\
			_server.start();\
		else if (!strcasecmp(argv[1], "-stop"))\
			_server.stop();\
		else if (!strcasecmp(argv[1], "-status"))\
			_server.status();\
		else if (!strcasecmp(argv[1], "-ver") || !strcasecmp(argv[1], "-version"))\
			printf("%s %s %s\n", SSERVICE, SBUILD, SVER); \
		else\
			_server.docmd(argc,argv);\
	}\
	else if(argc == 1)\
		_server.usage();\
	else\
		_server.docmd(argc, argv);\
	return 0;\
}\

#endif  //_WIN32
#endif  //_DEBUG
