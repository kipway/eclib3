/*!
\file ec_protoc.h
\author	jiangyong
\email  kipway@outlook.com
\update 2022.9.23

classes to encode/decode google protocol buffer,support proto3

eclib 3.0 Copyright (c) 2017-2022, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/

#pragma once

#include <stdint.h>
#include <memory.h>
#include <string>
#include <vector>
#ifdef _MSC_VER
#ifndef bswap_16
#	define bswap_16(x) _byteswap_ushort(x)
#endif
#ifndef bswap_32
#	define bswap_32(x) _byteswap_ulong(x)
#endif
#ifndef bswap_64
#	define bswap_64(x) _byteswap_uint64(x)
#endif
#else
#include <byteswap.h>
#endif
#include <type_traits>
#define pb_varint  0  // Varint
#define pb_fixed64 1  // 64-bit
#define pb_length_delimited  2  // Length-delimited
#define pb_start_group  3 // deprecated not support
#define pb_end_group  4   //deprecated not support
#define pb_fixed32 5  // 32-bit

namespace ec
{
	/*!
	\brief encode and decode
	see https://developers.google.com/protocol-buffers/docs/encoding
	*/
	namespace pb
	{
		//base
		inline bool bigendian()
		{
			union {
				uint32_t u32;
				uint8_t u8;
			} ua;
			ua.u32 = 0x01020304;
			return ua.u8 == 0x01;
		}

		template<typename _Tp>
		bool isutf8(const _Tp* s, size_t size = 0) // return true if s is utf8
		{
			if (!s)
				return true;
			uint8_t c;
			int nb = 0;
			const char* ps = static_cast<const char*>(s);
			const char* pend = ps + (size ? size : strlen(ps));
			while (ps < pend) {
				c = *ps++;
				if (!nb) {
					if (!(c & 0x80))
						continue;
					if (0xc0 == c || 0xc1 == c || c > 0xF4) // RFC 3629
						return false;
					if ((c & 0xFC) == 0xFC) // 1111 1100
						nb = 5;
					else if ((c & 0xF8) == 0xF8) // 1111 1000
						nb = 4;
					else if ((c & 0xF0) == 0xF0) // 1111 0000
						nb = 3;
					else if ((c & 0xE0) == 0xE0) // 1110 1000
						nb = 2;
					else if ((c & 0xC0) == 0xC0) // 1100 0000
						nb = 1;
					else
						return false;
					continue;
				}
				if ((c & 0xC0) != 0x80)
					return false;
				nb--;
			}
			return !nb;
		}
		template<class _Tp
			, class = typename std::enable_if<std::is_integral<_Tp>::value>::type >
			struct zigzag {
			using utype = typename std::conditional < sizeof(_Tp) < 8u, uint32_t, uint64_t > ::type;
			using itype = typename std::conditional < sizeof(_Tp) < 8u, int32_t, int64_t > ::type;
			inline utype  encode(itype v) const {
				return ((v << 1) ^ (v >> (sizeof(_Tp) < 8u ? 31 : 63)));
			}
			inline  itype decode(utype v) const {
				return (itype)((v >> 1) ^ (-(itype)(v & 1)));
			}
		};

		template<class _Tp
			, class = typename std::enable_if<std::is_integral<_Tp>::value&& std::is_unsigned<_Tp>::value> ::type >
			bool get_varint(const uint8_t*& pd, int& len, _Tp& out)  //get Varint (Base 128 Varints)
		{
			if (len <= 0)
				return false;
			int nbit = 0;
			out = 0;
			do {
				out |= (*pd & (_Tp)0x7F) << nbit;
				if (!(*pd & 0x80)) {
					pd++;
					len--;
					return true;
				}
				nbit += 7;
				pd++;
				len--;
			} while (len > 0 && nbit < ((int)sizeof(_Tp) * 8));
			return false;
		}

		template<class _Tp
			, class = typename std::enable_if<std::is_arithmetic<_Tp>::value && (sizeof(_Tp) == 4u || sizeof(_Tp) == 8u)>::type >
			bool get_fixed(const uint8_t*& pd, int& len, _Tp& out)  //get 32-bit and 64-bit (fixed32,sfixed32,fixed64,sfixed64,float,double)
		{
			if (len < (int)sizeof(out))
				return false;
			memcpy(&out, pd, sizeof(out));
			if (bigendian()) {
				if (4 == sizeof(out))
					*((uint32_t*)&out) = bswap_32(*((uint32_t*)&out));
				else
					*((uint64_t*)&out) = bswap_64(*((uint64_t*)&out));
			}
			pd += sizeof(out);
			len -= sizeof(out);
			return true;
		}

		template<typename _INT>
		bool get_key(const uint8_t*& pd, int& len, _INT& field_number, _INT& wire_type) //get field_number and  wire_type
		{
			uint32_t key;
			if (!get_varint(pd, len, key))
				return false;
			wire_type = (_INT)(key & 0x07);
			field_number = (_INT)(key >> 3);
			return true;
		}

		template<typename _TpOutlen>
		bool get_length_delimited(const uint8_t*& pd, int& len, const uint8_t*& pout, _TpOutlen& outlen)  //get string, bytes,no copy
		{
			uint32_t ul = 0;
			if (!get_varint(pd, len, ul))
				return false;
			if (len < (int)ul)
				return false;
			pout = pd;
			pd += ul;
			len -= ul;
			outlen = (_TpOutlen)ul;
			return true;
		}

		template<class _Tp
			, class = typename std::enable_if<std::is_integral<_Tp>::value>::type >
			bool get_varpacket(const uint8_t*& pd, int& len, _Tp& out, bool iszigzag = false)
		{
			if (!std::is_arithmetic<_Tp>::value)
				return false;
			using utype = typename std::conditional < sizeof(_Tp) < 8u, uint32_t, uint64_t > ::type;
			utype v;
			if (!get_varint(pd, len, v))
				return false;
			if (iszigzag)
				out = (_Tp)zigzag<_Tp>().decode(v);
			else
				out = (_Tp)v;
			return true;
		}

		template<class _Tp
			, class _Out
			, class = typename std::enable_if<std::is_integral<_Tp>::value&& std::is_unsigned<_Tp>::value > ::type >
			bool out_varint(_Tp v, _Out* pout) //out Varint (Base 128 Varints)
		{
			int nbit = 0;
			uint8_t ou = 0;
			using ctype = typename _Out::value_type;
			do {
				ou = (v >> nbit) & 0x7F;
				nbit += 7;
				if (v >> nbit) {
					ou |= 0x80;
					pout->push_back(ou);
				}
				else {
					pout->push_back(ou);
					break;
				}
			} while (nbit + 7 < ((int)sizeof(_Tp) * 8));
			if (v >> nbit)
				pout->push_back((ctype)(v >> nbit));
			return true;
		}

		template<class _Tp
			, class _Out
			, class = typename std::enable_if < std::is_arithmetic<_Tp>::value && sizeof(_Tp) == 4u> ::type >
			bool out_fixed32(_Tp v, _Out* pout)  //out 32-bit (fixed32,sfixed32,float)
		{
			if (bigendian()) {
				uint32_t* p = (uint32_t*)&v;
				*p = bswap_32(*p);
			}
			using ctype = typename _Out::value_type;
			pout->append((ctype*)&v, sizeof(v));
			return true;
		}
		template<class _Tp
			, class _Out
			, class = typename std::enable_if<std::is_arithmetic<_Tp>::value && sizeof(_Tp) == 8u >::type>
			bool out_fixed64(_Tp v, _Out* pout)  //out 64-bit (fixed64,sfixed64,double)
		{
			if (bigendian()) {
				uint64_t* p = (uint64_t*)&v;
				*p = bswap_64(*p);
			}
			using ctype = typename _Out::value_type;
			pout->append((ctype*)&v, sizeof(v));
			return true;
		}

		template<class _Out>
		bool out_key(uint32_t field_number, uint32_t wire_type, _Out* pout) // out field_number and  wire_type
		{
			uint32_t v = (field_number << 3) | (wire_type & 0x07);
			return out_varint(v, pout);
		}

		template<class _Out>
		bool out_length_delimited(const uint8_t* pd, size_t len, _Out* pout) // out string, bytes
		{
			if (!out_varint(len, pout))
				return false;
			using ctype = typename _Out::value_type;
			pout->append((ctype*)pd, len);
			return true;
		}

		template<typename _tpwire_type>
		bool jump_over(const uint8_t*& pd, int& len, _tpwire_type wire_type) //jump over unkown field_number
		{
			size_t datalen = 0;
			uint64_t v = 0;
			switch ((int)(wire_type)) {
			case pb_varint:
				return get_varint(pd, len, v);
				break;
			case pb_fixed64:
				if (len < 8)
					return false;
				pd += 8;
				len -= 8;
				break;
			case pb_length_delimited:
				if (!get_varint(pd, len, datalen))
					return false;
				if (len < (int)datalen)
					return false;
				pd += datalen;
				len -= (int)datalen;
				break;
			case pb_fixed32:
				if (len < 4)
					return false;
				pd += 4;
				len -= 4;
				break;
			default:
				return false;// unkown wire_type
			}
			return true;
		}

		// size
		template<class _Tp, class = typename std::enable_if<std::is_integral<_Tp>::value&& std::is_unsigned<_Tp>::value > ::type >
		size_t size_varint(_Tp v) //size Varint (Base 128 Varints)
		{
			int nbit = 0;
			size_t zret = 0;
			do {
				nbit += 7;
				zret++;
				if (!(v >> nbit))
					break;
			} while (nbit + 7 < ((int)sizeof(_Tp) * 8));
			if (v >> nbit)
				zret++;
			return zret;
		}

		inline size_t size_key(uint32_t field_number, uint32_t wire_type)
		{
			return size_varint((field_number << 3) | (wire_type & 0x07));
		}

		inline size_t size_length_delimited(size_t len) // size string, bytes
		{
			return len ? size_varint(len) + len : 0;
		}

		template<class _Tp
			, class = typename std::enable_if<std::is_integral<_Tp>::value>::type >
			size_t size_var(int id, _Tp v, bool iszigzag = false)
		{
			using utype = typename std::conditional < sizeof(_Tp) < 8u, uint32_t, uint64_t > ::type;
			if (!v)
				return 0; //default 0
			if (iszigzag)
				return size_key(id, pb_varint) + size_varint(zigzag<_Tp>().encode(v));
			return size_key(id, pb_varint) + size_varint((utype)v);
		}

		template<class _Tp
			, class = typename std::enable_if<std::is_arithmetic<_Tp>::value && (sizeof(_Tp) == 4u || sizeof(_Tp) == 8u)>::type >
			size_t size_fixed(uint32_t id, _Tp v)
		{
			if (4 == (int)sizeof(v))
				return size_key(id, 5) + 4;
			if (8 == (int)sizeof(v))
				return size_key(id, 1) + 8;
			return 0;
		}

		inline size_t size_str(uint32_t id, const char* s)
		{
			return  (!s || !*s) ? 0 : size_key(id, pb_length_delimited) + size_length_delimited(strlen(s));
		}

		inline size_t size_str(uint32_t id, const char* s, size_t size)
		{
			return (!s || !*s || !size) ? 0 : size_key(id, pb_length_delimited) + size_length_delimited(size);
		}		

		template<class _Str>
		size_t size_string(uint32_t id, const _Str &s)
		{
			return  s.empty() ? 0 : size_key(id, pb_length_delimited) + size_length_delimited(s.size());
		}

		inline size_t size_cls(uint32_t id, const void* pcls, size_t size)
		{
			return (!pcls || !size) ? 0 : size_key(id, pb_length_delimited) + size_length_delimited(size);
		}

		inline size_t size_cls(uint32_t id, size_t size)
		{
			return size ? size_key(id, pb_length_delimited) + size_length_delimited(size) : 0;
		}

		template<class _Tp
			, class = typename std::enable_if<std::is_integral<_Tp>::value>::type >
			size_t size_varpacket(_Tp* pdata, size_t items, bool iszigzag = false)
		{
			if (!items)
				return 0;
			size_t zn = 0;
			for (auto i = 0u; i < items; i++) {
				if (iszigzag)
					zn += size_varint(zigzag<_Tp>().encode(pdata[i]));
				else {
					using utype = typename std::conditional < sizeof(_Tp) < 8u, uint32_t, uint64_t > ::type;
					zn += size_varint((utype)pdata[i]);
				}
			}
			return zn;
		}

		template<class _Tp
			, class = typename std::enable_if<std::is_integral<_Tp>::value>::type >
			size_t size_varpacket(uint32_t id, _Tp* pdata, size_t items, bool iszigzag = false)
		{
			if (!items)
				return 0;
			size_t zn = 0;
			for (auto i = 0u; i < items; i++) {
				if (iszigzag)
					zn += size_varint(zigzag<_Tp>().encode(pdata[i]));
				else {
					using utype = typename std::conditional < sizeof(_Tp) < 8u, uint32_t, uint64_t > ::type;
					zn += size_varint((utype)pdata[i]);
				}
			}
			return size_key(id, pb_length_delimited) + size_varint(zn) + zn;
		}

		template<class _Tp,
			class = typename std::enable_if<std::is_arithmetic<_Tp>::value && sizeof(_Tp) == 4u>::type >
			size_t size_fix32packet(uint32_t id, _Tp* pdata, size_t items)
		{
			if (!items)
				return 0;
			return size_key(id, pb_length_delimited) + size_varint(sizeof(_Tp) * items) + sizeof(_Tp) * items;
		}

		template<class _Tp,
			class = typename std::enable_if<std::is_arithmetic<_Tp>::value && sizeof(_Tp) == 8u>::type >
			size_t size_fix64packet(uint32_t id, _Tp* pdata, size_t items)
		{
			if (!items)
				return 0;
			return size_key(id, pb_length_delimited) + size_varint(sizeof(_Tp) * items) + sizeof(_Tp) * items;
		}

		template<class _OBJ>
		size_t size_object(int objid, _OBJ& obj)
		{
			size_t zlen = obj.size_content();
			return zlen ? size_key(objid, pb_length_delimited) + size_length_delimited(zlen) : 0;
		}

		template<class _OBJSETS>
		size_t size_objsets(int objid, _OBJSETS& vs)
		{
			size_t zu = 0;
			for (auto& i : vs) {
				zu += size_object(objid, i);
			}
			return zu;
		}

		// out
		template<class _Tp
			, class _Out
			, class = typename std::enable_if<std::is_integral<_Tp>::value>::type>
			bool out_var(_Out* po, int id, _Tp v, bool iszigzag = false)
		{
			if (!v) //default 0
				return true;
			if (iszigzag)
				return out_key(id, pb_varint, po) && out_varint(zigzag<_Tp>().encode(v), po);
			using utype = typename std::conditional < sizeof(_Tp) < 8u, uint32_t, uint64_t > ::type;
			return out_key(id, pb_varint, po) && out_varint((utype)v, po);
		}

		template<class _Tp
			, class _Out
			, class = typename std::enable_if<std::is_arithmetic<_Tp>::value && sizeof(_Tp) == 4u>::type >
			inline bool out_fixed32(_Out* po, int id, _Tp v)
		{
			return out_key(id, pb_fixed32, po) && out_fixed32(v, po);
		}

		template<class _Tp
			, class _Out
			, class = typename std::enable_if<std::is_arithmetic<_Tp>::value && sizeof(_Tp) == 8u>::type >
			inline bool out_fixed64(_Out* po, int id, _Tp v)
		{
			return out_key(id, pb_fixed64, po) && out_fixed64(v, po);
		}

		template<class _Tp, class _Out,
			class = typename std::enable_if<std::is_arithmetic<_Tp>::value && sizeof(_Tp) == 4u>::type>
			bool out_fix32packet(uint32_t id, _Tp* pdata, size_t items, _Out* pout)
		{
			if (!items)
				return true;
			size_t zbody = sizeof(_Tp) * items;
			out_key(id, pb_length_delimited, pout);
			out_varint(zbody, pout);
			for (auto i = 0u; i < items; i++)
				out_fixed32(pdata[i], pout);
			return true;
		}

		template<class _Tp, class _Out,
			class = typename std::enable_if<std::is_arithmetic<_Tp>::value && sizeof(_Tp) == 8u>::type>
			bool out_fix64packet(uint32_t id, _Tp* pdata, size_t items, _Out* pout)
		{
			if (!items)
				return true;
			size_t zbody = sizeof(_Tp) * items;
			out_key(id, pb_length_delimited, pout);
			out_varint(zbody, pout);
			for (auto i = 0u; i < items; i++)
				out_fixed64(pdata[i], pout);
			return true;
		}

		template<class _Tp, class _Out
			, class = typename std::enable_if<std::is_integral<_Tp>::value>::type >
			bool out_varpacket(uint32_t id, _Tp* pdata, size_t items, _Out* pout, bool iszigzag = false)
		{
			using utype = typename std::conditional < sizeof(_Tp) < 8u, uint32_t, uint64_t > ::type;
			if (!items)
				return true;
			size_t zbody = size_varpacket(pdata, items, iszigzag);
			out_key(id, pb_length_delimited, pout);
			out_varint(zbody, pout);
			for (auto i = 0u; i < items; i++) {
				if (iszigzag) {
					zigzag<_Tp> zg;
					if (!out_varint(zg.encode(pdata[i]), pout))
						return false;
				}
				else {
					if (!out_varint((utype)pdata[i], pout))
						return false;
				}
			}
			return true;
		}

		template<class _Out>
		bool out_str(_Out* po, int id, const char* s)
		{
			if (!s || !*s)
				return true;
			if (!isutf8(s)) {
				return false;
			}
			return out_key(id, pb_length_delimited, po) && out_length_delimited((uint8_t*)s, strlen(s), po);
		}

		template<class _Out>
		bool out_str(_Out* po, int id, const char* s, size_t size)
		{
			if (!s || !*s || !size)
				return true;
			if (!isutf8(s, size)) {
				return false;
			}
			return out_key(id, pb_length_delimited, po) && out_length_delimited((uint8_t*)s, size, po);
		}

		template<class _Out, class _Str>
		bool out_string(_Out* po, int id, const _Str& s)
		{
			if (s.empty())
				return true;
			if (!isutf8(s.data(),s.size())) {
				return false;
			}
			return out_key(id, pb_length_delimited, po) && out_length_delimited((uint8_t*)s.data(), s.size(), po);
		}

		template<class _Out>
		bool out_cls(_Out* po, int id, const void* pcls, size_t size)
		{
			if (!pcls || !size)
				return true;
			return out_key(id, pb_length_delimited, po) && out_length_delimited((uint8_t*)pcls, size, po);
		}

		template<class _OBJ>
		bool parse(const void* pmsg, size_t msgsize, _OBJ& obj)
		{
			obj.clear();
			if (!pmsg || !msgsize)
				return true;
			uint32_t field_number = 0, wire_type = 0;
			int len = (int)msgsize;
			const uint8_t* pd = (const uint8_t*)pmsg;
			uint32_t u32 = 0;
			uint64_t u64 = 0;
			const uint8_t* pcls = nullptr;
			size_t  zlen = 0;
			do {
				if (!get_key(pd, len, field_number, wire_type))
					return false;
				switch (wire_type) {
				case pb_varint:
					if (!get_varint(pd, len, u64))
						return false;
					obj.on_var(field_number, u64);
					break;
				case pb_length_delimited:
					if (!get_length_delimited(pd, len, pcls, zlen))
						return false;
					obj.on_cls(field_number, pcls, zlen);
					break;
				case pb_fixed32:
					if (!get_fixed(pd, len, u32))
						return false;
					obj.on_fixed(field_number, &u32, sizeof(u32));
					break;
				case pb_fixed64:
					if (!get_fixed(pd, len, u64))
						return false;
					obj.on_fixed(field_number, &u64, sizeof(u64));
					break;
				default:
					if (!jump_over(pd, len, wire_type))
						return false;
					break;
				}
			} while (len > 0);
			return 0 == len;
		}

		template<class _Out, class _OBJ>
		bool serialize(_Out* pout, _OBJ& obj, int objid = -1)
		{
			if (objid == -1)
				return obj.out_content(pout);
			size_t sizebody = obj.size_content();
			if (!sizebody)
				return true;
			return out_key(objid, pb_length_delimited, pout) && out_varint(sizebody, pout)
				&& obj.out_content(pout);
		}

		template<class _Out, class _OBJ>
		bool out_object(_Out* pout, int objid, _OBJ& obj)
		{
			return serialize(pout, obj, objid);
		}

		template<class _Out, class _OBJSETS>
		bool out_objsets(_Out* pout, int objid, _OBJSETS& vs)
		{
			for (auto& i : vs) {
				if (!serialize(pout, i, objid))
					return false;
			}
			return true;
		}

		template<class _Out, class _OBJSETS>
		bool out_stringsets(_Out* pout, int objid, _OBJSETS& vs)
		{
			for (const auto& i : vs) {
				if (!out_str(pout, objid, i.data(), i.size()))
					return false;
			}
			return true;
		}
	}//pb
}//ec

