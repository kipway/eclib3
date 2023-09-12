/*!
\file ec_file.h
\author	kipway@outlook.com
\update 
2023.9.12 Fix OF_APPEND_DATA for windows
2023.5.13 use self memory allocator

eclib file class

eclib3 Copyright (c) 2017-2023, kipway
source repository : https://github.com/kipway/eclib

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#pragma once

#ifdef _WIN32
#pragma warning(disable:4996)
#include <Windows.h>
#else
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <sys/stat.h>
#include <unistd.h>

#include<sys/types.h>
#include<fcntl.h>
#include<sys/statfs.h>
#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE (-1)
#endif
#endif
#include <stdint.h>

namespace ec
{
	class File
	{
	public:
		File() {
			m_hFile = INVALID_HANDLE_VALUE;
			_sharemode = 0;
		};
		virtual ~File()
		{
			Close();
		}
		enum OFlags
		{
			OF_RDONLY = 0x00,
			OF_WRONLY = 0x01,
			OF_RDWR = 0x02,
			OF_CREAT = 0x04,
			OF_SYNC = 0x08,        // wirte througt
			OF_TRUNC = 0x10,       // create file,if exits create it
			OF_SHARE_READ = 0x20,  // only for windows,other open handle can read
			OF_SHARE_WRITE = 0x40,  // only for windows,other open handle can write
			OF_APPEND_DATA = 0X80
		};
		enum SeekPosition { seek_set = 0, seek_cur = 1, seek_end = 2 };
		_USE_EC_OBJ_ALLOCATOR
	protected:
#ifdef _WIN32
		HANDLE		m_hFile;
#else
		int			m_hFile;
#endif
		int         _sharemode;
	public:
		inline bool IsOpen() { return m_hFile != INVALID_HANDLE_VALUE; };
#ifdef _WIN32

		union UV {
			struct {
				unsigned int  l;
				unsigned int  h;
			};
			long long v;
		};

		bool	 Open(const char* utf8filename, unsigned int nOpenFlags, unsigned int sharemode = 0) //Open File
		{
			if (!utf8filename || !*utf8filename)
				return false;
			wchar_t sfile[512];
			sfile[0] = 0;
			if (!MultiByteToWideChar(CP_UTF8, 0, utf8filename, -1, sfile, sizeof(sfile) / sizeof(wchar_t)))
				return false;

			_sharemode = sharemode;
			unsigned int dwAccess = 0;
			switch (nOpenFlags & 3)
			{
			case OF_RDONLY:
				dwAccess = GENERIC_READ;
				break;
			case OF_WRONLY:
				dwAccess = GENERIC_WRITE;
				break;
			case OF_RDWR:
				dwAccess = GENERIC_READ | GENERIC_WRITE;
				break;
			default:
				dwAccess = GENERIC_READ;
				break;
			}
			unsigned int dwShareMode = 0;
			if (OF_SHARE_READ & sharemode)
				dwShareMode |= FILE_SHARE_READ;
			if (OF_SHARE_WRITE & sharemode)
				dwShareMode |= FILE_SHARE_WRITE;

			// map modeNoInherit flag
			SECURITY_ATTRIBUTES sa;
			sa.nLength = sizeof(sa);
			sa.lpSecurityDescriptor = NULL;
			sa.bInheritHandle = true;

			unsigned int dwCreateFlag;
			if (nOpenFlags & OF_CREAT)
			{
				if (nOpenFlags & OF_TRUNC)
					dwCreateFlag = CREATE_ALWAYS;// create file,if exits create it
				else
					dwCreateFlag = CREATE_NEW;// Creates a new file, only if it does not already exist.
			}
			else
				dwCreateFlag = OPEN_EXISTING;

			unsigned int dwFlags = FILE_ATTRIBUTE_NORMAL;
			if (nOpenFlags & OF_SYNC)
				dwFlags |= FILE_FLAG_WRITE_THROUGH;
			if (nOpenFlags & OF_APPEND_DATA)
				dwAccess = GENERIC_WRITE | FILE_APPEND_DATA;
			HANDLE hFile = ::CreateFileW(sfile, dwAccess, dwShareMode, &sa,
				dwCreateFlag, dwFlags, NULL);

			if (hFile == INVALID_HANDLE_VALUE)
				return false;

			m_hFile = hFile;
			return true;
		};

		void	 Close() {
			if (m_hFile != INVALID_HANDLE_VALUE)
				::CloseHandle(m_hFile);
			m_hFile = INVALID_HANDLE_VALUE;
		};

		///\breif return filepos or -1 with error
		long long	Seek(long long lOff, int nFrom)
		{
			if (m_hFile == INVALID_HANDLE_VALUE)
				return -1;

			LARGE_INTEGER liOff;

			liOff.QuadPart = lOff;
			liOff.LowPart = ::SetFilePointer(m_hFile, liOff.LowPart, &liOff.HighPart,
				nFrom);
			if (liOff.LowPart == 0xFFFFFFFF)
				if (::GetLastError() != NO_ERROR)
					return -1;
			return liOff.QuadPart;
		};

		///\breif return number of readbytes or -1 with error
		int Read(void* buf, unsigned int ucount)
		{
			if (m_hFile == INVALID_HANDLE_VALUE)
				return -1;
			DWORD dwRead = 0;
			if (!::ReadFile(m_hFile, buf, ucount, &dwRead, NULL))
				return -1;
			return (int)dwRead;
		}

		///\breif return number of writebytes or -1 with error
		int Write(const void* buf, unsigned int ucount)
		{
			if (m_hFile == INVALID_HANDLE_VALUE)
				return -1;
			DWORD dwRead = 0;
			if (!::WriteFile(m_hFile, buf, ucount, &dwRead, NULL))
				return -1;
			return (int)dwRead;
		}

		bool Lock(long long offset, long long lsize, bool bExc) // lsize 0 means to EOF
		{
			if (m_hFile == INVALID_HANDLE_VALUE)
				return false;
			UV pos, sz;
			pos.v = offset;
			if (!lsize) {
				sz.h = 0xffffffff;
				sz.l = 0xffffffff;
			}
			else
				sz.v = lsize;

			OVERLAPPED	op;
			memset(&op, 0, sizeof(op));
			op.Offset = pos.l;
			op.OffsetHigh = pos.h;

			unsigned int uf = 0;
			if (bExc)
				uf = LOCKFILE_EXCLUSIVE_LOCK;
			return LockFileEx(m_hFile, uf, 0, sz.l, sz.h, &op) != 0;
		}
		bool Unlock(long long offset, long long lsize) // lsize 0 means to EOF
		{
			if (m_hFile == INVALID_HANDLE_VALUE)
				return false;

			UV pos, sz;
			pos.v = offset;
			if (!lsize) {
				sz.h = 0xffffffff;
				sz.l = 0xffffffff;
			}
			else
				sz.v = lsize;

			OVERLAPPED	op;
			memset(&op, 0, sizeof(op));
			op.Offset = pos.l;
			op.OffsetHigh = pos.h;

			return UnlockFileEx(m_hFile, 0, sz.l, sz.h, &op) != 0;
		}
		size_t getfilesize()
		{
			if (m_hFile == INVALID_HANDLE_VALUE)
				return 0;
			DWORD dwh = 0;
			size_t ret = GetFileSize(m_hFile, &dwh);
#ifdef _WIN64
			if (dwh)
				ret = (((size_t)dwh) << 32) | ret;
#endif
			return ret;
		}
#else
		/*!
		\brief open file
		\remark  sharemode no use for linux
		*/
		bool Open(const char* sfile, unsigned int nOpenFlags, unsigned int sharemode = 0)
		{
			if (!sfile)
				return false;
			_sharemode = OF_SHARE_READ | OF_SHARE_WRITE;
			int oflags = 0;
			switch (nOpenFlags & 3)
			{
			case OF_RDONLY:
				oflags = O_RDONLY;
				break;
			case OF_WRONLY:
				oflags = O_WRONLY;
				break;
			case OF_RDWR:
				oflags = O_RDWR;
				break;
			default:
				oflags = O_RDONLY;
				break;
			}
			if (nOpenFlags & OF_CREAT)
				oflags |= O_CREAT | O_EXCL;

			if (nOpenFlags & OF_TRUNC)
				oflags |= O_TRUNC;

			unsigned int dwFlag = 0;
			if (nOpenFlags & OF_SYNC)
				dwFlag = O_SYNC | O_RSYNC;

			if (nOpenFlags & OF_APPEND_DATA)
				oflags |= O_WRONLY | O_APPEND;

			int hFile = ::open64(sfile, oflags | O_CLOEXEC, S_IROTH | S_IXOTH | S_IRWXU | S_IRWXG | dwFlag);//must add S_IRWXG usergroup can R&W

			if (hFile == INVALID_HANDLE_VALUE)
				return false;
			m_hFile = hFile;
			return true;
		};

		bool	 Close() {
			if (m_hFile != INVALID_HANDLE_VALUE)
				::close(m_hFile);
			m_hFile = INVALID_HANDLE_VALUE;
			return true;
		};

		///\breif return filepos or -1 with error
		long long	Seek(long long lOff, int nFrom)
		{
			if (m_hFile == INVALID_HANDLE_VALUE)
				return -1;
			return ::lseek64(m_hFile, lOff, nFrom);
		};

		///\breif return number of readbytes or -1 with error
		int Read(void* buf, unsigned int ucount)
		{
			if (m_hFile == INVALID_HANDLE_VALUE)
				return -1;
			ssize_t nr = ::read(m_hFile, buf, ucount);
			return (int)nr;
		}

		///\breif return number of writebytes or -1 with error
		int Write(const void* buf, unsigned int ucount)
		{
			if (m_hFile == INVALID_HANDLE_VALUE)
				return -1;
			ssize_t nr = ::write(m_hFile, buf, ucount);
			return (int)nr;
		}

		bool Lock(long long offset, long long lsize, bool bExc) // lsize 0 means to EOF
		{
			if (m_hFile == INVALID_HANDLE_VALUE)
				return false;
			struct flock    lock;

			if (bExc)
				lock.l_type = F_WRLCK;  /* F_RDLCK, F_WRLCK, F_UNLCK */
			else
				lock.l_type = F_RDLCK;

			lock.l_start = offset;    /* byte offset, relative to l_whence */
			lock.l_whence = SEEK_SET; /* SEEK_SET, SEEK_CUR, SEEK_END */
			lock.l_len = lsize;       /* #bytes (0 means to EOF) */
			lock.l_pid = ::getpid();
			return !(fcntl(m_hFile, F_SETLKW, &lock) < 0);
		}

		bool Unlock(long long offset, long long lsize) // lsize 0 means to EOF
		{
			if (m_hFile == INVALID_HANDLE_VALUE)
				return false;
			struct flock    lock;

			lock.l_type = F_UNLCK;  /* F_RDLCK, F_WRLCK, F_UNLCK */
			lock.l_start = offset;    /* byte offset, relative to l_whence */
			lock.l_whence = SEEK_SET; /* SEEK_SET, SEEK_CUR, SEEK_END */
			lock.l_len = lsize;       /* #bytes (0 means to EOF) */
			lock.l_pid = ::getpid();

			return !(fcntl(m_hFile, F_SETLKW, &lock) < 0);
		}
		size_t getfilesize(const char* filepath)
		{
			struct stat info;
			if (!stat(filepath, &info))
				return info.st_size;
			return 0;
		}
