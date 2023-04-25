/*
\file ec_time.h
\author	jiangyong
\email	kipway@outlook.com
\update
 2023.2.22 Fix string2jstime(), update cDateTime

cTime
	wrapper class for time
cDateTime
	date time
cJobTime
	job timer class
cBps
	weighted average traffic count

eclib 3.0 Copyright (c) 2017-2023, kipway
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

#define ECTIME_LOCALSTR 0 // "2023/2/20 12:10:32.123"    lcoal datetime
#define ECTIME_ISOSTR   1 // "2023-2-20T12:10:32.123+08:00"  ISO 8601
#define ECTIME_STAMP    2 // 1676866232123     UTC milliseconds since 1970-1-1

namespace ec
{
	/*!
	\param lfiletime [in] 100 nanoseconds from 1601/01/01
	\param pMicrosecond [out] microsecond,1000000 microseconds = 1 second
	\return seconds from 1970/01/01
	*/
	inline int64_t ftime2timet(int64_t lfiletime, int* pMicrosecond)
	{
		if (pMicrosecond)
			*pMicrosecond = (int)((lfiletime % 10000000LL) / 10);
		return (lfiletime / 10000000LL) - 11644473600LL;
	}

	/*!
	\param pMicrosecond [out] microsecond,1000000 microseconds = 1 second
	\return seconds from 1970/01/01
	*/
	inline time_t nstime(int* pMicrosecond)
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

	inline int timezone() // return -12 - 12
	{
#ifdef _WIN32
		return _timezone / 3600;
#else
		return __timezone / 3600;
#endif
	}

	inline bool gmtime_(struct tm* ptm, time_t gmt)
	{
#ifdef _WIN32
		if (gmtime_s(ptm, &gmt))
			return false;
#else
		if (!gmtime_r(&gmt, ptm))
			return false;
#endif
		return true;
	}

	inline bool localtime_(struct tm* ptm, time_t gmt)
	{
#ifdef _WIN32
		if (localtime_s(ptm, &gmt))
			return false;
#else
		if (!localtime_r(&gmt, ptm))
			return false;
#endif
		return true;
	}

	inline bool localtime__(time_t gmt, int* pyear, int* pmon, int* pday, int* phour, int* pmin, int* psec)
	{
		struct tm tml;
#ifdef _WIN32
		if (localtime_s(&tml, &gmt))
			return false;
#else
		if (!localtime_r(&gmt, &tml))
			return false;
#endif	
		* pyear = tml.tm_year + 1900;
		*pmon = tml.tm_mon + 1;
		*pday = tml.tm_mday;
		*phour = tml.tm_hour;
		*pmin = tml.tm_min;
		*psec = tml.tm_sec;
		return true;
	}

	inline time_t mktime_(int nyear, int nmon, int nday, int nhour = 0, int nmin = 0, int nsec = 0)
	{
		struct tm t;
		t.tm_year = nyear - 1900;
		t.tm_mon = nmon - 1;
		t.tm_mday = nday;
		t.tm_hour = nhour;
		t.tm_min = nmin;
		t.tm_sec = nsec;
		t.tm_wday = 0;
		t.tm_yday = 0;
		t.tm_isdst = 0;
		return mktime(&t);
	}

	inline const char* jstime2string(int64_t ltime, char* sout, size_t outsize, int isutc = 1, size_t* pzoutsize = nullptr) //输出jstime，isutc != 0 为0时区
	{
		if (pzoutsize)
			*pzoutsize = 0;
		*sout = 0;
		struct tm t;
		if (isutc) {
			if (!ec::gmtime_(&t, (time_t)(ltime / 1000)))
				return nullptr;
		}
		else {
			if (!ec::localtime_(&t, (time_t)(ltime / 1000)))
				return nullptr;
		}
		size_t n = (size_t)snprintf(sout, outsize, "%d-%02d-%02dT%02d:%02d:%02d.%03d",
			t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
			t.tm_hour, t.tm_min, t.tm_sec, (int)(ltime % 1000));

		if (isutc) {
			if ( n + 1 >= outsize) {
				*sout = 0;
				return nullptr;
			}
			sout[n++] = 'Z';
		}
		else {
			int timez = timezone();
			if (!timez) {
				if ( n + 1 >= outsize) {
					*sout = 0;
					return nullptr;
				}
				sout[n++] = 'Z';
			}
			else {
				if ( n + 6 >= outsize) {
					*sout = 0;
					return nullptr;
				}
				sout[n++] = (timez < 0) ? '+' : '-';
				sout[n++] = abs(timez) / 10 ? '0' + abs(timez) / 10 : '0';
				sout[n++] = '0' + abs(timez) % 10;
				sout[n++] = ':';
				sout[n++] = '0';
				sout[n++] = '0';
			}
		}
		sout[n] = 0;
		if (pzoutsize)
			*pzoutsize = n;
		return sout;
	}

	template<class _Str = std::string>
	bool jstime2string(int64_t ltime, _Str& sout, int utc = 1)// sout use append, ISO 8601,utc != 0 为0时区
	{
		size_t zn = 0;
		char sbuf[48];
		if (!jstime2string(ltime, sbuf, sizeof(sbuf), utc, &zn))
			return false;
		sout.append(sbuf, zn);
		return true;
	}

	inline const char* jstime2localstring(int64_t ltime, char* sout, size_t outsize, size_t* pzoutsize = nullptr)
	{
		return jstime2string(ltime, sout, outsize, 0, pzoutsize);
	}

	template<class _Str = std::string>
	bool jstime2localstring(int64_t ltime, _Str& sout)// sout use append, ISO 8601
	{
		size_t zn = 0;
		char sbuf[48];
		if (!jstime2string(ltime, sbuf, sizeof(sbuf), 0, &zn))
			return false;
		sout.append(sbuf, zn);
		return true;
	}

	/**
	 * @brief Parse the number split by cp
	 * @param s like "12:32:24"
	 * @param cp like ':'
	 * @param pn out buffer
	 * @param nmax maximum number
	 * @return return the number of parsed integers
	*/
	inline int splitint(char* s, char cp, int* pn, int nmax)
	{
		if (!s || !*s)
			return 0;
		int n = 0;
		char* ps = s, * si = s;
		while (*ps && n < nmax) {
			if (*ps == cp) {
				*ps = 0;
				pn[n++] = (*si) ? atoi(si) : 0;
				si = ps + 1;
			}
			++ps;
		}
		if (n < nmax && *si)
			pn[n++] = atoi(si);
		return n;
	}

	/**
	 * @brief parse datetime to timestamp
	 * @param sdatetime:
	 * datetime with timezone ISO 8601
		 2023-2-31T04:32:25.987Z
		 2023-2-31T12:32:25.987+0800
		 2023-2-31T12:32:25.987+08:00
		 2023-2-31T12:32:25+08:00
		 2023-2-31T12:32+08:00
		 2023-2-31T12+08:00
	   local datetime
		 2023-1-31 12:32:25.987
		 2023-1-31 12:32:25
		 2023-1-31 12:32
		 2023-1-31 12
		 2023-1-31
	 * @param zlen, length of sdatetime
	 * @return -1:error; >=0 UTC milliseconds since 1970-1-1
	*/
	inline int64_t string2jstime(const char* sdatetime, size_t zlen)
	{
		int zonexs = -1, iso8601 = 0, ndp = 0;
		char s[48];
		if (!sdatetime || zlen >= sizeof(s))
			return -1;
		memcpy(s, sdatetime, zlen);
		s[zlen] = 0;
		char* ps = s, * pdate = s, * ptime = nullptr, * pms = nullptr, * pzone = nullptr;
		while (*ps) {
			switch (*ps) {
			case 'T':
				if (2 != ndp)
					return -1;
				*ps = 0;
				ptime = ps + 1;
				iso8601 = 1;
				break;
			case '\x20':
				if (!ptime && 2 == ndp && *(ps - 1) >= '0' && *(ps - 1) <= '9') {
					*ps = 0;
					ptime = ps + 1;
				}
				break;
			case '.':
				*ps = 0;
				pms = ps + 1;
				break;
			case '+':
				if (ptime && iso8601) {
					*ps = 0;
					pzone = ps + 1;
				}
				break;
			case '-':
				if (ptime && iso8601) {
					*ps = 0;
					zonexs = 1;
					pzone = ps + 1;
				}
				else
					++ndp;
				break;
			case '/':
				*ps = '-';
				++ndp;
				break;
			}
			++ps;
		}

		int ymd[3] = { 0 }, hms[3] = { 0 }, ms = 0, zsec = 0, n = 0;
		n = splitint(pdate, '-', ymd, 3);//date
		if (n != 3 || ymd[0] < 1970 || ymd[0] > 3000 || ymd[1] < 1 || ymd[1] > 12 || ymd[2] < 1 || ymd[2] > 31)
			return -1;

		n = splitint(ptime, ':', hms, 3);//time
		if (hms[0] < 0 || hms[0] > 23 || hms[1] < 0 || hms[1] > 59 || hms[2] < 0 || hms[2] > 59)
			return -1;

		if (pms)//msec
			ms = atoi(pms);
		if (ms < 0 || ms > 999)
			return -1;

		if (iso8601 && pzone) {// timezone offset
			int zone[2] = { 0 };
			n = splitint(pzone, ':', zone, 2);
			if (n == 1) { // 0800 no ':'
				zone[1] = zone[0] % 100;
				zone[0] = zone[0] / 100;
			}
			if (zone[0] * 100 + zone[1] > 1200 || zone[0] < 0 || zone[1] < 0)
				return -1;
			zsec = 3600 * zone[0] + 60 * zone[1];
			zsec *= zonexs;
		}

		time_t ltime = ec::mktime_(ymd[0], ymd[1], ymd[2], hms[0], hms[1], hms[2]) + zsec;
		if (ptime && iso8601)
			ltime -= timezone() * 3600;
		return (int64_t)(ltime * 1000ll + ms);
	}

	class cTime
	{
	public:
		cTime() : _gmt(0)
			, _year(1970), _mon(1), _day(1)
			, _hour(0), _min(0), _sec(0)
		{
		}
		cTime(time_t gmt)
		{
			SetTime(gmt);
		};
		cTime(int nyear, int nmon, int nday, int nhour = 0, int nmin = 0, int nsec = 0)
		{
			SetTime(nyear, nmon, nday, nhour, nmin, nsec);
		};
		void SetTime(int nyear, int nmon, int nday, int nhour = 0, int nmin = 0, int nsec = 0)
		{
			_gmt = ec::mktime_(nyear, nmon, nday, nhour, nmin, nsec);
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
			if (!ec::localtime__(gmt, &_year, &_mon, &_day, &_hour, &_min, &_sec)) {
				_gmt = -1;
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
		int tostring(char* sout, size_t sizeout, bool hastime = true)
		{
			if (hastime)
				return snprintf(sout, sizeout, "%d/%d/%d %d:%d:%d", _year, _mon, _day, _hour, _min, _sec);
			else
				return snprintf(sout, sizeout, "%d/%d/%d", _year, _mon, _day);
		}
		int tostring_ag(char* sout, size_t sizeout, bool hastime = true)
		{
			if (hastime)
				return snprintf(sout, sizeout, "%d/%02d/%02d %02d:%02d:%02d", _year, _mon, _day, _hour, _min, _sec);
			else
				return snprintf(sout, sizeout, "%d/%02d/%02d", _year, _mon, _day);
		}
		const char* toisostring(char* sout, size_t sizeout, int msec = 0, size_t *pzoutsize = nullptr)// ISO 8601
		{
			if (pzoutsize)
				*pzoutsize = 0;
			*sout = 0;
			if (_gmt < 0)
				return nullptr;
			size_t n = snprintf(sout, sizeout, "%04d-%02d-%02dT%02d:%02d:%02d.%03d",
				_year, _mon, _day, _hour, _min, _sec, msec);
			if ( n + 6 >= sizeout) {
				*sout = 0;
				return nullptr;
			}
			int timez = timezone();
			if (!timez)
				sout[n++] = 'Z';
			else {
				sout[n++] = (timez < 0) ? '+' : '-';
				sout[n++] = abs(timez) / 10 ? '0' + abs(timez) / 10 : '0';
				sout[n++] = '0' + abs(timez) % 10;
				sout[n++] = ':';
				sout[n++] = '0';
				sout[n++] = '0';
			}
			sout[n] = 0;
			if (pzoutsize)
				*pzoutsize = n;
			return sout;
		}

		template<class _Str = std::string>
		bool tojslocalstring(_Str& sout, int msec = 0)// sout use append, ISO 8601
		{
			size_t zn = 0;
			char buf[48];
			if (!toisostring(buf, sizeof(buf), msec, &zn))
				return false;
			sout.append(buf, zn);
			return true;
		}

		int weekday()   // 1=monday,..., 7=sunday, 0:error
		{
			if (_gmt <= 0)
				return 0;
			return ((_gmt / 86400) % 7 + 3) % 7 + 1;// 1970/1/1 is Thursday
		}
	protected:
		time_t _gmt; // GMT time
	public:
		int _year, _mon, _day, _hour, _min, _sec; //local
	};

	/*!
	\brief local date time
	fmt:
	yyyy/mm/dd HH:MM:SS  or yyyy/mm/dd HH:MM:SS.mmm
	yyyy-mm-dd HH:MM:SS  or yyyy-mm-dd HH:MM:SS.mmm

	2023.2.19 update support ISO8601
	*/
	class cDateTime
	{
	public:
		cDateTime() : _nyear(0), _nmon(0), _nday(0), _nhour(0), _nmin(0), _nsec(0), _nmsec(0), _gmt(-1) { };
		cDateTime(const char* s) : cDateTime()
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
		bool parse_n(const char* stime, size_t zlen) {
			_gmt = -1;
			char s[48];
			if (!stime || !zlen || !*stime || zlen >= sizeof(s))
				return false;
			memcpy(s, stime, zlen);
			s[zlen] = 0;

			int64_t ltime = string2jstime(s, zlen);
			if (ltime < 0)
				return false;
			_gmt = (time_t)(ltime / 1000);
			_nmsec = (int)(ltime % 1000);

			if (!ec::localtime__(_gmt, &_nyear, &_nmon, &_nday, &_nhour, &_nmin, &_nsec)) {
				_gmt = -1;
				_nyear = 1970;
				_nmon = 1;
				_nday = 1;
				_nhour = 0;
				_nmin = 0;
				_nsec = 0;
				_nmsec = 0;
				return false;
			}
			return true;
		}
		bool parse(const char* s)
		{
			if (!s || !*s) {
				_gmt = -1;
				return false;
			}
			return parse_n(s, strlen(s));
		}
		int weekday()   // 1=monday,..., 7=sunday, 0:error ; Note that it is GMT
		{
			if (_gmt <= 0)
				return 0;
			return ((_gmt / 86400) % 7 + 3) % 7 + 1; // 1970/1/1 is Thursday
		}
		const char* toisostring(char* sout, size_t sizeout, size_t *pzoutsize = nullptr)// ISO 8601
		{
			if (pzoutsize)
				*pzoutsize = 0;
			*sout = 0;
			if (_gmt < 0)
				return nullptr;
			size_t n = snprintf(sout, sizeout, "%04d-%02d-%02dT%02d:%02d:%02d.%03d",
				_nyear, _nmon, _nday, _nhour, _nmin, _nsec, _nmsec);
			if ( n + 6 >= sizeout) {
				*sout = 0;
				return nullptr;
			}
			int timez = timezone();
			if (!timez)
				sout[n++] = 'Z';
			else {
				sout[n++] = (timez < 0) ? '+' : '-';
				sout[n++] = abs(timez) / 10 ? '0' + abs(timez) / 10 : '0';
				sout[n++] = '0' + abs(timez) % 10;
				sout[n++] = ':';
				sout[n++] = '0';
				sout[n++] = '0';
			}
			sout[n] = 0;
			if (pzoutsize)
				*pzoutsize = n;
			return sout;
		}
		template<class _Str = std::string>
		bool tojslocalstring(_Str& sout)// sout use append, ISO 8601
		{
			size_t zn = 0;
			char buf[48];
			if (!toisostring(buf, sizeof(buf), &zn))
				return false;
			sout.append(buf, zn);
			return true;
		}
	};

#define DAY_TIME(h,m,s) (h * 3600 + m * 60 + s)
#define DAY_HOUR(t)	(t/3600)
#define DAY_MIN(t)  ((t%3600)/60)
#define DAY_SEC(t)	(t%60)

	class cJobTime
	{
	public:
		cJobTime() :_utimeinit(0), _ti_o(0), _ti_add(86400)
		{
		}
		cJobTime(int nsec) :cJobTime()
		{
			SetTime(nsec);
		}
		cJobTime(int hour, int min, int sec) :cJobTime()//hour,min,sec is local time zone
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
