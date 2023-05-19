/*!
\file ec_diskio.h
\author	jiangyong
\email  kipway@outlook.com
\update 2023.5.19

io
	tools for disk IO，use utf-8 parameters

cdir
	class for file and directory search.

eclib 3.0 Copyright (c) 2017-2023, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/

#pragma once

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include  <io.h>
#include <windows.h>
#include <sys/stat.h>
#else
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <fcntl.h>
#endif
#include <string>
#include "ec_string.h"

namespace ec
{
	struct t_stat {
		long long size;
		time_t mtime;
		time_t ctime;
		bool   bdir;
	};
	namespace io
	{
		inline FILE *fopen(const char* utf8file, const char* utf8mode)
		{
#ifdef _WIN32
			UINT codepage = ec::strisutf8(utf8file) ? CP_UTF8 : CP_ACP;
			wchar_t sfile[512], smode[32];
			sfile[0] = 0;
			smode[0] = 0;
			if (!MultiByteToWideChar(codepage, 0, utf8file, -1, sfile, sizeof(sfile) / sizeof(wchar_t))
				|| !MultiByteToWideChar(codepage, 0, utf8mode, -1, smode, sizeof(smode) / sizeof(wchar_t))
				)
				return nullptr;
			return _wfopen(sfile, smode);
#else
			return ::fopen(utf8file, utf8mode);
#endif
		}

		inline int remove(const char* utf8filename)
		{
#ifdef _WIN32
			wchar_t sfile[512];
			sfile[0] = 0;
			if (!MultiByteToWideChar(ec::strisutf8(utf8filename) ? CP_UTF8 : CP_ACP, 0, utf8filename, -1, sfile, sizeof(sfile) / sizeof(wchar_t)))
				return -1;
			return ::_wremove(sfile);
#else
			return ::remove(utf8filename);
#endif
		}

#ifdef _WIN32
		inline bool exist(const char* utf8filename)
		{
			wchar_t sfile[512];
			sfile[0] = 0;
			if (!MultiByteToWideChar(ec::strisutf8(utf8filename) ? CP_UTF8 : CP_ACP, 0, utf8filename, -1, sfile, sizeof(sfile) / sizeof(wchar_t))
				|| _waccess(sfile, 0))
				return false;
			return true;
		}

		inline bool createdir(const char* utf8path)
		{
			UINT codepage = ec::strisutf8(utf8path) ? CP_UTF8 : CP_ACP;
			int i = 0;
			char szt[512];
			wchar_t sdir[512];
			sdir[0] = 0;

			szt[0] = 0;
			while (*utf8path) {
				szt[i] = (*utf8path == '\\') ? '/' : *utf8path;
				if (szt[i] == '/') {
					szt[i] = '\0';
					if (!exist(szt)) {
						if (!MultiByteToWideChar(codepage, 0, szt, -1, sdir, sizeof(sdir) / sizeof(wchar_t))
							|| !CreateDirectoryW(sdir, NULL))
							return false;
					}
					szt[i] = '/';
				}
				++utf8path;
				++i;
			}
			if (i && szt[i] != '/') {
				szt[i] = '\0';
				if (!exist(szt)) {
					if (!MultiByteToWideChar(codepage, 0, szt, -1, sdir, sizeof(sdir) / sizeof(wchar_t))
						|| !CreateDirectoryW(sdir, NULL))
						return false;
				}
			}
			return true;
		}

		inline long long getdiskspace(const char* utf8path) // lpszDisk format is "c:\"
		{
			wchar_t wpath[512];
			if (!MultiByteToWideChar(ec::strisutf8(utf8path) ? CP_UTF8 : CP_ACP, 0, utf8path, -1, wpath, sizeof(wpath) / sizeof(wchar_t)))
				return 0;
			ULARGE_INTEGER ullFreeBytesAvailable, ullTotalNumberOfBytes, ullTotalNumberOfFreeBytes;
			ullFreeBytesAvailable.QuadPart = 0;
			ullTotalNumberOfBytes.QuadPart = 0;
			ullTotalNumberOfFreeBytes.QuadPart = 0;
			BOOL bret = GetDiskFreeSpaceExW(
				wpath,      // directory name
				&ullFreeBytesAvailable,    // bytes available to caller
				&ullTotalNumberOfBytes,    // bytes on disk
				&ullTotalNumberOfFreeBytes // free bytes on disk
			);
			if (bret) {
				ULONGLONG lspace = ullFreeBytesAvailable.QuadPart >> 20; //MB
				return (long long)lspace;
			}
			return 0;
		}

		inline bool filestat(const char* utf8file, t_stat *pout)
		{
			wchar_t wfile[512];
			if (!MultiByteToWideChar(ec::strisutf8(utf8file) ? CP_UTF8 : CP_ACP, 0, utf8file, -1, wfile, sizeof(wfile) / sizeof(wchar_t)))
				return false;
			struct __stat64 statbuf;
			if (!_wstat64(wfile, &statbuf)) {
				pout->size = statbuf.st_size;
				pout->mtime = statbuf.st_mtime;
				pout->ctime = statbuf.st_ctime;
				pout->bdir = (statbuf.st_mode & _S_IFDIR) != 0;
				return true;
			}
			return false;
		}

		template<class _Out = std::string>
		bool getappname(_Out &utf8name)
		{
			wchar_t sFilename[512];
			wchar_t sDrive[_MAX_DRIVE];
			wchar_t sDir[_MAX_DIR];
			wchar_t sFname[_MAX_FNAME];
			wchar_t sExt[_MAX_EXT];

			GetModuleFileNameW(NULL, sFilename, sizeof(sFilename) / sizeof(wchar_t));
			_wsplitpath(sFilename, sDrive, sDir, sFname, sExt);

			char sutf8[_MAX_FNAME * 3];
			sutf8[0] = 0;
			if (!WideCharToMultiByte(CP_UTF8, 0, sFname, -1, sutf8, (int)sizeof(sutf8), NULL, NULL))
				return false;
			utf8name = sutf8;
			return true;
		}

		template<class _Out = std::string>
		bool getexepath(_Out& utf8path) // last char is '/'
		{
			wchar_t sFilename[512];
			wchar_t sDrive[_MAX_DRIVE];
			wchar_t sDir[_MAX_DIR];
			wchar_t sFname[_MAX_FNAME];
			wchar_t sExt[_MAX_EXT];
			utf8path.clear();
			GetModuleFileNameW(NULL, sFilename, sizeof(sFilename) / sizeof(wchar_t));
			_wsplitpath(sFilename, sDrive, sDir, sFname, sExt);

			char sdrv[8] = { 0 };
			char sutf8[_MAX_DIR * 3];
			sutf8[0] = 0;
			if (!WideCharToMultiByte(CP_UTF8, 0, sDrive, -1, sdrv, (int)sizeof(sdrv), NULL, NULL)
				||!WideCharToMultiByte(CP_UTF8, 0, sDir, -1, sutf8, (int)sizeof(sutf8), NULL, NULL))
				return false;
			utf8path = sdrv;
			utf8path += sutf8;
			for (auto &i : utf8path) {
				if (i == '\\')
					i = '/';
			}

			if (utf8path.back() != '/')
				utf8path.push_back('/');
			return true;
		}

		inline long long filesize(const char* utf8file)
		{
			wchar_t wsfile[512];
			if (!MultiByteToWideChar(ec::strisutf8(utf8file) ? CP_UTF8 : CP_ACP, 0, utf8file, -1, wsfile, sizeof(wsfile) / sizeof(wchar_t)))
				return -1;
			struct __stat64 statbuf;
			if (!_wstat64(wsfile, &statbuf))
				return statbuf.st_size;
			return -1;
		}

		template <class _Out = std::string>
		bool lckread(const char* utf8file, _Out *pout, long long offset = 0, long long lsize = 0)
		{
			pout->clear();
			long long size = filesize(utf8file);
			if (size <= 0) {
				return false;
			}
			if (lsize)
				pout->reserve((size_t)lsize);
			else if(size > offset)
				pout->reserve(static_cast<size_t>(size - offset));

			wchar_t sfile[512];
			if (!MultiByteToWideChar(ec::strisutf8(utf8file) ? CP_UTF8 : CP_ACP, 0, utf8file, -1, sfile, sizeof(sfile) / sizeof(wchar_t)))
				return false;

			HANDLE hFile = CreateFileW(sfile, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING
				, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL); // 共享只读打开
			if (hFile == INVALID_HANDLE_VALUE)
				return false;

			uint64_t uoff = (uint64_t)offset, usize = (uint64_t)lsize;
			OVERLAPPED	 op;
			memset(&op, 0, sizeof(op));
			op.Offset = (DWORD)(uoff & 0xFFFFFFFF);
			op.OffsetHigh = (DWORD)((uoff >> 32) & 0xFFFFFFFF);

			DWORD dwll = (DWORD)-1, dwlh = (DWORD)-1;
			if (usize) {
				dwll = (DWORD)(usize & 0xFFFFFFFF);
				dwlh = (DWORD)((usize >> 32) & 0xFFFFFFFF);
			}
			if (!LockFileEx(hFile, 0, 0, dwll, dwlh, &op)) {
				CloseHandle(hFile);
				return false;
			}

			char tmp[1024 * 32];
			DWORD dwr = 0;
			if (offset) {
				LARGE_INTEGER liOff;
				liOff.QuadPart = offset;
				liOff.LowPart = ::SetFilePointer(hFile, liOff.LowPart, &liOff.HighPart, 0);
				if (liOff.LowPart == 0xFFFFFFFF)
					if (::GetLastError() != NO_ERROR)
						return false;
			}
			do {
				if (lsize && pout->size() + sizeof(tmp) > (size_t)lsize)
					dwr = (DWORD)((size_t)lsize - pout->size());
				else
					dwr = (DWORD)sizeof(tmp);
				if (!ReadFile(hFile, tmp, dwr, &dwr, nullptr))
					break;
				pout->append(tmp, dwr);
			} while (dwr == sizeof(tmp) && (!lsize || (pout->size() < (size_t)lsize)));

			UnlockFileEx(hFile, 0, dwll, dwlh, &op);
			CloseHandle(hFile);
			return pout->size() > 0;
		}
#else
		inline bool exist(const char* sfile)
		{
			if (access(sfile, F_OK))
				return false;
			return true;
		}

		inline bool createdir(const char* spath)
		{
			if (!spath || !*spath)
				return false;
			char cl = 0;
			char szt[512] = { '\0', }, *pct;

			pct = (char*)szt;
			while (*spath) {
				if (*spath == '\\' || *spath == '/') {
					if (szt[0] && access(szt, F_OK))
						if (mkdir(szt, S_IROTH | S_IXOTH | S_IRWXU | S_IRWXG))
							return false;
					*pct = '/';
					pct++;
					*pct = '\0';
					cl = '/';
				}
				else {
					*pct = *spath;
					cl = *pct;
					pct++;
					*pct = '\0';
				}
				spath++;
			}
			if (cl != '/' && access(szt, F_OK))
				if (mkdir(szt, S_IROTH | S_IXOTH | S_IRWXU | S_IRWXG))
					return false;
			return true;
		}

		inline long long getdiskspace(const char* sroot) //
		{
			struct statfs diskInfo;

			if (-1 == statfs(sroot, &diskInfo))
				return 0;

			long long blocksize = diskInfo.f_bsize;
			unsigned long long  freeDisk = diskInfo.f_bfree * blocksize;
			freeDisk >>= 20; //MB
			return (long long)freeDisk;
		}

		template<class _String>
		bool getexepath(_String &spath) // last char is '/'
		{
			char sopath[1024] = { 0 };
			int n = readlink("/proc/self/exe", sopath, sizeof(sopath) - 1);
			while (n > 0 && sopath[n - 1] != '/') {
				n--;
				sopath[n] = 0;
			}
			if (n > 0 && sopath[n - 1] != '/')
				strcat(sopath, "/");
			spath = sopath;
			return true;
		}

		template<class _String>
		bool getappname(_String &appname) //
		{
			char sopath[1024] = { 0, };
			int n = readlink("/proc/self/exe", sopath, sizeof(sopath) - 1);
			while (n > 0 && sopath[n - 1] != '/')
				n--;
			appname = &sopath[n];
			return true;
		}

		inline bool lock(int nfd, long long offset, long long lsize, bool bwrite)
		{
			struct flock    lock;

			if (bwrite)
				lock.l_type = F_WRLCK;  /* F_RDLCK, F_WRLCK, F_UNLCK */
			else
				lock.l_type = F_RDLCK;

			lock.l_start = offset;    /* byte offset, relative to l_whence */
			lock.l_whence = SEEK_SET; /* SEEK_SET, SEEK_CUR, SEEK_END */
			lock.l_len = lsize;       /* #bytes (0 means to EOF) */
			lock.l_pid = getpid();
			return !(fcntl(nfd, F_SETLKW, &lock) < 0);
		}

		inline bool unlock(int nfd, long long offset, long long lsize)
		{
			struct flock    lock;

			lock.l_type = F_UNLCK;  /* F_RDLCK, F_WRLCK, F_UNLCK */
			lock.l_start = offset;    /* byte offset, relative to l_whence */
			lock.l_whence = SEEK_SET; /* SEEK_SET, SEEK_CUR, SEEK_END */
			lock.l_len = lsize;       /* #bytes (0 means to EOF) */
			lock.l_pid = getpid();

			return !(fcntl(nfd, F_SETLKW, &lock) < 0);
		}

		inline long long filesize(const char* utf8file)
		{
			struct stat statbuf;
			if (!::stat(utf8file, &statbuf))
				return (long long)statbuf.st_size;
			return -1;
		}

		inline bool filestat(const char* sfile, t_stat *pout)
		{
			struct stat statbuf;
			if (!::stat(sfile, &statbuf)) {
				pout->size = statbuf.st_size;
				pout->mtime = statbuf.st_mtim.tv_sec;
				pout->ctime = statbuf.st_ctim.tv_sec;
				pout->bdir = (statbuf.st_mode & S_IFDIR) != 0;
				return true;
			}
			return false;
		}

		/*!
		\brief lock read whole file for linux

		pout ec::bytes or ec::string

		S_IRWXU  00700 user (file owner) has read, write and execute permission
		S_IRUSR  00400 user has read permission
		S_IWUSR  00200 user has write permission
		S_IXUSR  00100 user has execute permission
		S_IRWXG  00070 group has read, write and execute permission
		S_IRGRP  00040 group has read permission
		S_IWGRP  00020 group has write permission
		S_IXGRP  00010 group has execute permission
		S_IRWXO  00007 others have read, write and execute permission
		S_IROTH  00004 others have read permission
		S_IWOTH  00002 others have write permission
		S_IXOTH  00001 others have execute permission
		*/

		template <class _Out = std::string>
		bool lckread(const char* utf8file, _Out *pout, long long offset = 0, long long lsize = 0)
		{
			int nfd = ::open(utf8file, O_RDONLY, S_IROTH | S_IRUSR | S_IRGRP);
			if (nfd == -1)
				return false;
			long long  size = filesize(utf8file);
			if (size <= 0) {
				::close(nfd);
				pout->clear();
				return false;
			}
			pout->clear();
			if (lsize)
				pout->reserve((size_t)lsize);
			else if (size > offset)
				pout->reserve(static_cast<size_t>(size - offset));

			char tmp[1024 * 32];
			ssize_t nr;
			if (!lock(nfd, offset, lsize, false)) {
				::close(nfd);
				return false;
			}
			if (offset)
				::lseek64(nfd, offset, 0);
			size_t szr;
			do {
				if (lsize && pout->size() + sizeof(tmp) > (size_t)lsize)
					szr = ((size_t)lsize - pout->size());
				else
					szr = sizeof(tmp);
				nr = ::read(nfd, tmp, szr);
				if (nr > 0)
					pout->append(tmp, nr);
			} while (nr == (int)sizeof(tmp) &&(!lsize || (pout->size() < (size_t)lsize)));
			unlock(nfd, offset, lsize);
			::close(nfd);
			return pout->size() > 0;
		}