#endif
		inline int ReadFrom(long long loff, void* buf, unsigned int ucount)
		{
			if (Seek(loff, seek_set) < 0)
				return -1;
			return Read(buf, ucount);
		};
		inline int WriteTo(long long loff, const void* buf, unsigned int ucount)
		{
			if (Seek(loff, seek_set) < 0)
				return -1;
			return Write(buf, ucount);
		};

#ifdef FILE_GROWN_FILLZERO
		bool FastGrown(int nsize)
		{
			char stmp[32768];
			if (nsize < 0 || Seek(0, seek_end) < 0)
				return false;
			memset(stmp, 0, sizeof(stmp));
			int n = nsize;
			while (n > 0) {
				if (n >= (int)sizeof(stmp)) {
					if (Write(stmp, (unsigned int)sizeof(stmp)) != (int)sizeof(stmp))
						return false;
				}
				else {
					if (Write(stmp, (unsigned int)n) != n)
						return false;
				}
				n -= (int)sizeof(stmp);
			}
#ifdef _WIN32
			if (!::SetEndOfFile(m_hFile))
				return false;
#else
			fsync(m_hFile);
#endif
			return true;
		}
		bool FastGrownTo(int64_t lsize, bool bprtinfo = false)
		{
			char stmp[64 * 1024];
			int64_t lend = Seek(0, seek_end);
			if (lsize < 0 || lend < 0)
				return false;
			if (lend >= lsize)
				return true;
			memset(stmp, 0, sizeof(stmp));
			int64_t n = lsize - lend, n100 = n / 100;
			int np = 100, ncur;
			while (n > 0) {
				if (n >= (int)sizeof(stmp)) {
					if (Write(stmp, (unsigned int)sizeof(stmp)) != (int)sizeof(stmp))
						return false;
				}
				else {
					if (Write(stmp, (unsigned int)n) != n)
						return false;
				}
				n -= (int)sizeof(stmp);
				if (bprtinfo) {
					ncur = (int)(n / n100);
					if (np != ncur) {
						printf("\r%%%3d", 100 - np);
						fflush(stdout);
						np = ncur;
					}
				}
			}
#ifdef _WIN32
			if (!::SetEndOfFile(m_hFile))
				return false;
#else
			fsync(m_hFile);
#endif
			if (bprtinfo)
				printf("\r%%100\n");
			return true;
		}
