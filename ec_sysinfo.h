/*!
\file ec_sysinfo.h
\author	kipway@outlook.com
\update 
2023.5.18 use ec::string

eclib system infomation class

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
#include "Psapi.h"
#pragma  comment(lib,"Psapi.lib")
#define DECLARE_DLL_FUNCTION(fn, type, dll) \
    auto fn = reinterpret_cast<type>(GetProcAddress(GetModuleHandleW(L##dll), #fn))
#else
#include "ec_diskio.h"
#endif
namespace ec {
	class sys_info
	{
	public:
		/*!
		\breif 获取当前进程的句柄使用数量
		\return 句柄数
		\remark linux对应/proc/$pid$/fd目录下的子文件数量
		*/
		static uint32_t getprocesshandlecount() {
#ifdef _WIN32
			DWORD dwHandleCount = 0;
			GetProcessHandleCount(GetCurrentProcess(), &dwHandleCount);
			return dwHandleCount;
#else
			uint32_t _count = 0;
			char file[128];
			sprintf(file, "/proc/%d/fd", getpid());
			int _isdir;
			ec::cdir _dir(file);
			ec::string _s;
			while (_dir.next(_s, &_isdir)) {
				if (!_isdir)
					++_count;
			}
			return _count;
#endif
		}
		/*!
		\breif 获取当前进程的内存占用
		\return 内存占用字节数
		\remark linux对应top命令的RES列(使用物理内存的大小), windows为资源监视器中的工作集内存
		*/
		static int64_t getprocessmem()
		{
#ifdef _WIN32
			HANDLE handle = GetCurrentProcess();
			PROCESS_MEMORY_COUNTERS pmc;
			if (GetProcessMemoryInfo(handle, &pmc, sizeof(pmc)))
				return pmc.WorkingSetSize;
			return 0;
#else
			FILE *fd;
			char name[32], line_buff[512], file[64] = { 0 };
			int i, vmrss = 0;
			line_buff[sizeof(line_buff) - 1] = 0;

			sprintf(file, "/proc/%d/status", getpid());
			fd = fopen(file, "r");
			if (fd == nullptr)
				return 0;
			for (i = 0; i < 40; i++) {
				if (fgets(line_buff, sizeof(line_buff) - 1, fd) == nullptr)
					break;
				if (strstr(line_buff, "VmRSS:") != nullptr) {
					sscanf(line_buff, "%s %d", name, &vmrss);
					break;
				}
			}
			fclose(fd);
			return vmrss * 1024LL;
#endif
		}

		class cpurate
		{
		public:
#ifdef _WIN32
			ULONGLONG _all_time, _process_time;
			cpurate() :_all_time(0), _process_time(0) {
			}

			/*!
			\breif 获取CPU占用率
			\return 返回两位小数的定点百分数。比如391表示3.91%
			\remark 和资源监视器中的平均CPU占用率算法一致, 建议调用间隔时间大于等于1秒
			*/
			int32_t getcpurate(int *pout_num_threads = nullptr)
			{
				HANDLE hProcess = GetCurrentProcess(); //应该等于 -1
				if (!hProcess)
					return 0;
				FILETIME ft_create, ft_exit, ft_kernel, ft_user, ft_sysidle, ft_syskernel, ft_sysuser;
				if (!GetProcessTimes(hProcess, &ft_create, &ft_exit, &ft_kernel, &ft_user) ||
					!GetSystemTimes(&ft_sysidle, &ft_syskernel, &ft_sysuser)
					) {
					CloseHandle(hProcess);
					return 0;
				}
				ULARGE_INTEGER	uk, us, sidle, skernel, suser;
				uk.LowPart = ft_kernel.dwLowDateTime;
				uk.HighPart = ft_kernel.dwHighDateTime;

				us.LowPart = ft_user.dwLowDateTime;
				us.HighPart = ft_user.dwHighDateTime;

				sidle.LowPart = ft_sysidle.dwLowDateTime;
				sidle.HighPart = ft_sysidle.dwHighDateTime;

				skernel.LowPart = ft_syskernel.dwLowDateTime;
				skernel.HighPart = ft_syskernel.dwHighDateTime;

				suser.LowPart = ft_sysuser.dwLowDateTime;
				suser.HighPart = ft_sysuser.dwHighDateTime;

				int64_t nrate = 0;
				if (_all_time) {
					ULONGLONG fta = (skernel.QuadPart + suser.QuadPart) - _all_time;
					ULONGLONG ftp = (uk.QuadPart + us.QuadPart) - _process_time;
					if (fta != 0)
						nrate = (ftp * 10000) / fta;
				}
				_all_time = skernel.QuadPart + suser.QuadPart;
				_process_time = uk.QuadPart + us.QuadPart;

				CloseHandle(hProcess);
				return (int32_t)nrate;
			}
#else
			int64_t _all_time, _process_time;
			cpurate() : _all_time(0), _process_time(0) {
			}

			int64_t getalltime(int &ncpu)
			{
				ncpu = 0;
				FILE *fd;
				fd = fopen("/proc/stat", "r");
				if (fd == nullptr)
					return 0;
				char line_buff[1024];// cpu  221962793 4276 106627720 6703113085 2025391 0 1119229 0 0 0
				line_buff[sizeof(line_buff) - 1] = 0;
				if (fgets(line_buff, sizeof(line_buff) - 1, fd) == nullptr) {
					fclose(fd);
					return 0;
				}
				char swords[48] = { 0 };
				size_t pos = 0, zlen = strlen(line_buff);
				int nt = 0;
				int64_t nr = 0;
				while (ec::strnext('\x20', line_buff, zlen, pos, swords, sizeof(swords) - 1)) {
					if (nt > 0 && nt < 9) {
						nr += atol(swords);
					}
					else if (nt >= 8)
						break;
					++nt;
				}
				while (fgets(line_buff, sizeof(line_buff) - 1, fd)) { //读取CPU核心数
					if (line_buff[0] != 'c' || line_buff[1] != 'p' || line_buff[2] != 'u')
						break;
					++ncpu;
				}
				fclose(fd);
				return  nr;
			}

			int64_t getproctime(int32_t *pnum_threads)
			{
				FILE *fd;
				char line_buff[1024], file[64] = { 0 };
				line_buff[sizeof(line_buff) - 1] = 0;
				sprintf(file, "/proc/%d/stat", getpid());
				fd = fopen(file, "r");
				if (fd == nullptr)
					return 0;
				if (fgets(line_buff, sizeof(line_buff) - 1, fd) == nullptr) {
					fclose(fd);
					return 0;
				}
				fclose(fd);
				char swords[48] = { 0 };
				size_t pos = 0, zlen = strlen(line_buff);
				int nt = 0;
				int64_t nr = 0;
				while (ec::strnext('\x20', line_buff, zlen, pos, swords, sizeof(swords) - 1)) {
					++nt;
					if (nt >= 14 && nt <= 17) // utime,stime,cutime,cstime
						nr += atol(swords);
					else if (nt == 20) { // num_threads
						if (pnum_threads)
							*pnum_threads = atoi(swords);
						break;
					}
				}
				return  nr;
			}

			/*!
			\breif 获取CPU占用率
			\return 返回两位小数的定点百分数。比如391表示3.91%
			\remark 使用 top 命令的计算方式，相对于单核的占用率，可能大于100%, 建议调用间隔时间大于等于1秒
			*/
			int32_t getcpurate(int *pout_num_threads = nullptr)
			{
				int ncpu = 0;
				int64_t nrate = 0;
				int64_t tall = getalltime(ncpu);
				int64_t tproc = getproctime(pout_num_threads);
				if (!ncpu)
					ncpu = 1;
				if (_all_time && tall > _all_time) {
					nrate = ((tproc - _process_time) * 10000 * ncpu) / (tall - _all_time);
				}
				if (!_all_time
					|| tproc - _process_time > sysconf(_SC_CLK_TCK) / 10
					|| tall - _all_time > 5 * sysconf(_SC_CLK_TCK)) {
					_all_time = tall;
					_process_time = tproc;
				}
				return (int32_t)nrate;
			}
#endif
		};

		static int getosinfo(char *sout, size_t outsize)
		{
#ifdef _WIN32
			OSVERSIONINFOW info;
			memset(&info, 0, sizeof(info));
			info.dwOSVersionInfoSize = sizeof(info);

			constexpr LONG kStatusSuccess = 0L;
			DECLARE_DLL_FUNCTION(RtlGetVersion, LONG(WINAPI*)(PRTL_OSVERSIONINFOW), "ntdll.dll");
			if (!RtlGetVersion)
				return -1;
			RtlGetVersion(&info);
			return snprintf(sout, outsize, "windows version %u.%u, build %u", info.dwMajorVersion, info.dwMinorVersion, info.dwBuildNumber);
#else
			FILE *fd;
			fd = fopen("/proc/version", "r");
			if (fd == nullptr)
				return -1;
			int c;
			size_t n = 0;
			while (EOF != (c = fgetc(fd))) {
				if ('#' == c)
					break;
				if (n + 1 < outsize)
					sout[n] = (char)c;
				++n;
			}
			if (n < outsize)
				sout[n] = 0;
			else sout[outsize - 1] = 0;
			fclose(fd);
			return n;
#endif
		}
	}; // sys_info
}// namespace ec