#endif
	}// namespace io

	class cdir
	{
	public:
		cdir(const char* utf8path)//spath with'/'
		{
#ifdef _WIN32
			size_t zn = -1;
			char szFilter[512];
			hFind = INVALID_HANDLE_VALUE;
			memset(&FindFileData, 0, sizeof(FindFileData));
			zn = snprintf(szFilter, sizeof(szFilter), "%s*.*", utf8path);
			if(zn >= sizeof(szFilter))
				return;
			wchar_t sfile[512];
			if (MultiByteToWideChar(ec::strisutf8(szFilter) ? CP_UTF8 : CP_ACP, 0, szFilter, -1, sfile, sizeof(sfile) / sizeof(wchar_t)))
				hFind = FindFirstFileW(sfile, &FindFileData);
#else
			dir = opendir(utf8path);
#endif
		}
		~cdir()
		{
#ifdef _WIN32
			if (hFind != INVALID_HANDLE_VALUE) {
				FindClose(hFind);
				hFind = INVALID_HANDLE_VALUE;
			}
#else
			if (dir) {
				closedir(dir);
				dir = nullptr;
			}
#endif
		}
	protected:
#ifdef _WIN32
		WIN32_FIND_DATAW FindFileData;
		HANDLE hFind;// = INVALID_HANDLE_VALUE;
		char _utf8tmp[512];
#else
		DIR * dir;
#endif
	public:
		template<class _Str>
		bool next(_Str& utf8out, int* pisdir = nullptr)
		{
#ifdef _WIN32
			if (hFind != INVALID_HANDLE_VALUE) {
				if (!WideCharToMultiByte(CP_UTF8, 0, FindFileData.cFileName, -1, _utf8tmp, (int)sizeof(_utf8tmp), NULL, NULL))
					_utf8tmp[0] = 0;
				utf8out = _utf8tmp;
				if (pisdir)
					*pisdir = (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
				if (!FindNextFileW(hFind, &FindFileData)) {
					FindClose(hFind);
					hFind = INVALID_HANDLE_VALUE;
				}
				return true;
			}
			return false;
#else
			if (!dir)
				return false;
			struct dirent *d = readdir(dir);
			if (d) {
				utf8out = (const char*)d->d_name;
				if (pisdir)
					*pisdir = (d->d_type == DT_DIR) ? 1 : 0;
				return true;
			}
			closedir(dir);
			dir = nullptr;
			return false;
#endif
		}
	};
}; // ec