#define CASE_U16(id,var) case id: var = static_cast<uint16_t>(val);break;
#define CASE_I32(id,var) case id: var = static_cast<int32_t>(val);break;
#define CASE_U32(id,var) case id: var = static_cast<uint32_t>(val);break;
#define CASE_I64(id,var) case id: var = static_cast<int64_t>(val);break;
#define CASE_U64(id,var) case id: var = static_cast<uint64_t>(val);break;
#define CASE_STR(id,var) case id: var.assign((const char*)pdata, size);break;
#define CASE_S32(id,var) case id: var = static_cast<int32_t>(ec::pb::zigzag<int32_t>().decode((uint32_t)val));break;
#define CASE_S64(id,var) case id: var = static_cast<int64_t>(ec::pb::zigzag<int64_t>().decode(val));break;
#define CASE_FIXF32(id,var) case id: var = *((const float*)pval);break;
#define CASE_FIXF64(id,var) case id: var = size == 8u ? *((const double*)pval) : *((const float*)pval);break;

#define CASE_VI32(id,var) case id: {\
	const uint8_t* pd = (const uint8_t*)pdata;\
	int len = (int)size;\
	int32_t nv;\
	while (len > 0 && ec::pb::get_varpacket(pd, len, nv))\
		var.push_back(nv);\
}break;

