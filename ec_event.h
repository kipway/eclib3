/*!
\file ec_event.h
\author	jiangyong
\email  kipway@outlook.com
\update 2022.5.3

cEvent
	event class use c++11 condition_variable

eclib 3.0 Copyright (c) 2017-2022, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
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
		bool SetEvent(bool ball = false)
		{
			std::unique_lock<std::mutex> lck(_mtx);
			_nready = true;
			if (ball)
				_cv.notify_all();
			else
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

