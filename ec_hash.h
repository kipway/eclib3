/*!
\file ec_hash.h
\author	jiangyong
\email  kipway@outlook.com
\update 2020.9.6

hash class for hashmap

eclib 3.0 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/

#pragma once
namespace ec
{
	template<class _Kty> // hash class
	struct hash
	{
		size_t operator()(_Kty key)
		{
			if (sizeof(size_t) == 8)
				return static_cast<size_t>(static_cast<size_t>(key) * 11400714819323198485ULL);
			return (((size_t)key) * 2654435769U);
		}
	};

	template<>
	struct hash<const char*>
	{
		size_t  operator()(const char*  key)
		{
			register unsigned int uHash = 0;
			while (char ch = *key++)
				uHash = uHash * 31 + ch;
			return uHash;
		}
	};

	template<>
	struct hash<char*>
	{
		size_t operator()(char*  key)
		{
			register unsigned int uHash = 0;
			while (char ch = *key++)
				uHash = uHash * 31 + ch;
			return uHash;
		}
	};

	struct hash_istr {
		size_t  operator()(const char*  key)
		{
			register unsigned int uHash = 0;
			while (char ch = *key++)
				uHash = uHash * 31 + tolower(ch);
			return uHash;
		}
	};
}