#define CASE_CSTR(id,var) case id: {\
	size_t zlen = size < sizeof(var) ? size : sizeof(var) - 1u;\
	memcpy(var, pdata, zlen);\
	var[zlen] = 0;\
}break;

#define CASE_OBJ(id,obj) case id: ec::pb::parse(pdata, size, obj);break;

/*
usgae:

class test1
{
public:
	enum {
		id_no = 1,  // int32 no =1;
		id_f32 = 2, // float f32= 2;
		id_f64 = 3, // double f64 = 3;
		id_name = 4,//string name = 4;
		id_s32 = 5, // sint32 = 5;
		id_s64 = 6, // sint64 = 6;
		id_vi32 = 7 // repeated int32 vi32 = 7 [packed=true];
	};

	int32_t _no;
	float _f32;
	int32_t _s32;

	std::string _name;
	double _f64;
	int64_t _s64;

	std::vector<int32_t> _vi32;
public:
	void clear()
	{
		_no = 0;
		_f32 = 0;
		_s32 = 0;
		_f64 = 0;
		_s64 = 0;
		_name.clear();
		_vi32.clear();
	}
	void on_var(uint32_t field_number, uint64_t val)
	{
		switch (field_number)
		{
			CASE_I32(id_no, _no);
			CASE_S32(id_s32, _s32);
			CASE_S64(id_s64, _s64);
		}
	}

	void on_fixed(uint32_t field_number, const void* pval, size_t size)
	{
		switch (field_number)
		{
			CASE_FIXF32(id_f32, _f32);
			CASE_FIXF64(id_f64, _f64);
		}
	}

	void on_cls(uint32_t field_number, const void* pdata, size_t size)
	{
		switch (field_number)
		{
			CASE_STR(id_name, _name);
			CASE_VI32(id_vi32, _vi32);
		}
	}

	template<class _Out>
	bool out_content(_Out* pout)
	{
		ec::pb::out_var(pout, id_no, _no);
		ec::pb::out_fixed32(pout, id_f32, _f32);
		ec::pb::out_fixed64(pout, id_f64, _f64);
		ec::pb::out_string(pout, id_name, _name);
		ec::pb::out_var(pout, id_s32, _s32, true);
		ec::pb::out_var(pout, id_s64, _s64, true);
		ec::pb::out_varpacket(id_vi32, _vi32.data(), _vi32.size(), pout);
		return true;
	}

	size_t size_content()
	{
		return ec::pb::size_var(id_no, _no)
			+ ec::pb::size_fixed(id_f32, _f32)
			+ ec::pb::size_fixed(id_f64, _f64)
			+ ec::pb::size_string(id_name, _name)
			+ ec::pb::size_var(id_s32, _s32, true)
			+ ec::pb::size_var(id_s64, _s64, true)
			+ ec::pb::size_varpacket(id_vi32, _vi32.data(), _vi32.size())
			;
	}

	void prt()
	{
		printf("no  : %d\n", _no);
		printf("f32 : %f\n", _f32);
		printf("f64 : %f\n", _f64);
		printf("name: %s\n", _name.c_str());
		printf("s32 : %d\n", _s32);
		printf("s64 : %jd\n", _s64);
		printf("vi32:\n");
		for (auto& i : _vi32)
			printf("%d, ", i);
	}
};

void testpb()
{
	test1 obj;
	obj._no = -10;
	obj._f32 = -12.5;
	obj._f64 = 125.75;
	obj._name = "test name string";
	obj._s32 = -100;
	obj._s64 = -200000;
	obj._vi32.push_back(101);
	obj._vi32.push_back(102);
	obj._vi32.push_back(103);
	std::string sout;
	ec::pb::serialize(&sout, obj);
	obj.prt();
	printf("size=%zu\n\n",sout.size());

	obj.clear();

	test1 obj2;
	if (!ec::pb::parse(sout.data(), sout.size(),obj2)) {
		printf("parse failed!\n");
		return;
	}
	printf("obj2 parse success.\n");
	obj2.prt();
}

*/