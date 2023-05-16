/*!
\file ec_vector.h
\author	jiangyong
\email  kipway@outlook.com
\update 2023.5.15

std::vector use ec::std_allocator

eclib 3.0 Copyright (c) 2017-2023, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/
#pragma once
#include <vector>
#include "ec_alloctor.h"
namespace ec
{
	template<typename _Tp>
	class vector : public std::vector<_Tp, ec::std_allocator<_Tp>> {
	};
}
