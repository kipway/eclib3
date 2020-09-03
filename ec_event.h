﻿/*!
\file ec_event.h
\author	jiangyong
\email  kipway@outlook.com
\update 2020.8.29

eclib event use c++11 condition_variable

class cEvent;

eclib 3.0 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

简介：
使用C++11的条件变量实现的事件信号。用于基于事件触发的线程使用。
*/
#pragma once
#include <mutex>
#include <condition_variable>
namespace ec {
	class cEvent
	{
	public:
		cEvent(const cEvent&) = delete;
		cEvent& operator = (const cEvent&) = delete;
		cEvent(bool bInitiallyOwn = false, bool bManualReset = false) :_nready(bInitiallyOwn), _bManualReset(bManualReset)
		{
		}
		bool SetEvent()
		{			
			_mtx.lock();
			_nready = true;
			_mtx.unlock();
			_cv.notify_one();
			return true;
		};
		bool ResetEvent()
		{			
			_mtx.lock();
			_nready = false;
			_mtx.unlock();
			return true;
		}
		bool Wait(int milliseconds)
		{
			std::unique_lock<std::mutex> lck(_mtx);
			if (!_nready)
				_cv.wait_for(lck, std::chrono::milliseconds(milliseconds));
			if (_nready)
			{
				if (!_bManualReset)
					_nready = false;
				return true;
			}
			return false;
		}
	protected:
		bool _nready;
		bool _bManualReset;
		std::mutex _mtx;
		std::condition_variable _cv;
	};
}