#else
		bool FastGrown(int nsize)
		{
			int64_t lend = Seek(0, seek_end);
			if (nsize < 0 || lend < 0)
				return false;
			if (Seek(lend + nsize - 1, seek_set) < 0)
				return false;
			char c = 0;
			if (1 != Write(&c, 1))
				return false;
#ifdef _WIN32
			if (!::SetEndOfFile(m_hFile))
				return false;
#else
			fsync(m_hFile);
#endif
			return true;
		}
		bool FastGrownTo(int64_t lsize)
		{
			if (lsize < 0 || Seek(lsize - 1, seek_set) < 0)
				return false;
			char c = 0;
			if (1 != Write(&c, 1))
				return false;
#ifdef _WIN32
			if (!::SetEndOfFile(m_hFile))
				return false;
#else
			fsync(m_hFile);
#endif
			return true;
		}
#endif

		bool flush()
		{
#ifdef _WIN32
			return ::FlushFileBuffers(m_hFile) != 0;
#else
			return !fsync(m_hFile);
#endif
		}

		long long curpos()
		{
			return Seek(0, seek_cur);
		}

		bool truncate() // set file size to current position
		{
#ifdef _WIN32
			if (!::SetEndOfFile(m_hFile))
				return false;
#else
			long long lpos = curpos();
			if (lpos < 0)
				return false;
			ftruncate(m_hFile, (off_t)lpos);
#endif
			return true;
		}
	};//CFile

	/*!
	\brief safe use file lock
	*/
	class unique_filelock
	{
	public:
		unique_filelock(File* pf, long long offset, long long lsize, bool bExc)
		{
			_pf = pf;
			_offset = offset;
			_lsize = lsize;
			_bExc = bExc;
			if (_pf)
				_pf->Lock(_offset, _lsize, _bExc);
		}
		~unique_filelock()
		{
			if (_pf)
				_pf->Unlock(_offset, _lsize);
		}
	private:
		File* _pf;
		long long _offset;
		long long _lsize;
		bool _bExc;
	};
};//ec
