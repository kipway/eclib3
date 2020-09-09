/*
\file ec_time.h
\author	jiangyong
\email	kipway@outlook.com
\update 2020.9.6

cTime
	wrapper class for time
cDateTime
	date time 
cJobTime
	job timer class
cBps
	weighted average traffic count

eclib 3.0 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/

#pragma once

#ifdef _WIN32
#pragma warning (disable : 4996)
#else
#include <sys/time.h>
#endif
#include <time.h>
#include <stdint.h>
#include "ec_string.h"
#include "ec_mutex.h"
#ifndef _WIN32
inline unsigned int GetTickCount()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned int)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

inline uint64_t GetTickCount64()
{
	struct timeval tv;
	gettimeofday(&tv, nullptr);
	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}
#endif

namespace ec
{
	/*!
	\param lfiletime [in] 100 nanoseconds from 1601/01/01
	\param pMicrosecond [out] microsecond,1000000 microseconds = 1 second
	\return seconds from 1970/01/01
	*/
	inline int64_t ftime2timet(int64_t lfiletime, int *pMicrosecond)
	{
		if (pMicrosecond)
			*pMicrosecond = (int)((lfiletime % 10000000LL) / 10);
		return (lfiletime / 10000000LL) - 11644473600LL;
	}

	/*!
	\param pMicrosecond [out] microsecond,1000000 microseconds = 1 second
	\return seconds from 1970/01/01
	*/
	inline time_t nstime(int *pMicrosecond)
	{
#ifdef _WIN32
		FILETIME ft;
		GetSystemTimeAsFileTime(&ft);
		ULARGE_INTEGER ul;
		ul.LowPart = ft.dwLowDateTime;
		ul.HighPart = ft.dwHighDateTime;
		if (pMicrosecond)
			*pMicrosecond = (int)((ul.QuadPart % 10000000LL) / 10);
		return (time_t)(ul.QuadPart / 10000000LL) - 11644473600LL;
#else
		struct timeval tv;
		gettimeofday(&tv, nullptr);
		if (pMicrosecond)
			*pMicrosecond = (int)tv.tv_usec;
		return tv.tv_sec;
#endif
	}

	inline long long time_ns() // return microsecond from from 1970/01/01,1000000 microseconds = 1 second
	{
#ifdef _WIN32
		FILETIME ft;
		GetSystemTimeAsFileTime(&ft);
		ULARGE_INTEGER ul;
		ul.LowPart = ft.dwLowDateTime;
		ul.HighPart = ft.dwHighDateTime;

		return (long long)((ul.QuadPart / 10000000LL) - 11644473600LL) * 1000000ll + ((ul.QuadPart % 10000000LL) / 10);
#else
		struct timeval tv;
		gettimeofday(&tv, nullptr);
		return tv.tv_sec * 1000000ll + tv.tv_usec;
#endif
	}

	inline uint32_t ustick()
	{
#ifdef _WIN32
		FILETIME ft;
		GetSystemTimeAsFileTime(&ft);
		ULARGE_INTEGER ul;
		ul.LowPart = ft.dwLowDateTime;
		ul.HighPart = ft.dwHighDateTime;
		return (uint32_t)((ul.QuadPart % 10000000LL) / 10);

#else
		struct timeval tv;
		gettimeofday(&tv, nullptr);
		return (uint32_t)tv.tv_usec;
#endif
	}

	inline int64_t mstime() // Return milliseconds since 1970-1-1
	{
#ifdef _WIN32
		FILETIME ft;
		GetSystemTimeAsFileTime(&ft);
		ULARGE_INTEGER ul;
		ul.LowPart = ft.dwLowDateTime;
		ul.HighPart = ft.dwHighDateTime;
		return (int64_t)((ul.QuadPart / 10000000LL) - 11644473600LL) * 1000 + (ul.QuadPart % 10000000LL) / 10000;
#else
		struct timeval tv;
		gettimeofday(&tv, nullptr);
		return (int64_t)(tv.tv_sec * 1000LL + tv.tv_usec / 1000);
#endif
	}

	class cTime
	{
	public:
		cTime()
		{
			SetTime(::time(0));
		};
		cTime(time_t gmt)
		{
			SetTime(gmt);
		};
		cTime(int nyear, int nmon, int nday)
		{
			SetTime(nyear, nmon, nday);
		};
		cTime(int nyear, int nmon, int nday, int nhour, int nmin, int nsec)
		{
			SetTime(nyear, nmon, nday, nhour, nmin, nsec);
		};
		~cTime() {};
		void SetTime(int nyear, int nmon, int nday)
		{
			struct tm t;
			time_t tmp = ::time(NULL);
			t = *localtime(&tmp);
			t.tm_year = nyear - 1900;
			t.tm_mon = nmon - 1;
			t.tm_mday = nday;
			t.tm_hour = 0;
			t.tm_min = 0;
			t.tm_sec = 0;
			_gmt = mktime(&t);
			_year = nyear;
			_mon = nmon;
			_day = nday;
			_hour = 0;
			_min = 0;
			_sec = 0;
		}
		void SetTime(int nyear, int nmon, int nday, int nhour, int nmin, int nsec)
		{
			struct tm t;
			time_t tmp = ::time(NULL);
			t = *localtime(&tmp);
			t.tm_year = nyear - 1900;
			t.tm_mon = nmon - 1;
			t.tm_mday = nday;
			t.tm_hour = nhour;
			t.tm_min = nmin;
			t.tm_sec = nsec;
			_gmt = mktime(&t);

			_year = nyear;
			_mon = nmon;
			_day = nday;
			_hour = nhour;
			_min = nmin;
			_sec = nsec;
		}

		inline time_t GetTime() const
		{
			return _gmt;
		};
		void SetTime(time_t gmt)
		{
			_gmt = gmt;
			struct tm *ptm = localtime(&_gmt);
			if (ptm) {
				_year = ptm->tm_year + 1900;
				_mon = ptm->tm_mon + 1;
				_day = ptm->tm_mday;
				_hour = ptm->tm_hour;
				_min = ptm->tm_min;
				_sec = ptm->tm_sec;
			}
			else {
				_year = 1970;
				_mon = 1;
				_day = 1;
				_hour = 0;
				_min = 0;
				_sec = 0;
				_gmt = 0;
			}
		}
		cTime& operator = (time_t gmt)
		{
			SetTime(gmt);
			return *this;
		}
		void tostring(char* sout, size_t sizeout, bool hastime = true)
		{
			if (hastime)
				snprintf(sout, sizeout, "%d/%d/%d %d:%d:%d", _year, _mon, _day, _hour, _min, _sec);
			else
				snprintf(sout, sizeout, "%d/%d/%d", _year, _mon, _day);
		}
		void tostring_ag(char* sout, size_t sizeout, bool hastime = true)
		{
			if (hastime)
				snprintf(sout, sizeout, "%d/%02d/%02d %02d:%02d:%02d", _year, _mon, _day, _hour, _min, _sec);
			else
				snprintf(sout, sizeout, "%d/%02d/%02d", _year, _mon, _day);
		}

		int weekday()   // 1=monday,..., 7=sunday, 0:error
		{
			if (!_gmt)
				return 0;
			return ((_gmt / 86400) % 7 + 3) % 7 + 1;// 1970/1/1 is Thursday
		}
	protected:
		time_t _gmt; // GMT time
	public:
		int _year, _mon, _day, _hour, _min, _sec; //local
	};

	/*!
	\brief date time
	fmt:
	yyyy/mm/dd HH:MM:SS  or yyyy/mm/dd HH:MM:SS.mmm
	yyyy-mm-dd HH:MM:SS  or yyyy-mm-dd HH:MM:SS.mmm
	*/
	class cDateTime
	{
	public:
		cDateTime() : _nyear(0), _nmon(0), _nday(0), _nhour(0), _nmin(0), _nsec(0), _nmsec(0), _gmt(-1) { };
		cDateTime(const char *s) : _nyear(0), _nmon(0), _nday(0), _nhour(0), _nmin(0), _nsec(0), _gmt(-1)
		{
			parse(s);
		}
	public:
		int _nyear, _nmon, _nday, _nhour, _nmin, _nsec, _nmsec;
		time_t _gmt;
		inline bool IsOk()
		{
			return _gmt > 0;
		}
		bool parse(const char* s)
		{
			_gmt = -1;
			char sd[16] = { 0 }, st[16] = { 0 }, sf[8] = { 0 };
			size_t pos = 0, nsize = strlen(s);
			if (!ec::strnext('\x20', s, nsize, pos, sd, sizeof(sd)))
				return false;
			ec::strnext('\x20', s, nsize, pos, st, sizeof(st));
			int np = 0, n = 0;
			char *sp = sd;
			while (*sp) {
				if (*sp == '/' || *sp == '-') {
					sf[n++] = 0;
					np++;
					if (np > 2)
						return false;
					if (np == 1)
						_nyear = atoi(sf);
					else if (np == 2)
						_nmon = atoi(sf);
					n = 0;
				}
				else if (*sp < '0' || *sp > '9')
					return false;
				else {
					sf[n++] = *sp;
					if (n >= 7)
						return false;
				}
				sp++;
			}
			if (np != 2 || !n)
				return false;
			sf[n] = 0;
			_nday = atoi(sf);
			_nhour = 0;
			_nmin = 0;
			_nsec = 0;
			_nmsec = 0;
			if (st[0]) { //has time filed
				np = 0;
				n = 0;
				sp = st;
				while (*sp) {
					if (*sp == ':') {
						sf[n++] = 0;
						np++;
						if (np > 2)
							return false;
						if (np == 1)
							_nhour = atoi(sf);
						else if (np == 2)
							_nmin = atoi(sf);
						n = 0;
					}
					else if (*sp == '.') {
						sf[n++] = 0;
						np++;
						if (np == 3) {
							_nsec = atoi(sf);
							if (_nsec > 59 || _nsec < 0)
								return false;
						}
						n = 0;
					}
					else if (*sp < '0' || *sp > '9')
						return false;
					else {
						sf[n++] = *sp;
						if (n >= 7)
							return false;
					}
					sp++;
				}
				if (np < 2 || !n)
					return false;
				sf[n] = 0;
				if (np == 2)
					_nsec = atoi(sf);
				else if (np == 3)
					_nmsec = atoi(sf);
				if (_nmsec < 0 || _nmsec > 999)
					return false;
			}
			if (_nyear < 1970 || _nmon > 12 || _nmon < 1 || _nday > 31 || _nday < 1
				|| _nhour > 23 || _nhour < 0 || _nmin < 0 || _nmin > 59 || _nsec < 0 || _nsec > 59)
				return false;
			ec::cTime t(_nyear, _nmon, _nday, _nhour, _nmin, _nsec);
			_gmt = t.GetTime();
			if (_gmt < 0)
				return false;
			ec::cTime t2(_gmt);
			if (t2._year != _nyear || t2._mon != _nmon || t2._day != _nday
				|| t2._hour != _nhour || t2._min != _nmin || t2._sec != _nsec) {
				_gmt = -1;
				return false;
			}
			return true;
		}
		bool parse_n(const char* s, size_t len)
		{
			char st[32] = { 0 };
			if (len >= sizeof(st))
				return false;
			memcpy(st, s, len);
			return parse(st);
		}
		int weekday()   // 1=monday,..., 7=sunday, 0:error ; Note that it is GMT
		{
			if (!_gmt)
				return 0;
			return ((_gmt / 86400) % 7 + 3) % 7 + 1; // 1970/1/1 is Thursday
		}
	};

