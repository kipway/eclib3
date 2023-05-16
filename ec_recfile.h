/*!
\file ec_recfile.h
\author	kipway@outlook.com
\update 2021.9.30

eclib file class

class recfile;

eclib3 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway/eclib3

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/

#pragma once
#include "ec_string.h"
#include "ec_crc.h"
#include "ec_file.h"

#define RFL_MINRECS 1024
#ifndef RFL_MAXRECS
#define RFL_MAXRECS (1024 * 1024 * 16)
#endif

#define RFL_MINBLKSIZE 256

#ifndef RFL_MAXBLKSIZE
#define RFL_MAXBLKSIZE (1024 * 16)
#endif

#define RFL_ERR_POS   0xffffffff
#define RFL_ADD_POS   0xffffffff
#define RFL_HEADSIZE  512

#define RFLE_NOTEXIST  1 // file not exist
#define RFLE_FILEIO    2 // file IO error
#define RFLE_MEMORY    3 // memory error
#define RFLE_CRC32     4 // file HEAD CRC32 error
#define RFLE_APPFLAG   5 // application flag error
#define RFLE_SYSFLAG   6 // system flag error
#define RFLE_PARAM     7 // parameter error
#define RFLE_PHTABLE   8 // page table error
#define RFLE_RECFULL   9 // record full
#define RFLE_RECHEADERR   10 // record head error

#define RFILE_SYS_FLAG "ec_record_file"
#define RFILE_REC_HEAD_SZIE  8
namespace ec
{
	/**
	 \class	recfile

	 \brief	A record file.
	 */
	class record_file : public File
	{
	public:
		static const char* geterrinfo(int nerr)
		{
			struct t_errmsg {
				int n;
				const char* s;
			};

			t_errmsg msg[] = {
				{0,"success"}
				,{RFLE_NOTEXIST,"file not exist"}
				,{RFLE_FILEIO,"file IO error"}
				,{RFLE_MEMORY,"memory error"}
				,{RFLE_CRC32,"file CRC32 check error"}
				,{RFLE_APPFLAG,"application flag error"}
				,{RFLE_SYSFLAG,"system flag error"}
				,{RFLE_PARAM,"parameter error"}
				,{RFLE_PHTABLE,"page table error"}
				,{RFLE_RECFULL,"record full"}
			};
			for (auto i = 0u; i < sizeof(msg) / sizeof(t_errmsg); i++) {
				if (msg[i].n == nerr)
					return msg[i].s;
			}
			return "undefine error";
		}

		record_file()
		{
			_maxrecs = 1024 * 1024;
			_sizeblk = 1024;
			_pgtab = nullptr;
			_nextpgpos = 0;
			_filesize = 0;
			_errcode = 0;
			memset(_tmpblk, 0, sizeof(_tmpblk));
			flush_flag = 0;
		}
		virtual ~record_file() {
			if (_pgtab) {
				free(_pgtab);
				_pgtab = nullptr;
			}
		}

		struct t_head {
			char sysflag[16]; //RFILE_SYS_FLAG
			char appflag[64]; //
			char res[160];
			uint32_t maxrecs;
			uint32_t sizeblk;
			uint32_t crc32; //
			uint32_t ures;
		}; //sizeof() = 256

		inline uint32_t sizeblk() {
			return _sizeblk;
		}
		inline uint32_t maxrecs() {
			return _maxrecs;
		}
	private:
		uint32_t _maxrecs;// MAX records, 1024 - 1024 * 1024 * 4
		uint32_t _sizeblk;  // record block size, 512-4096
		uint8_t *_pgtab; //page alloc table
		uint32_t _nextpgpos; //next page position
		long long _filesize; // file size
		uint8_t _tmpblk[RFL_MAXBLKSIZE]; //temp block buffer
		int   _errcode; //error code
		int   flush_flag; // Non-zero: need flush;
	private:
		uint32_t sizepagetable()
		{
			uint32_t z = _maxrecs / 8u;
			if (z % RFL_HEADSIZE)
				z += RFL_HEADSIZE - z % RFL_HEADSIZE;
			return z;
		}
	private:
		uint32_t alloc_page()
		{
			_errcode = 0;
			uint32_t i = _nextpgpos / 8u, umaxpg = sizepagetable();
			for (; i < umaxpg; i++) {
				if (0xFF == _pgtab[i])
					continue;
				for (uint32_t j = 0; j < 8u; j++) {
					if (!((0x01 << j) & _pgtab[i])) {
						uint8_t u = _pgtab[i] | (0x01 << j);
						unique_filelock flck(this, RFL_HEADSIZE + i, 1, true);
						if (1 != WriteTo(RFL_HEADSIZE + i, &u, 1)) {
							_errcode = RFLE_FILEIO;
							return RFL_ERR_POS;
						}
						_pgtab[i] = u;
						_nextpgpos = i * 8u + j + 1;
						return i * 8 + j;
					}
				}
			}
			_errcode = RFLE_RECFULL;
			return RFL_ERR_POS;
		}

		bool free_page(uint32_t upos)
		{
			_errcode = 0;
			uint32_t ub = upos / 8u, ui = upos % 8u;
			if (ub >= sizepagetable())
				return true;
			uint32_t u = _pgtab[ub];
			if (u & (0x01 << ui)) {
				u &= ~(0x01 << ui);
				unique_filelock flck(this, RFL_HEADSIZE + ub, 1, true);
				if (1 != WriteTo(RFL_HEADSIZE + ub, &u, 1)) {
					_errcode = RFLE_FILEIO;
					return false;
				}
				if (_nextpgpos > upos)
					_nextpgpos = upos;
				_pgtab[ub] = u;
				return true;
			}
			return true;
		}

	public:

		/*!
		 \brief	Create record file

		 \param 	sdbfile 	The DB file name, utf8.
		 \param 	maxrecs 	The max records.
		 \param 	blksize 	The blockk size.
		 \param 	sappflag	The applaction flag.

		 \return	0:success. -1:error.
		 \remark    if sdbfile is exist return -1;
		 */
		int create_file(const char* sdbfile, uint32_t maxrecs, uint32_t blksize, const char * sappflag, bool bsync = true)
		{
			if (blksize < RFL_MINBLKSIZE || blksize > RFL_MAXBLKSIZE || maxrecs < RFL_MINRECS || maxrecs > RFL_MAXRECS) {
				_errcode = RFLE_PARAM;
				return -1;
			}

			Close();
			_maxrecs = maxrecs;
			_sizeblk = blksize;
			_nextpgpos = 0;
			_filesize = 0;

			_errcode = RFLE_FILEIO;
			uint32_t uflag = OF_RDWR | OF_CREAT;
			if (bsync)
				uflag |= OF_SYNC;
			if (!Open(sdbfile, uflag, OF_SHARE_READ))
				return -1;
			do{
				unique_filelock flck(this, 0, 0, true);
				char head[RFL_HEADSIZE] = { 0 };
				t_head* ph = (t_head*)&head;

				ec::strlcpy(ph->sysflag, RFILE_SYS_FLAG, sizeof(ph->sysflag));
				ec::strlcpy(ph->appflag, sappflag, sizeof(ph->appflag));
				ph->maxrecs = maxrecs;
				ph->sizeblk = blksize;
				ph->crc32 = ec::crc32(ph, 248);

				if (_pgtab) {
					free(_pgtab);
					_pgtab = nullptr;
				}
				_pgtab = (uint8_t*)malloc(sizepagetable());
				if (!_pgtab) {
					_errcode = RFLE_MEMORY;
					File::Close();
					return -1;
				}

				if (Write(head, sizeof(head)) < 0) {
					File::Close();
					return -1;
				}

				memset(_pgtab, 0, sizepagetable());
				if (Write(_pgtab, sizepagetable()) < 0) {
					File::Close();
					return -1;
				}
				_filesize = Seek(0, seek_end);
				flush();
			}while (0);
			_errcode = 0;
			return 0;
		}

		/*!
		\brief open an exist file
		*/
		int open_file(const char* sdbfile, const char * sappflag = nullptr, bool bsync = true)
		{
			uint32_t uflag = OF_RDWR;
			if (bsync)
				uflag |= OF_SYNC;
			if (!Open(sdbfile, uflag, OF_SHARE_READ)) {
				_errcode = RFLE_NOTEXIST;
				return -1;
			}
			do {
				unique_filelock flck(this, 0, 0, false);
				char head[RFL_HEADSIZE] = { 0 };
				t_head* ph = (t_head*)&head;
				if (sizeof(t_head) != Read(ph, sizeof(t_head)) || !ec::streq(RFILE_SYS_FLAG, ph->sysflag)) {
					_errcode = RFLE_SYSFLAG;
					File::Close();
					return -1;
				}
				if (sappflag && !ec::streq(sappflag, ph->appflag)) {
					_errcode = RFLE_APPFLAG;
					File::Close();
					return -1;
				}
				if (ph->crc32 != ec::crc32(ph, 248)) {
					_errcode = RFLE_CRC32;
					File::Close();
					return -1;
				}
				_maxrecs = ph->maxrecs;
				_sizeblk = ph->sizeblk;
				if (_pgtab) {
					::free(_pgtab);
					_pgtab = nullptr;
				}
				_pgtab = (uint8_t*)malloc(sizepagetable());
				if (!_pgtab) {
					_errcode = RFLE_MEMORY;
					File::Close();
					return -1;
				}

				if (Seek(RFL_HEADSIZE, seek_set) < 0) {
					_errcode = RFLE_PHTABLE;
					File::Close();
					return -1;
				}
				uint32_t urecs = sizepagetable();
				if (Read(_pgtab, urecs) != (int)urecs) {
					_errcode = RFLE_PHTABLE;
					File::Close();
					return -1;
				}
				_nextpgpos = RFL_ERR_POS;
				for (uint32_t i = 0; i < urecs; i++) {
					if (0xFF == _pgtab[i])
						continue;
					for (uint32_t j = 0; j < 8u; j++) {
						if (!((0x01 << j) & _pgtab[i])) {
							_nextpgpos = i * 8u + j;
							break;
						}
					}
					break;
				}
				if (RFL_ERR_POS == _nextpgpos)
					_nextpgpos = urecs * 8u;
				_filesize = Seek(0, seek_end);
			} while (0);
			_errcode = 0;
			return 0;
		}

		/*!
		\brief read all records
		return 0:OK; -1:error;
		*/
		int load_records(std::function<void(void *prec, size_t recsize, uint32_t upositon)> fun)
		{
			uint32_t urecs = sizepagetable();
			for (uint32_t i = 0; i < urecs; i++) {
				if (!_pgtab[i])
					continue;
				for (uint32_t j = 0; j < 8u; j++) {
					if ((0x01 << j) & _pgtab[i]) {
						if (ReadFrom(RFL_HEADSIZE + urecs + (i * 8 + j) * (long long)_sizeblk, _tmpblk, _sizeblk) != (int)_sizeblk) {
							_errcode = RFLE_FILEIO;
							return -1;
						}
						uint8_t *ph = _tmpblk;
						uint16_t ulen = ph[5];
						ulen = (ulen << 8) + ph[4];

						if (ulen > _sizeblk || ph[7] != (ph[4] ^ ph[5])) {
							_errcode = RFLE_RECHEADERR;
							return -1;
						}
						fun(ph + RFILE_REC_HEAD_SZIE, ulen, i * 8 + j);
					}
				}
			}
			_errcode = 0;
			return 0;
		}

		/*!
		 \brief	Write one record

		 \param 	prec	The record.
		 \param 	size	The size of record.
		 \param 	pos 	The position, if pos == RFL_ADD_POS, write to new postion

		 \return	if success return record position, else return RFL_ERR_POS.

		 \remark record head size = 8
		 |byte0|byte1|byte2|byte3|byte4|byte5|byte6|byte7|
		 byte0 : 0: empty; FF used
		 byte1 : 0; reserve
		 byte2 : 0; reserve
		 byte3 : 0; reserve
		 byte4 : length(16bit) low  byte
		 byte5 : length(16bit) high byte
		 byte6 : 0; reserve; May be used to extend the length
		 byte7 : check bytes, byte4 xor byte5
		 */
		uint32_t write_record(const void* prec, size_t size, uint32_t pos)
		{
			if (size + RFILE_REC_HEAD_SZIE > _sizeblk) {
				_errcode = RFLE_PARAM;
				return RFL_ERR_POS;
			}
			_errcode = RFLE_FILEIO;
			uint32_t upos = pos; // alloc page if need
			if (RFL_ADD_POS == pos) {
				if (RFL_ERR_POS == (upos = alloc_page()))
					return RFL_ERR_POS;
			}

			uint8_t *rh = _tmpblk; // add record head
			memset(rh, 0, RFILE_REC_HEAD_SZIE);
			rh[4] = static_cast<uint8_t>(size & 0xFF);
			rh[5] = static_cast<uint8_t>(size >> 8);
			rh[7] = rh[4] ^ rh[5];
			memcpy(rh + RFILE_REC_HEAD_SZIE, prec, size);

			int nw = 0, nsize; // write record
			long long lpos = RFL_HEADSIZE + sizepagetable() + upos * (long long)_sizeblk;

			Lock(lpos, _sizeblk, true);
			if (lpos + _sizeblk > _filesize)
				nsize = (int)_sizeblk; // append one block
			else
				nsize = (int)size + RFILE_REC_HEAD_SZIE; // write only data
			nw = WriteTo(lpos, rh, nsize);
			Unlock(lpos, _sizeblk);

			if (nw != nsize) {
				if (RFL_ERR_POS == pos)
					free_page(upos);
				_errcode = RFLE_FILEIO;
				return RFL_ERR_POS;
			}
			if (lpos + _sizeblk > _filesize) {
				_filesize = lpos + _sizeblk;
				flush_flag = 1;
			}

			_errcode = 0;
			return upos;
		}

		inline bool delete_record(uint32_t upos)
		{
			return free_page(upos);
		}

		inline int getlasterror() {
			return _errcode;
		}

		void flushend()
		{
			if (!flush_flag)
				return;
			long long lsize = Seek(0, seek_end);
			if (lsize > 0) {
				File::flush();
				_filesize = lsize;
				flush_flag = 0;
			}
		}
	};
}//namespace ec
