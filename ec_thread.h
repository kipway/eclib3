/*
\file ec_thread.h
\author	jiangyong
\email  kipway@outlook.com
\update 2023.5.13
2023.5.13 support self stack size at linux
class ec::thread

a handle template class

eclib 3.0 Copyright (c) 2017-2023, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/
#pragma once
#include <thread>
namespace ec
{
#ifndef _WIN32
#include <pthread.h>
#include <unistd.h>
#ifndef ARM_STACK_SIZE
#ifdef _MEM_TINY
#	define ARM_STACK_SIZE 0x100000 // 1MB
#else
#	define ARM_STACK_SIZE 0x200000 // 2MB
#endif
#endif
	class thread 
	{
	private:
		pthread_t   m_tid = 0;
		int _runonce, _stopflag, _runst; //终止线程标志,非0表示需要终止

		static void* ThreadProcess(void* pargs)
		{
			thread* pt = (thread*)pargs;
			pt->mainloop();
			return nullptr;
		}
		void	mainloop() {
			_runst = 1;
			do {
				threadRuntime();
			} while (!_stopflag && !_runonce);
			_stopflag = 0;
			_runst = 0;
		}
	public:
		thread(int runonce = 0) :m_tid(0), _runonce(runonce), _stopflag(0), _runst(0) {
		}
		virtual ~thread() {
			threadStop();
		}
		bool threadStart(int runonce = 0)
		{
			_runonce = runonce;
			pthread_attr_t attr;
			pthread_attr_init(&attr);
			pthread_attr_setstacksize(&attr, ARM_STACK_SIZE);
			pthread_create(&m_tid, &attr, ThreadProcess, this);
			pthread_attr_destroy(&attr);
			return m_tid != 0;
		}
		void threadStop()
		{
			if (!m_tid)
				return;
			_stopflag = 1;
			while (_runst)
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			pthread_join(m_tid, nullptr);
			m_tid = 0;
		}
		bool threadKilling()
		{
			return _stopflag;
		}

		bool threadRunning() {
			return _runst != 0;
		}
	protected:
		virtual void threadRuntime() = 0;
	};
#else
	class thread
	{
	public:
		thread( int runonce = 0) :_pthread(nullptr), _runonce(runonce), _stopflag(0), _runst(0) {
		}
		virtual ~thread() {
			threadStop();
		}
		bool threadStart(int runonce = 0)
		{
			_runonce = runonce;
			_pthread = new std::thread([&]() {
				_runst = 1;
				do {
					threadRuntime();
				} while (!_stopflag && !_runonce);
				_stopflag = 0;
				_runst = 0;
				});
			return _pthread != nullptr;
		}
		void threadStop()
		{
			if (!_pthread)
				return;
			_stopflag = 1;
			_pthread->join();
			delete _pthread;
			_pthread = nullptr;
		}
		bool threadKilling()
		{
			return _stopflag;
		}

		bool threadRunning() {
			return _runst != 0;
		}
	protected:
		virtual void threadRuntime() = 0;
	private:
		std::thread* _pthread;
		int _runonce, _stopflag, _runst; //终止线程标志,非0表示需要终止
	};
#endif
}// namespace ec