#define DAY_TIME(h,m,s) (h * 3600 + m * 60 + s)
#define DAY_HOUR(t)	(t/3600)
#define DAY_MIN(t)  ((t%3600)/60)
#define DAY_SEC(t)	(t%60)

	class cJobTime
	{
	public:
		cJobTime()
		{
			SetTime(0, 0, 0);
		}
		cJobTime(int nsec)
		{
			SetTime(nsec);
		}
		cJobTime(int hour, int min, int sec)//hour,min,sec is local time zone
		{
			SetTime(hour, min, sec);
		}
		bool IsJobTime(time_t cur = 0)
		{
			time_t tcur = cur;
			if (!cur)
				tcur = ::time(NULL);
			if ((tcur - _ti_o) >= _ti_add) {
				CalNextDay();
				return true;
			}
			return false;
		}
		void SetTime(int hour, int min, int sec)//hour,min,sec is local time zone
		{
			_utimeinit = DAY_TIME(hour, min, sec);
			CalNextDay();
		}
		void SetTime(int secs)
		{
			_utimeinit = secs;
			CalNextDay();
		}
		inline int Getjobtime()
		{
			return (int)_utimeinit;
		}
	protected:
		unsigned int _utimeinit;//second from 0:0:0 local time zone
		time_t		_ti_o;
		time_t		_ti_add;

		void CalNextDay()
		{
			time_t tcur = time(NULL);
			ec::cTime curt(tcur);
			_ti_add = (_utimeinit + 86400 - DAY_TIME(curt._hour, curt._min, curt._sec)) % 86400;
			if (_ti_add == 0)
				_ti_add = 86400;
			_ti_o = tcur;
		}
	};

