/*!
\file ec_mutex.h
\author	jiangyong
\email  kipway@outlook.com
\update 2020.8.29

class unique_lock;
class spinlock;
class unique_spinlock

eclib 3.0 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

简介:
互拆体，排他锁类，unique_lock是标准库的简化版，增加了自旋锁spinlock
*/
#pragma once

#include <mutex>
#ifdef _WIN32
#include <Windows.h>
#else
#include <pthread.h>
#endif
namespace ec {
	class unique_lock {
	private:
		std::mutex *_pmutex;
	public:
		unique_lock(const unique_lock&) = delete;
		unique_lock& operator = (const unique_lock&) = delete;

		unique_lock(std::mutex *pmutex) : _pmutex(pmutex)
		{
			if (_pmutex)
				_pmutex->lock();
		}
		~unique_lock()
		{
			if (_pmutex)
				_pmutex->unlock();
		}
	};

#ifdef _WIN32
	class spinlock
	{
	public:
		spinlock(const spinlock&) = delete;
		spinlock& operator = (const spinlock&) = delete;

		spinlock()
		{
			InitializeCriticalSectionAndSpinCount(&_v, UINT16_MAX);
		}
		~spinlock()
		{
			DeleteCriticalSection(&_v);
		}
	public:
		void lock() { EnterCriticalSection(&_v); }
		void unlock() { LeaveCriticalSection(&_v); }
	private:
		CRITICAL_SECTION _v;
	};
#else
	class spinlock {
	public:
		spinlock(const spinlock&) = delete;
		spinlock& operator = (const spinlock&) = delete;

		spinlock()
		{
			pthread_spin_init(&_v, PTHREAD_PROCESS_PRIVATE);
		}
		~spinlock()
		{
			pthread_spin_destroy(&_v);
		}
	public:
		void lock()
		{
			pthread_spin_lock(&_v);
		}
		void unlock()
		{
			pthread_spin_unlock(&_v);
		}
	private:
		pthread_spinlock_t  _v;
	};
#endif

	class unique_spinlock {
	private:
		spinlock *_plck;
	public:
		unique_spinlock(const unique_spinlock&) = delete;
		unique_spinlock& operator = (const unique_spinlock&) = delete;

		unique_spinlock(spinlock *plck) : _plck(plck)
		{
			if (_plck)
				_plck->lock();
		}
		~unique_spinlock()
		{
			if (_plck)
				_plck->unlock();
		}
	};
}