#define BPS_DATA_ITEMS  5 // 5 second Weighted average
	class cBps // Second flow
	{
	public:
		struct T_I {
			int32_t  nid;
			uint32_t ures;
			uint32_t ubytes[BPS_DATA_ITEMS];
			int64_t  timet[BPS_DATA_ITEMS];
		};
		cBps(int nsize = 16)
		{
			int i;
			if (nsize <= 0)
				nsize = 16;
			_items = (T_I*)malloc(nsize * sizeof(T_I));
			if (!_items)
				_nsize = 0;
			else
				_nsize = nsize;
			memset(_items, 0, _nsize * sizeof(T_I));
			for (i = 0; i < _nsize; i++)
				_items[i].nid = -1;
		};
		virtual ~cBps()
		{
			if (_items) {
				free(_items);
				_items = nullptr;
			}
			_nsize = 0;
		}

	protected:
		spinlock _cs;
		int  _nsize;
		T_I* _items;
	public:
		int32_t AllocOne()
		{
			unique_spinlock lck(&_cs);
			int i;
			for (i = 0; i < _nsize; i++) {
				if (_items[i].nid == -1) {
					_items[i].nid = i;
					for (int j = 0; j < BPS_DATA_ITEMS; j++) {
						_items[i].timet[j] = 0;
						_items[i].ubytes[j] = 0;
					}
					return _items[i].nid;
				}
			}
			return -1;
		}

		void DelOne(int32_t nid)
		{
			unique_spinlock lck(&_cs);
			if (nid < 0 || nid >= _nsize)
				return;
			_items[nid].nid = -1;
			for (int j = 0; j < BPS_DATA_ITEMS; j++) {
				_items[nid].timet[j] = 0;
				_items[nid].ubytes[j] = 0;
			}
		}

		bool Set(int32_t nid, uint32_t nbytes)
		{
			unique_spinlock lck(&_cs);
			if (nid < 0 || nid >= _nsize)
				return false;
			int64_t curtime = mstime();
			if (curtime - _items[nid].timet[0] > 1000) {
				for (int j = BPS_DATA_ITEMS - 1; j > 0; j--) {
					_items[nid].timet[j] = _items[nid].timet[j - 1];
					_items[nid].ubytes[j] = _items[nid].ubytes[j - 1];
				}
				_items[nid].timet[0] = curtime;
				_items[nid].ubytes[0] = 0;
			}
			_items[nid].ubytes[0] += nbytes;
			return true;
		}

		uint32_t Get(int32_t nid)
		{
			unique_spinlock lck(&_cs);
			if (nid < 0 || nid >= _nsize)
				return 0;
			int ni = 0;
			int64_t timet = mstime();
			double xs[BPS_DATA_ITEMS] = { 0.5, 0.25, 0.125, 0.075, 0.05 }, dblr = 0;
			for (int j = 0; j < BPS_DATA_ITEMS; j++) {
				ni = (int)(timet - _items[nid].timet[j]) / 1000;
				if (ni >= 0 && ni < BPS_DATA_ITEMS)
					dblr += _items[nid].ubytes[j] * xs[ni];
			}
			return (uint32_t)dblr;
		}
	};
}; // ec
