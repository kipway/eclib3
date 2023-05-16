/*!
\file ec_ucpx.h

实现一个基于udp多通道并行的可靠传输的封装，取名为ucpx;

\author jiangyong

\update 2023-2-7 增加ipv6支持
\update 2022-10-28 优化CPU占用
\update 2022-10-16 优化速度
\update 2022-10-7 基于eclib3内存分配器和队列优化
\update 2022-9-20增加多线路支持，升级为ec_ucpx.h,不兼容原ec_ucp.h
\update 2022-8-26做了确认优化
\update 2022-8-8做了帧缓冲优化
\update 2021-12-3做了最大承载优化，不兼容以前版本
*/
#pragma once

#include "ec_alloctor.h"
#include <vector>
#include "ec_stream.h"
#include "ec_crc.h"
#include "ec_log.h"
#include "ec_map.h"
#include "ec_vector.hpp"

#ifndef SIZE_UDPBUF_FRMS
#define SIZE_UDPBUF_FRMS 512 //udp未确认缓冲大小,单位ucp报文数
#endif
#define SIZE_UDPSENDFULL_FRMS (SIZE_UDPBUF_FRMS - 2) //发送缓冲满

#ifndef SIZE_MTU
#define SIZE_MTU 1492u
#endif
#define SIZE_UDPFRM (SIZE_MTU - 28u) //报文最大长度,1492(mtu_size) - 20(ipv4_head)-8(udp_head), test use "ping baidu.com -l 1464 -f" , icmp头和udp头长度相同 = 8

#ifdef SEQNO_32
using seqno_t = uint32_t;
#define SIZE_UCPHEAD 16u //报头大小，32位seqno为16Bytes
#else
using seqno_t = uint64_t;
#define SIZE_UCPHEAD 20u //报头大小，64位seqno为20Bytes
#endif
#define SIZE_UDPCONTENT (SIZE_UDPFRM - SIZE_UCPHEAD)

#define FRMCMD_HRT  20 //心跳包
#define FRMCMD_SYN  21 //请求连接,客户端发送
#define FRMCMD_SYNR 22 //服务端连接应答,分配一个SSID
#define FRMCMD_DAT  30 //数据包
#define FRMCMD_DATR 31 //重发的数据包
#define FRMCMD_ACK  32 //数据确认包seqno
#define FRMCMD_FIN  33 //断开包,客户端和服务端都可主动发送。

#define UCP_GUID_SIZE 16 //多通道防止重复连接的GUID字节数

namespace ec {
	namespace ucp {
		enum ucptype {
			ucp_conin = 0,
			ucp_conout = 1
		};

		enum ucpdisconnect {
			ucp_disconnect_fin = 0,
			ucp_disconnect_err = 1,
			ucp_disconnect_timeout = 2
		};

		class udp_item //udp会话参数
		{
		public:
			int _fd;
			ec::net::socketaddr _netaddr;
		public:
			udp_item() : _fd(0) {
			}
			inline const struct sockaddr* addr() {
				return _netaddr.addr();
			}
			int addrlen() {
				return _netaddr.addrlen();
			}
			inline void set(const struct sockaddr* paddr, size_t addrsize)
			{
				_netaddr.set(paddr, addrsize);
			}
		};

		using udp_chns = std::vector<udp_item, ec::std_allocator<udp_item>>; //udp通道参数vector

		//UDP发送回调,由应用层实现,如果因系统底层发送缓冲满失败，由应用层暂存实现异步发送。
		typedef int(*cb_udpsend)(int fd, const struct sockaddr* paddr, int addrlen, const void* pdata, size_t size, void* app_param, bool bresend);

		/*!
		* \brief ucp帧list
		* 适用于发送和接受,每一个socket有一个发送和接收暂存区，用于重发和接收纠序
		*/
		class frmlist
		{
		public:
			/*!
			* \biref 存储ucp帧的单向list节点
			* 用于发送暂存和重发，接收暂存纠序
			*/
			struct t_node
			{
				seqno_t _seqno; //报文号
				int _ack; //0: 未确认; >0:确认次数
				int _cntresend; //重发次数,作为发送缓冲时使用,0表示没有重发.
				uint16_t _frmsize; //报文大小,_frm里字节数
				int64_t  _mstime;//接收或者发送的时标,自1970-1-1的毫秒数,只有发送才会填写用于重发，接受缓冲区始终填写0
				t_node* _pprior;
				t_node* _pnext;
				uint8_t _frm[SIZE_UDPFRM]; //报文存储区,报文是已经编码完成的。
				_USE_EC_OBJ_ALLOCATOR
			};
			using PNODE = t_node*;
		protected:
			t_node* _phead;//最旧的
			t_node* _ptail;//最新的,新来帧从ptail加入
			size_t _size; // 个数,动态维护的list的节点数
		public:
			frmlist()
				: _phead(nullptr)
				, _ptail(nullptr)
				, _size(0)
			{
			}

			virtual ~frmlist() {
				t_node* pf;
				while (_phead) {
					pf = _phead->_pnext;
					delete _phead;
					_phead = pf;
				}
				_size = 0;
				_phead = nullptr;
				_ptail = nullptr;
			}

			inline size_t size() {
				return _size;
			}

			/*!
			* \brief 从尾部插入
			* \param seqno 帧序号
			* \param msec 时标，从1970-1-1开始的毫秒数
			* \param pfrm 完整帧
			* \param frmsize 完整帧大小
			* \param bsndbuf 0表示是接收帧，非0表示是发送帧,需要将_cntsnd置1
			* \return 返回0表示成功,-1表示失败
			* \remark 从尾部加入,确保 seqno大于当前list内的seqno,是insert的效率版,这里不判断是否满，由外部通过size()获取。
			*/
			int push_back(seqno_t seqno, int64_t  msec, const void* pfrm, size_t frmsize)//接收缓冲区使用
			{
				if (!pfrm || frmsize > SIZE_UDPFRM)
					return -1;
				t_node* pf = new t_node;
				if (!pf)
					return -1;
				pf->_seqno = seqno;
				pf->_mstime = msec;
				pf->_pprior = _ptail;
				pf->_pnext = nullptr;
				pf->_ack = 0;
				pf->_cntresend = 0;
				pf->_frmsize = (uint16_t)frmsize;
				memcpy(pf->_frm, pfrm, frmsize);
				if (_ptail)
					_ptail->_pnext = pf;
				else
					_phead = pf;
				_ptail = pf;
				++_size;
				return 0;
			}

			int push_back(seqno_t seqno, int64_t  msec, t_node* pf) //发送优化使用
			{
				pf->_seqno = seqno;
				pf->_mstime = msec;
				pf->_pprior = _ptail;
				pf->_pnext = nullptr;
				pf->_ack = 0;
				pf->_cntresend = 0;
				if (_ptail)
					_ptail->_pnext = pf;
				else
					_phead = pf;
				_ptail = pf;
				++_size;
				return 0;
			}

			t_node* makenode(seqno_t seqno, int64_t  msec, const void* pfrm, size_t frmsize)
			{
				t_node* pf = new t_node;
				if (!pf)
					return nullptr;
				pf->_seqno = seqno;
				pf->_mstime = msec;
				pf->_pprior = nullptr;
				pf->_pnext = nullptr;
				pf->_ack = 0;
				pf->_cntresend = 0;
				pf->_frmsize = (uint16_t)frmsize;
				memcpy(pf->_frm, pfrm, frmsize);
				return pf;
			}
		protected:
			/*!
			* \brief 插入到合适位置,seqno相同滤掉
			* \param seqno 帧序号
			* \param msec 时标，从1970-1-1开始的毫秒数
			* \param pfrm 完整帧
			* \param frmsize 完整帧大小
			* \param bsndbuf 0表示是接收帧，非0表示是发送帧,需要将_cntsnd置1
			* \return 返回0表示成功,-1表示失败
			* \remark 这里不判断是否满，由外部通过size()获取。seqno一定小于ptail->seqno
			*/
			int insert_fast(seqno_t seqno, int64_t  msec, const void* pfrm, size_t frmsize)
			{
				if (!pfrm || frmsize > SIZE_UDPFRM)
					return -1;
				if (!_phead) {
					t_node* pf = makenode(seqno, msec, pfrm, frmsize);
					if (nullptr == pf)
						return -1;
					_phead = pf;
					_ptail = pf;
					_size = 1;
					return 0;
				}
				//最近方向插入
				if (seqno <= _phead->_seqno || (seqno - _phead->_seqno < _ptail->_seqno - seqno))
					return insert_head(seqno, msec, pfrm , frmsize);
				return insert_tail(seqno, msec, pfrm, frmsize);
			}
		private:
			int insert_head(seqno_t seqno, int64_t  msec, const void* pfrm, size_t frmsize)//从头部开始插入
			{
				t_node* p = _phead;
				t_node* pf = nullptr;
				while (p) {
					if (p->_seqno == seqno)
						return 0; //已经存在
					if (p->_seqno > seqno) { //插入到p之前
						if (nullptr == (pf = makenode(seqno, msec, pfrm, frmsize)))
							return -1;
						pf->_pprior = p->_pprior;
						pf->_pnext = p;
						if (p->_pprior)
							p->_pprior->_pnext = pf;
						else
							_phead = pf;
						p->_pprior = pf;
						_size++;
						return 0;
					}
					p = p->_pnext;
				}
				if (nullptr == (pf = makenode(seqno, msec, pfrm, frmsize)))
					return -1;
				_ptail->_pnext = pf;
				pf->_pprior = _ptail;
				_ptail = pf;
				_size++;
				return 0;
			}

			int insert_tail(seqno_t seqno, int64_t  msec, const void* pfrm, size_t frmsize) //从尾部开始插入
			{
				if (!pfrm || frmsize > SIZE_UDPFRM)
					return -1;
				t_node* p = _ptail;
				t_node* pf = nullptr;
				while (p) {
					if (p->_seqno == seqno)
						return 0; //已经存在
					if (p->_seqno < seqno) { //插入到p之后
						if (nullptr == (pf = makenode(seqno, msec, pfrm, frmsize)))
							return -1;
						pf->_pnext = p->_pnext;
						if (p->_pnext)
							p->_pnext->_pprior = pf;
						else
							_ptail = pf;
						pf->_pprior = p;
						p->_pnext = pf;
						_size++;
						return 0;
					}
					p = p->_pprior;
				}
				if (nullptr == (pf = makenode(seqno, msec, pfrm, frmsize)))
					return -1;
				pf->_pnext = _phead;
				_phead->_pprior = pf;
				_phead = pf;
				return 0;
			}
		protected:
			bool qsearch_(seqno_t v, seqno_t* pnos, size_t size)
			{
				if (size < 12) {
					for (auto i = 0u; i < size; i++) {
						if (v == pnos[i])
							return true;
					}
					return false;
				}
				int nl = 0, nh = (int)size - 1, nm;//下面利用二分查找
				while (nl <= nh) {
					nm = (nl + nh) / 2;
					if (v == pnos[nm])
						return true;
					else if (pnos[nm] < v)
						nl = nm + 1;
					else
						nh = nm - 1;
				}
				return false;
			}
		};

		/*!
		* \brief 发送list,添加一些发送需要的方法
		* 发送等待确认区,超过一定时间后重发, 重发超过一定次数后，报告丢包，通知应用层。
		* 定时清理已经确认的报文，从头开始。
		*/
		class frmlist_send : public frmlist
		{
		public:
			frmlist_send(){
			}
			virtual ~frmlist_send() {
			}

			/*!
			* 计算重发时间节点
			* remark 第一次重发间隔basetime；第二次间隔2*basetime； 第三次间隔4*basetime
			*/
			int resendtime(int cnt, int basetime)
			{
				int i = 0, n = 1;
				for (; i < cnt; i++)
					n = n * 2;
				return n * basetime;
			}

			/*!
			* \brief 处理重发,定时调用,多通道发送
			* \param curmstime当前时间，GMT毫秒数
			* \param udps UDP线路的参数
			* \param udpsend, udp发送回调函数, 返回>=0表示发送的字节数, -1表示发送错误
			* \param udpsend_param 发送回调的参数
			* \param maxcnt 成功时回填最大重发次数
			* \param maxacksndno 已确认的最大序列号，用于智能重发。
			* \param acknodelta 智能重发序列号差数，当未确认的的包序列号小于最大已确认序列号差值大于该数时做第一次重发。默认配置为10
			* \param baseresendtime 基础重发时间，毫秒
			* \param maxsndfrms 最大重发包数
			* \return 返回 >=0重发的帧数; -1表示失败。
			* \remark 发送缓冲中没有被确认的，第一次重发采用确认序列号判断和时间判断,以后采用时间判断；
			*/
			int resend(int64_t curmstime, udp_chns& udps, cb_udpsend udpsend, void* udpsend_param //回调函数的参数
				, int& maxcnt, seqno_t maxacksndno, uint32_t acknodelta, int baseresendtime, int maxsndfrms
				, int& as_timeover, int&  as_seqo)
			{
				int n = 0, ndo=0;
				maxcnt = 0;
				t_node* pf = _phead;
				while (pf && n < maxsndfrms) {
					if (!pf->_ack) {
						ndo = 0;
						if (llabs(curmstime - pf->_mstime) > resendtime(pf->_cntresend, baseresendtime)) { //超时重发
							as_timeover += 1;
							ndo = 1;
						}
						else if (0 == pf->_cntresend && pf->_seqno + acknodelta < maxacksndno) { //快速重发
							as_seqo += 1;
							ndo = 1;
						}
						if(ndo) {
							for (auto& i : udps)
								udpsend(i._fd, i.addr(),i.addrlen(), pf->_frm, pf->_frmsize, udpsend_param, true);//重发优先，使用插入
							pf->_cntresend++;
							pf->_mstime = curmstime;
							n++;
							if (maxcnt < pf->_cntresend)
								maxcnt = pf->_cntresend;
						}
					}
					pf = pf->_pnext;
				}
				return n;
			}

			bool acked(seqno_t seqno)//判断seqno是否已经确认了。
			{
				return !_phead || _phead->_seqno > seqno;
			}

			/*!
			* \brief 批量确认并从发送缓冲删除,确认到act2no(含)
			* \param  pnos 需要确认的序号数组
			* \param  pnos里的序号个数
			* \param  umaxseqno out 对端收到的最大seqno，用于判断跳号重发
			* \return 返回确认的个数
			*/
			int ack_del(seqno_t act2no, seqno_t* pnos, size_t size, seqno_t& umaxseqno)
			{
				int nr = 0;
				t_node* pf = _phead;
				t_node* pdel;
				if (umaxseqno < act2no)
					umaxseqno = act2no;
				while (pf) {
					if (pf->_ack || pf->_seqno <= act2no || qsearch_(pf->_seqno, pnos, size)) {
						pdel = pf;
						if (umaxseqno < pf->_seqno)
							umaxseqno = pf->_seqno;
						nr++;
						if (pf->_pprior)
							pf->_pprior->_pnext = pf->_pnext;
						if (pf->_pnext)
							pf->_pnext->_pprior = pf->_pprior;
						if (pf == _phead)
							_phead = pf->_pnext;
						if (pf == _ptail)
							_ptail = pf->_pprior;
						pf = pf->_pnext;
						delete pdel;
						--_size;
					}
					else
						pf = pf->_pnext;
				}
				return nr;
			}
		};

		/*!
		* \brief 接收list,添加一些接收需要的方法
		* 插入去重, 确认处理。
		*/
		class frmlist_recv :public frmlist
		{
		private:
			seqno_t _maxseqno = 0;
		public:
			frmlist_recv(){
			}
			virtual ~frmlist_recv() {
			}

			/*!
			* brief 接收到的帧加入list
			* return 0:ok; -1:error
			* remark 只是加入list,加入成功后在使用ackread提出接收到的帧数据
			*/
			int add(seqno_t seqno, const void* pfrm, size_t frmsize)
			{
				if(_maxseqno < seqno)
					_maxseqno = seqno;
				if (!_ptail || _ptail->_seqno < seqno) {
					return push_back(seqno, 0, pfrm, frmsize);
				}
				return insert_fast(seqno, 0, pfrm, frmsize);
			}

			inline seqno_t maxrecvno()
			{
				return _maxseqno;
			}
			/*!
			* brief 从头开始确认已经收到的报文
			\param nxtrcvno 下一个接收序列号
			\param funrecv 帧回调，依次回调完整的ucp帧,返回-1：表示错误
			\return 确认的报文数 n.
			\remark 当return > 0 时，需要向对端发送确认到序列号(nxtrcvno + n - 1)的控制报文;
			*/
			int ackread(seqno_t nxtrcvno, ec::vector<t_node*>&frmout)
			{
				int nr = 0;
				seqno_t no = nxtrcvno;
				t_node* pf = _phead;
				while (pf && pf->_seqno == no) {
					frmout.push_back(pf);
					pf = pf->_pnext;
					--_size;
					++nr;
					++no;
				}
				if (pf) {
					pf->_pprior = nullptr;
					_phead = pf;
				}
				else {
					_phead = nullptr;
					_ptail = nullptr;
					_size = 0;
				}
				return nr;
			}

			/**
			 * @brief 读取接收缓冲的批量确认并设置确认次数+1,返回个数
			 * @tparam _CLS 
			 * @param nmax 
			 * @param uplimit 
			 * @param ackto 
			 * @param seqnos 
			 * @return 返回需要确认的seqno个数
			 * @remark deprecated at 2022.10.28
			*/
			template<class _CLS>
			int getrecvacks(int nmax, int uplimit, seqno_t ackto, _CLS& seqnos)
			{
				int n = 0;
				t_node* pf = _phead;
				seqnos.clear();
				while (n < nmax && pf) {
					if (pf->_ack < uplimit && pf->_seqno >= ackto) {
						seqnos.push_back(pf->_seqno);
						pf->_ack++;
						++n;
					}
					pf = pf->_pnext;
				}
				return n;
			}
		};

		/*！
		* class frmpkg
		* brief ucp帧编码类
		 编码直接二进制小头优化输出,计算头部CRC32后，用CRC32做快速XOR掩码，产生的帧没有特征字。
		 帧头16字节,其后为承载数据.
		*/
		class frmpkg
		{
		public:
			uint32_t _crc32; //校验值,从ussid到datasize共16字节的CRC32校验值,最后用crc32做其后的所有字节快速异或。
			uint32_t _ussid; //服务器分配的会话ID
			seqno_t  _seqno; //序列号
			uint8_t  _frmcmd; //包指令
			uint8_t  _res; //保留
			uint16_t _datasize; //数据大小

			uint8_t  _data[SIZE_UDPCONTENT]; //数据

			frmpkg() :_res(0), _datasize(0) {
			}
		public:
			static void fastxor_le(uint8_t* pd, int size, unsigned int umask)
			{ // little endian fast XOR,4x faster than byte-by-byte XOR
				if (!size)
					return;
				int i = 0, nl = 0, nu;
				unsigned int um = umask;
				if ((size_t)pd % 4) {
					nl = 4 - ((size_t)pd % 4);
					um = umask >> nl * 8;
					um |= umask << (4 - nl) * 8;
				}
				nu = (size - nl) / 4;
				for (i = 0; i < nl && i < size; i++)
					pd[i] ^= (umask >> ((i % 4) * 8)) & 0xFF;

				unsigned int* puint = (unsigned int*)(pd + i);
				for (i = 0; i < nu; i++)
					puint[i] ^= um;

				for (i = nl + nu * 4; i < size; i++)
					pd[i] ^= (umask >> ((i % 4) * 8)) & 0xFF;
			}

			/*!
			* brief 使用成员数据组包
			* param pout 输出缓冲
			* param sizeout 输出缓冲的大小
			* return 返回完整帧长度,-1表示空间不够失败
			*/
			int makepkg(void* pout, size_t sizeout)
			{
				if (_datasize > (size_t)SIZE_UDPCONTENT || sizeout < (size_t)SIZE_UCPHEAD + _datasize)
					return -1;
				uint8_t* pu = ((uint8_t*)pout) + 4;
				ec::stream ss(pout, sizeout);
				try {
					_crc32 = 0;
					ss << _crc32 << _ussid << _seqno << _frmcmd << _res << _datasize;
					if (_datasize)
						ss.write(_data, _datasize);
					_crc32 = ec::crc32(pu, SIZE_UCPHEAD - 4);
					ss.setpos(0);
					ss << _crc32;
				}
				catch (...) {
					return -1;
				}
				fastxor_le(pu, SIZE_UCPHEAD - 4 + _datasize, _crc32);//快速异或
				return SIZE_UCPHEAD + _datasize;
			}

			/*!
			* brief 使用成员帧头，外部数据组包
			* param pout 输出缓冲，必须大于
			* param sizeout 输出缓冲的大小
			* return 返回完整帧长度,-1表示空间不够失败
			*/
			int makepkg(void* pout, size_t sizeout, const void* pdata, size_t sizedata)
			{
				if (sizedata > SIZE_UDPCONTENT || sizeout < SIZE_UCPHEAD + sizedata)
					return -1;
				uint8_t* pu = ((uint8_t*)pout) + 4;
				ec::stream ss(pout, sizeout);
				_crc32 = 0;
				_datasize = (uint16_t)sizedata;
				try {
					ss << _crc32 << _ussid << _seqno << _frmcmd << _res << _datasize;
					if (pdata && sizedata)
						ss.write(pdata, _datasize);
					_crc32 = ec::crc32(pu, SIZE_UCPHEAD - 4);
					ss.setpos(0);
					ss << _crc32;
				}
				catch (...) {
					return -1;
				}
				fastxor_le(pu, SIZE_UCPHEAD - 4 + _datasize, _crc32);//快速异或
				return SIZE_UCPHEAD + _datasize;
			}

			/*!
			* brief 解包
			* 解析pfrm 到类成员变量中
			* param pfrm 完整帧
			* param size 帧大小
			* return 0:成功; -1失败
			* remark 注意为了减少一次拷贝，会直接使用pfrm做解码，会改变其原有内容。
			*/
			int parsepkg(void* pfrm, size_t size) //解析报文，返回 -1表示错误; 0表示成功
			{
				if (size < SIZE_UCPHEAD)
					return -1; //长度错误
				uint8_t* pu = ((uint8_t*)pfrm) + 4;
				ec::stream ss(pfrm, size);
				try {
					ss >> _crc32;
					fastxor_le(pu, (int)size - 4, _crc32);//快速异或
					uint32_t crc = ec::crc32(pu, SIZE_UCPHEAD - 4);
					if (crc != _crc32)
						return -1; //校验错误
					ss >> _ussid >> _seqno >> _frmcmd >> _res >> _datasize;
					if (_datasize + SIZE_UCPHEAD != size)
						return -1; //头部错误
					ss.read(_data, _datasize);
				}
				catch (...) {
					return -1;
				}
				return 0;
			}

			void* parsepkgin(void* pfrm, size_t size) //就地解码,返回数据部分指针,其余参数在对象里。
			{
				if (size < SIZE_UCPHEAD)
					return nullptr; //长度错误
				uint8_t* pu = ((uint8_t*)pfrm) + 4;
				ec::stream ss(pfrm, size);
				try {
					ss >> _crc32;
					fastxor_le(pu, (int)size - 4, _crc32);//快速异或
					uint32_t crc = ec::crc32(pu, SIZE_UCPHEAD - 4);
					if (crc != _crc32)
						return nullptr; //校验错误
					ss >> _ussid >> _seqno >> _frmcmd >> _res >> _datasize;
					if (_datasize + SIZE_UCPHEAD != size)
						return nullptr; //头部错误
					return ((uint8_t*)pfrm) + ss.getpos();
				}
				catch (...) {
					return nullptr;
				}
				return nullptr;
			}

			/*!
			* brief 解码头部,用于判断帧类型以便调用相应方法处理
			* param pfrm 完整帧
			* param size 完整帧大小
			* return 0:成功; -1失败
			* remark 解码的数据写入成员变量，不改变原有帧内容.
			*/
			int parsepkghead(const void* pfrm, size_t size) //解析头部，返回 -1表示错误; 0表示成功
			{
				if (size < SIZE_UCPHEAD)
					return -1; //长度错误
				uint8_t ubuf[24];
				memcpy(ubuf, pfrm, SIZE_UCPHEAD);

				uint8_t* pu = &ubuf[4];
				ec::stream ss(ubuf, sizeof(ubuf));
				try {
					ss >> _crc32;
					fastxor_le(pu, SIZE_UCPHEAD - 4, _crc32);//快速异或
					uint32_t crc = ec::crc32(pu, SIZE_UCPHEAD - 4);
					if (crc != _crc32)
						return -1; //校验错误
					ss >> _ussid >> _seqno >> _frmcmd >> _res >> _datasize;
				}
				catch (...) {
					return -1;
				}
				if (_datasize + SIZE_UCPHEAD != size)
					return -1; //头部错误
				return 0;
			}

			static int mkfrm(uint32_t ssid, seqno_t seqno, uint8_t cmd, const void* pdata, size_t sizedata,
				uint8_t* pout, size_t sizeout)
			{
				frmpkg pkg;
				pkg._ussid = ssid;
				pkg._seqno = seqno;
				pkg._frmcmd = cmd;
				return pkg.makepkg(pout, sizeout, pdata, sizedata);
			}
		};

		/*!
		* \brief 在UDP通道上实现一个可靠连接,实现接收，发送，确认，重发
		* \remark 流控原理,发送方在未确认报文达到 SIZE_UDPBUF_FRMS时，停止发送新报文
		*/
		class ucpsocket
		{
		protected:
			uint32_t _ssid; //会话ID,用于识别连接,握手时客户端定义低16位，服务端定义高16位
			seqno_t _nxtsndno; //下一个发送序列号,从1开始
			seqno_t _nxtrcvno; //下一个接收序列号,从1开始,
			seqno_t _ackrcvno; //已向对端发送的确认到序列号,从0开始
			seqno_t _ackmaxsndno; //单个确认的最大序列号，用于实现按照序列号差重发数据.
			frmlist_send _sbuf; //待确认的发送缓冲
			frmlist_recv _rbuf; //接收缓冲,用于纠序和补齐

			int64_t _lastack2time; //上次确认到的时间
			uint32_t _uack2sameno; //相同_ackrcvno的确认次数，不超过4次

			ec::vector<seqno_t> _seqnos;//重复使用的多seqno确认处理缓冲区
		public:
			int _sstype; //会话类型; 0接入; 1连出
			int64_t  _time_lastread; //最后一次接收报文时间,1970-1-1的GMT毫秒数
			int64_t  _time_lastsend; //最后一次发送报文时间,1970-1-1的GMT毫秒数
			int _forceack2; //设置需要应答sck to标志
			udp_chns _udps;//udp通道
			uint8_t _guid[UCP_GUID_SIZE];//连接的MD5散列值的guid，防止多通道重复连接
		public:
			_USE_EC_OBJ_ALLOCATOR
		public:
			ucpsocket(uint32_t ssid, int sstype)
				: _ssid(ssid)
				, _nxtsndno(1)
				, _nxtrcvno(1)
				, _ackrcvno(0)
				, _ackmaxsndno(0)
				, _sstype(sstype)
			{
				_time_lastread = ec::mstime();
				_time_lastsend = _time_lastread;
				_forceack2 = 0;
				_lastack2time = 0;
				_uack2sameno = 0;
				_seqnos.reserve(SIZE_UDPCONTENT/sizeof(seqno_t));
				memset(_guid, 0, sizeof(_guid));
			}

			void addudp(int fd, const struct sockaddr* paddr, int addrlen)
			{
				int n = 0;
				for (auto& i : _udps) {
					if (fd == i._fd) {
						i.set(paddr, addrlen);
						n = 1;
						break;
					}
				}
				if (!n) {
					udp_item it;
					it._fd = fd;
					it.set(paddr, addrlen);
					_udps.push_back(it);
				}
			}

			inline uint32_t get_ssid() {
				return _ssid;
			}

			inline uint32_t set_ssid(uint32_t ssid) {
				return _ssid = ssid;
			}

			inline seqno_t getnxtrcvno() {
				return _nxtrcvno;
			}

			inline seqno_t maxrecvno() {
				return _rbuf.maxrecvno();
			}
			inline seqno_t getrcvack2no() {
				return _ackrcvno;
			}

			size_t sndbuf_leftfrms() //发送空间剩余可用帧数
			{
				if (_sbuf.size() < SIZE_UDPSENDFULL_FRMS)
					return (SIZE_UDPSENDFULL_FRMS - _sbuf.size());
				return 0u;
			}

			inline size_t sndbufsize() {
				return _sbuf.size();
			}
			inline size_t recvbufsize() {
				return _rbuf.size();
			}

			inline bool acked(seqno_t seqno)
			{
				return _sbuf.acked(seqno);
			}
			/*!
			\brief 处理一帧接收到的数据帧，重发帧和确认帧
			\param fd 接收数据的fd
			\param paddr 对端地址
			\param pkg 报文编解码对象
			\param pudp 收到的udp帧数据
			\param size pudp的字节数
			\param plog 日志
			\param vout 解析后的承载数据输出缓冲, append模式.
			\return o:OK; -1:error;
			*/
			int dorevc(int fd, const struct sockaddr* paddr, int addrlen, 
				frmpkg& pkg, const void* pudp, size_t size, ec::ilog* plog, ec::vector<frmlist::t_node*>& outfrms)
			{
				int nr = 0;
				switch (pkg._frmcmd) {
				case FRMCMD_DAT:
				case FRMCMD_DATR:
					nr = do_datafrm(pkg._seqno, pudp, size, plog, outfrms);
					break;
				case FRMCMD_ACK:
					if (1) {
						frmpkg pkgd;
						if (pkgd.parsepkg((void*)pudp, size) < 0) {
							plog->add(CLOG_DEFAULT_ERR, "ssid(%08XH) ACKS parsepkg failed.", _ssid);
							return -1;
						}
						seqno_t no;
						_seqnos.clear();//2022-11-8开始，对端会存放一个收到的最大序列号
						try {
							ec::stream ss(pkgd._data, pkgd._datasize);
							for (auto i = 0u; i < pkgd._datasize / sizeof(seqno_t); i++) {
								ss >> no;
								_seqnos.push_back(no);
							}
						}
						catch (...) {
							plog->add(CLOG_DEFAULT_ERR, "ssid(%08XH) ACKS stream failed.", _ssid);
							return -1;
						}
						_sbuf.ack_del(pkgd._seqno, _seqnos.data(), _seqnos.size(), _ackmaxsndno);
					}
					break;
				}
				return nr;
			}

			/*!
			* brief 多通道发送心跳，确认等无需确认的控制报文。
			*/
			int sendfrm(const void* pfrm, size_t size, cb_udpsend udpsend, void* udpsend_param)
			{
				int nret = -1, nr;
				for (auto& ch : _udps) {
					nr = udpsend(ch._fd, ch.addr(), ch.addrlen(), pfrm, size, udpsend_param, false);
					if (nr >= 0 && nret < 0)
						nret = nr;
				}
				return nret;
			}

			/*!
			* brief 发送数据,多通道发送,同时加入待确认发送缓冲
			* param pdata 数据
			* param size 数据长度
			* param udpsend 如果有应答,会使用这个回调函数向对端发送udp报文(ucp帧),返回>=0表示发送的字节数, -1表示发送错误
			* param udpsend_param udpsend中的app_param。
			* return >=0返回发送的数据字节数, -1表示错误
			*/
			int sendbytes(const void* pdata, size_t size, cb_udpsend udpsend, void* udpsend_param)
			{
				int ns = 0, nall = (int)size, nc = SIZE_UDPCONTENT, frmlen;
				const char* ps = (const char*)pdata;
				frmpkg pkg;
				while (ns < nall && _sbuf.size() < SIZE_UDPSENDFULL_FRMS) {
					nc = SIZE_UDPCONTENT;
					if (ns + nc > nall)
						nc = nall - ns;
					pkg._ussid = _ssid;
					pkg._seqno = _nxtsndno++;
					pkg._frmcmd = FRMCMD_DAT;
					frmlist::t_node* pnode = new frmlist::t_node;
					if (!pnode)
						return -1;
					frmlen = pkg.makepkg(pnode->_frm, sizeof(pnode->_frm), ps, nc);
					if (frmlen < 0) {
						delete pnode;
						return -1;
					}
					pnode->_frmsize = (uint16_t)frmlen;
					for (auto& ch : _udps) {
						udpsend(ch._fd, ch.addr(), ch.addrlen(), pnode->_frm, pnode->_frmsize, udpsend_param, false);
					}
					_sbuf.push_back(pkg._seqno, ec::mstime(), pnode);
					ps += nc;
					ns += nc;
				}
				if (ns > 0)
					_time_lastsend = ec::mstime();
				return ns;
			}

			/*!
			* brief 重发,多通道发出
			* param tmsec 当前时间,用于判断是否需要重发
			* param nmaxcnt 返回时填写重发的最大次数,用于判断是否需要断开连接。
			* param udpsend 如果有应答,会使用这个回调函数向对端发送udp报文(ucp帧),返回>=0表示发送的字节数, -1表示发送错误
			* param udpsend_param udpsend中的app_param。
			* param acknodelta 按照seqno优化重发的参数(配置里的"重发确认序号间隔")
			* param baseresendtime 基础重发超时
			* param maxsndfrms 最大重发帧数.
			* remark 直接调用sbuf的重发处理,一般定时被调用
			*/
			int resend(int64_t tmsec, int& nmaxcnt,
				cb_udpsend udpsend,
				void* udpsend_param,
				uint32_t acknodelta,
				int baseresendtime,
				int maxsndfrms,
				int& as_timeover, int& as_seqo
			)
			{
				int nr = _sbuf.resend(tmsec, _udps, udpsend, udpsend_param, nmaxcnt, _ackmaxsndno,
					acknodelta, baseresendtime, maxsndfrms, as_timeover, as_seqo);
				if (nr > 0)
					_time_lastsend = tmsec;
				return nr;
			}

			/*!
			* brief 批量确认，定时重复确认使用
			* param frmout报文输出区
			* param sizeout 报文输出区大小
			* return 返回报文长度
			* remark 用于减少网络环境较差时确认包丢失造成的不必要的重发。 deprecated at 2022.10.28
			*/
			//批量确认返回完整报文,0没有，-1错误; >0报文长度
			int acks(uint8_t* frmout, size_t sizeout)
			{
				seqno_t u2 = _nxtrcvno - 1u;
				ec::array<seqno_t, SIZE_UDPCONTENT / sizeof(seqno_t)> nos;
				int n = _rbuf.getrecvacks((int)nos.capacity(), 4, _nxtrcvno, nos);
				if (!n && !u2)
					return 0;
				if (!n && _ackrcvno >= u2 && _uack2sameno > 2 && !_forceack2)
					return 0;
				if (_ackrcvno != u2)
					_uack2sameno = 0;
				++_uack2sameno;

				frmpkg pkg;
				pkg._ussid = _ssid;
				pkg._seqno = u2;
				pkg._frmcmd = FRMCMD_ACK;
				ec::stream ss(pkg._data, sizeof(pkg._data));
				for (auto& i : nos)
					ss << i;
				pkg._datasize = (uint16_t)(sizeof(seqno_t) * nos.size());
				int frmsize = pkg.makepkg(frmout, sizeout);
				if (frmsize < 0)
					return -1;
				_ackrcvno = u2;
				_forceack2 = 0;
				return frmsize;
			}

			/**
			* brief 确认到
			* param frmout报文输出区
			* param sizeout 报文输出区大小
			* return 返回报文长度
			* remark 没有数据,包头的seqno存储已确认的seqno,
			*/
			int ack(uint8_t* frmout, size_t sizeout)
			{
				seqno_t u2 = _nxtrcvno - 1u;
				if (!u2)
					return 0;
				if (_ackrcvno == u2 && _uack2sameno > 2 && !_forceack2)
					return 0;
				if (_ackrcvno != u2)
					_uack2sameno = 0;
				++_uack2sameno;

				frmpkg pkg;
				pkg._ussid = _ssid;
				pkg._seqno = u2;
				pkg._frmcmd = FRMCMD_ACK;
				
				ec::stream ss(pkg._data, sizeof(pkg._data));
				ss << maxrecvno(); //带一个收到的最大序列号
				pkg._datasize = (uint16_t)sizeof(seqno_t);
				int frmsize = pkg.makepkg(frmout, sizeout);
				if (frmsize < 0)
					return -1;
				_ackrcvno = u2;
				_forceack2 = 0;
				return frmsize;
			}
		protected:
			/*!
			* brief 处理数据帧，包括重发的数据帧
			* param seqno 序列号
			* param pudp 完整的帧
			* param size 帧长度
			* param plog 日志
			* param vout 承载数据输出缓冲，append模式
			* return 0:OK; -1:error;
			*/
			int do_datafrm(seqno_t seqno, const void* pudp, size_t size, ec::ilog* plog, ec::vector<frmlist::t_node*>& frmout)
			{
				if (seqno < _nxtrcvno) { //已经处理了，直接抛弃, 回送一个确认到
					_forceack2 = 1;
					return 0;
				}
				if (_rbuf.add(seqno, pudp, size) < 0)
					return -1;
				int nr = _rbuf.ackread(_nxtrcvno, frmout);
				if (nr > 0) //以后连续确认
					_nxtrcvno += nr;
				if (nr < 0)
					return -1;
				return 0;
			}
		};

#define NOTIFY_CMD_CONNECT 0
#define NOTIFY_CMD_DISCONNECT 1

		/*!
		* \class ucp
		* \brief udp通道上的tcp,多连接封装
		*/
		class ucp
		{
		public:
			using PSOCKET = ucpsocket*;
		protected:
			udp_chns _udps;//udp通道
			uint32_t _nxtssid; //下一个本端ssid, 1-65534，客户端使用低16位，服务端使用高16位，组成一个连接唯一的ID
			int64_t  _lastruntime;//上次运行时间，用于runtime定时调用
			ec::ilog* _plog; //日志输出

			int(*_ucpnotify)(uint32_t ssid, int cmd, int cmdarg, void* app_param); //通知回调,包括断开和连接成功
			void* _notify_param;//通知回调的参数

			cb_udpsend _udpsend; //udp发送回调
			void* _udpsend_param;//udp发送回调参数

			//重发基础时间毫秒[50, 500], 200
			uint32_t _resendtime;

			//重发确认序号间隔[3, 30], 15
			uint32_t _resendackno;

			//最大重发次数[3, 10], 5
			uint32_t _maxresendcnt;

			ec::vector<uint32_t> _delssids; //超时需要删除的连接

			uint32_t mknxtid() // 服务端会将客户端的ID放在低两个字节，服务端的ID放在高两字节作为通信SSID
			{
				++_nxtssid;
				while (_nxtssid == 0 || _map.get(_nxtssid)) {
					++_nxtssid;
					if (_nxtssid >= 65535)
						_nxtssid = 1;
				}
				return _nxtssid;
			}

		public:
			struct keq_psocket {
				bool operator()(uint32_t key, const PSOCKET& val)
				{
					return key == val->get_ssid();
				}
			};

			ec::hashmap<uint32_t, PSOCKET, keq_psocket> _mapcon; //连接中表,ssid为客户端分配(低16位)
			ec::hashmap<uint32_t, PSOCKET, keq_psocket> _map; //已连接表

			void loginfo(int viewcon)
			{
				if (_plog) {
					if (viewcon)
						_plog->add(CLOG_DEFAULT_DBG, "map size %zu, mapcon size %zu", _map.size(), _mapcon.size());
					else
						_plog->add(CLOG_DEFAULT_DBG, "map size %zu", _map.size());
				}
			}
		protected:
			//连接断开通知, status = 0表示正常断开(收到FIN命令)，其他为错误断开
			void ondisconnected(uint32_t ssid, int status)
			{
				if (_ucpnotify)
					_ucpnotify(ssid, NOTIFY_CMD_DISCONNECT, status, _notify_param);
			}

			//连接成功通知,返回0表示继续,否则失败删除该连接. dir =0 接入, dir=1 连出
			int onconnect(uint32_t ssid, int dir)
			{
				if (_ucpnotify)
					return _ucpnotify(ssid, NOTIFY_CMD_CONNECT, dir, _notify_param);
				return 0;
			}

			ucpsocket* getbyguid(const void* pguid) //从已连接表中查找guid
			{
				const uint64_t* p1, *p2 = (const uint64_t*)pguid;
				for (auto& i : _map) {
					p1 = (const uint64_t*)i->_guid;
					if (p1[0] == p2[0] && p1[1] == p2[1])
						return i;
				}
				return nullptr;
			}
			
		public:
			ucp() : _nxtssid(0)
				, _lastruntime(0)
				, _plog(nullptr)
				, _ucpnotify(nullptr)
				, _notify_param(nullptr)
				, _udpsend(nullptr)
				, _udpsend_param(nullptr)
				, _mapcon(64)
				, _map(128)
			{
				_resendtime = 260;
				_resendackno = 5;
				_maxresendcnt = 6;
				_udps.reserve(2);
			}

			void init(ec::ilog* plog, cb_udpsend funudpsend, void* udpsend_param
				, int(*cb_ucpnotify)(uint32_t ssid, int cmd, int cmdarg, void* app_param)
				, void* notify_param
			)
			{
				_plog = plog;

				_udpsend = funudpsend;
				_udpsend_param = udpsend_param;

				_ucpnotify = cb_ucpnotify;
				_notify_param = notify_param;
			}

			void addudpsession(int fd, const struct sockaddr* paddr, int addrlen) {
				int n = 0;
				for (auto& i : _udps) {
					if (fd == i._fd) {
						i.set(paddr, addrlen);
						n = 1;
						break;
					}
				}
				if (!n) {
					udp_item it;
					it._fd = fd;
					it.set(paddr, addrlen);
					_udps.push_back(it);
				}
			}

			void setucpargs(uint32_t resendtime, uint32_t resendackno, uint32_t maxresendcnt)
			{
				_resendtime = resendtime;
				_resendackno = resendackno;
				_maxresendcnt = maxresendcnt;
			}

			size_t sndbuf_leftfrms(uint32_t ssid) //可以发送的缓冲大小
			{
				PSOCKET p = nullptr;
				if (!_map.get(ssid, p))
					return 0;
				return p->sndbuf_leftfrms();
			}

			virtual ~ucp() {
				for (auto& i : _map) {
					if (i) {
						delete i;
						i = nullptr;
					}
				}
				_map.clear();
				for (auto& i : _mapcon) {
					if (i) {
						delete i;
						i = nullptr;
					}
				}
				_mapcon.clear();
			}
			
			/*!
			* brief 异步连接,将暂在_mapcon里, 成功返回客户端的 csid
			* param guidmd5 16字节的客户端生成的guid，16字节。
			* return 返回非0为本端的ssid(高16位为0); 0表示失败
			*/
			uint32_t connectasyn(const uint8_t* guidmd5)
			{
				PSOCKET ps = new ucpsocket(mknxtid(), ucp_conout);
				if (!ps)
					return 0;
				memcpy(ps->_guid, guidmd5, UCP_GUID_SIZE);
				for (auto& i : _udps) {
					ps->addudp(i._fd, i.addr(), i.addrlen());
				}
				uint8_t frmreq[SIZE_UDPFRM];
				int ns = frmpkg::mkfrm(ps->get_ssid(), ps->get_ssid(), FRMCMD_SYN, guidmd5, UCP_GUID_SIZE, frmreq, sizeof(frmreq));
				
				//多通道同时发出连接请求
				if (ps->sendfrm(frmreq, ns, _udpsend, _udpsend_param) < 0) {
					delete ps;
					return 0;
				}
				_mapcon.set(ps->get_ssid(), ps);
				return  ps->get_ssid();
			}

			int closessid(uint32_t ssid) //删除ssid
			{
				if (ssid & 0xffff0000) {
					PSOCKET ps = nullptr;
					if (_map.get(ssid, ps)) {
						uint8_t frmret[SIZE_UCPHEAD + 4];
						int ns = frmpkg::mkfrm(ssid, 0, FRMCMD_FIN, nullptr, 0, frmret, sizeof(frmret));
						ps->sendfrm(frmret, ns, _udpsend, _udpsend_param);
						_map.erase(ssid);
						delete ps;
						return 0;
					}
					return -1;
				}
				return _mapcon.erase(ssid, [](PSOCKET& p) {
					if (p) {
						delete p;
						p = nullptr;
					}
				}) ? 0 : (-1);
			}

			/*!
			* brief 处理接收到的报文,接受后返回的确认使用当前通道。
			* param pfrm 完整的ucp报文
			* param size pfrm的字节数
			* param paddr 对端地址,如果时ucp_conin则需要保存到会话种。
			* param rbuf  输出,解析后的承载数据缓冲,append模式
			* param ssid  输出,解析成功后回填的ssid
			* return -1:错误;  0:无输出;
			*/
			int onrecv(int fd, const struct sockaddr* paddr, int addrlen, 
				const void* pfrm, size_t size, ec::vector<frmlist::t_node*>& outfrms, uint32_t &ussid)
			{
				frmpkg pkg;
				uint8_t frmret[SIZE_UDPFRM];
				if (pkg.parsepkghead(pfrm, size) < 0) {
					if (_plog && _plog->getlevel() >= CLOG_DEFAULT_ALL) {
						char smsg[2048];
						smsg[0] = 0;
						ec::bin2view(pfrm, size > 200 ? 200 : size, smsg, sizeof(smsg));
						_plog->add(CLOG_DEFAULT_ALL, "fd(%d) read unkown udp frm size %zu\n%s", fd, size, smsg);
					}
					return 0;
				}
				if (FRMCMD_SYN == pkg._frmcmd) { //连接握手请求,作为服务端
					if (pkg.parsepkg((void*)pfrm, size) < 0) {
						_plog->add(CLOG_DEFAULT_ERR, "fd(%d) read FRMCMD_SYN parsepkg failed", fd);
						return 0;
					}
					if (pkg._datasize != 16) {
						char smsg[2048];
						smsg[0] = 0;
						ec::bin2view(pfrm, size > 200 ? 200 : size, smsg, sizeof(smsg));
						_plog->add(CLOG_DEFAULT_ERR, "fd(%d) read FRMCMD_SYN datasize != 16, size %zu\n%s", fd, size, smsg);
						return 0;
					}
					ucpsocket* pss = getbyguid(pkg._data);
					if (pss) { 
						if (ucp_conin == pss->_sstype) {
							pss->addudp(fd, paddr, addrlen); //设置通道的对端地址
							int ns = frmpkg::mkfrm(pss->get_ssid(), pkg._seqno, FRMCMD_SYNR, nullptr, 0, frmret, sizeof(frmret));
							_udpsend(fd, paddr, addrlen, frmret, ns, _udpsend_param, false); //应答一个SYNR握手成功信息,解决主通道握手应答可能丢包的场景
						}
						return 0; //已经存在
					}
					
					uint32_t unxtid = (mknxtid() << 16) | (pkg._ussid & 0xFFFF); //客户端的ssid在低16位
					pss = new ucpsocket(unxtid, ucp_conin);//接入的，需要保留对端地址信息
					if (!pss)
						return -1;
					pss->addudp(fd, paddr, addrlen);
					memcpy(pss->_guid, pkg._data, 16);
					
					int ns = frmpkg::mkfrm(unxtid, pkg._seqno, FRMCMD_SYNR, nullptr, 0, frmret, sizeof(frmret));
					_udpsend(fd, paddr, addrlen, frmret, ns, _udpsend_param, false); //应答一个SYNR握手成功信息

					_map.set(pss->get_ssid(), pss);
					ec::net::socketaddr peeraddr;
					peeraddr.set(paddr, addrlen);
					_plog->add(CLOG_DEFAULT_MSG, "fd(%d) new connect ssid(%XH), from udp://%s:%u", fd, unxtid,
						peeraddr.viewip(), peeraddr.port());
					onconnect(unxtid, 0); //连入成功
					return 0;
				}
				else if (FRMCMD_SYNR == pkg._frmcmd) { //异步连接成功,作为客户端
					ec::net::socketaddr peeraddr;
					peeraddr.set(paddr, addrlen);
					PSOCKET ps = nullptr;
					if (!_mapcon.get(pkg._ussid & 0xFFFF, ps)) {
						if (_map.get(pkg._ussid, ps)) {
							_plog->add(CLOG_DEFAULT_DBG, "fd(%d) recv SYNR from udp://%s:%u, ssid(%08XH) 2rd.", fd,
								peeraddr.viewip(), peeraddr.port(), pkg._ussid);
							ps->addudp(fd, paddr, addrlen);
						}
						else
							_plog->add(CLOG_DEFAULT_DBG, "fd(%d) recv SYNR from %s:%u, ssid(%08XH) not exist.", fd, 
								peeraddr.viewip(), peeraddr.port(), pkg._ussid);
						return 0;
					}
					_plog->add(CLOG_DEFAULT_DBG, "fd(%d) recv SYNR from %s:%u, ssid(%08XH) connect success.", fd,
						peeraddr.viewip(), peeraddr.port(), pkg._ussid);

					ps->_time_lastread = ec::mstime();
					_mapcon.erase(pkg._ussid & 0xFFFF);
					ps->set_ssid(pkg._ussid);
					ps->addudp(fd, paddr, addrlen);
					_map.set(ps->get_ssid(), ps);
					onconnect(ps->get_ssid(), 1); //连出成功
					return 0;
				}

				int nr = 0;
				PSOCKET ps = nullptr;
				if (!_map.get(pkg._ussid, ps)) { //不存在的ssid,发送断开报文
					if (pkg._frmcmd != FRMCMD_FIN) {
						int ns = frmpkg::mkfrm(pkg._ussid, pkg._seqno, FRMCMD_FIN, nullptr, 0, frmret, sizeof(frmret));
						_udpsend(fd, paddr, addrlen, frmret, ns, _udpsend_param, false);
					}
					return 0;
				}
				if (paddr && 0 == ps->_sstype) //接入的才拷贝对端地址
					ps->addudp(fd, paddr, addrlen);
				ps->_time_lastread = ec::mstime();
				switch (pkg._frmcmd) {
				case FRMCMD_HRT:
					break;
				case FRMCMD_DAT:
				case FRMCMD_DATR:
				case FRMCMD_ACK:
					if (ps->dorevc(fd, paddr, addrlen, pkg, pfrm, size, _plog, outfrms) < 0) {
						uint32_t ussid = pkg._ussid;
						_map.erase(ussid, [&](PSOCKET& p) {
							if (p) {
								delete p;
								p = nullptr;
								ondisconnected(ussid, ucp_disconnect_err);//错误断开通知
							}
						});
						nr = -1;
					}
					ussid = pkg._ussid;
					break;
				case FRMCMD_FIN:
					uint32_t ussid = pkg._ussid;
					_map.erase(ussid, [&](PSOCKET& p) {
						if (p) {
							delete p;
							p = nullptr;
							ondisconnected(ussid, ucp_disconnect_fin);//断开通知
						}
					});
					break;
				}
				if (nr < 0) {
					int ns = frmpkg::mkfrm(pkg._ussid, pkg._seqno, FRMCMD_FIN, nullptr, 0, frmret, sizeof(frmret));
					_udpsend(fd, paddr, addrlen, frmret, ns, _udpsend_param, false);
					return 0;
				}
				return nr;
			}

			/*!
			* brief 发送数据，多通道
			* param ssid 会话(连接)ID
			* param pdata 数据
			* param size 数据长度
			* return >=0返回发送的数据字节数, -1表示错误
			*/
			int sendbytes(uint32_t ssid, const void* pdata, size_t size)
			{
				PSOCKET ps = nullptr;
				if (!_map.get(ssid, ps))
					return -1;
				return ps->sendbytes(pdata, size, _udpsend, _udpsend_param);
			}

			/*!
			* brief 运行时，定时调用，建议5-10毫秒调用一次
			* param curmsec当前时标，gmt 1970-1-1开始的毫秒数
			*/
			void runtime(int64_t curmsec, int interval) //运行时，定时调用，主要时重发, curmsec为当前时间毫秒数
			{
				int nfl;
				uint8_t frmout[SIZE_UDPFRM];
				if (_lastruntime + interval > curmsec && _lastruntime <= curmsec)
					return;
				_lastruntime = curmsec;

				for (auto& i : _map) { //批量确认,双通道发送
					nfl = i->ack(frmout, sizeof(frmout));
					if (nfl < 0)
						_plog->add(CLOG_DEFAULT_ERR, "ssid(%08XH) acks error.", i->get_ssid());
					else if (nfl > 0) {
						i->sendfrm(frmout, nfl, _udpsend, _udpsend_param);
					}
				}

				int ncnt = 0, nfrms = 0;
				int as_timeover = 0, as_seqo = 0;
				_delssids.clear();
				//处理重发
				for (auto& i : _map) {
					ncnt = 0;
					as_timeover = 0;
					as_seqo = 0;
					nfrms = i->resend(curmsec, ncnt, _udpsend, _udpsend_param, _resendackno, (int)_resendtime,16, as_timeover, as_seqo);
					if (nfrms > 0)
						_plog->add(CLOG_DEFAULT_DBG, "ssid(%08XH) resend %d frames. timeover=%d, seqnoout=%d max resend counts %d, sndbuf %zu",
							i->get_ssid(), nfrms, as_timeover, as_seqo, ncnt - 1, i->sndbufsize());

					if (ncnt > (int)_maxresendcnt) //重发次数
						_delssids.push_back(i->get_ssid());
				}

				//断开重发超时的。
				for (auto& i : _delssids) {
					PSOCKET ps = nullptr;
					if (_map.get(i, ps)) {
						_map.erase(i, [&](PSOCKET& p) {
							if (p) {
								delete p;
								p = nullptr;
								ondisconnected(i, ucp_disconnect_timeout);
							}
						});
					}
				}

				//处理确心跳
				for (auto& i : _map) {
					if (curmsec - i->_time_lastsend > 20 * 1000 || curmsec < i->_time_lastsend) {
						int ns = frmpkg::mkfrm(i->get_ssid(), 0, FRMCMD_HRT, nullptr, 0, frmout, sizeof(frmout));
						if(ns >0)
							i->sendfrm(frmout, ns, _udpsend, _udpsend_param);
						i->_time_lastsend = curmsec;
					}
				}
			}

			bool acked(const void* pfrm, size_t size)//判断是否已经确认,对于优化慢速通道不再发送.
			{
				frmpkg pkg;
				if (pkg.parsepkghead(pfrm, size) < 0)
					return false;
				if (FRMCMD_DAT != pkg._frmcmd && FRMCMD_DATR != pkg._frmcmd)
					return false;
				PSOCKET ps = nullptr;
				if (!_map.get(pkg._ussid, ps))
					return false;
				return ps->acked(pkg._seqno);
			}
		};// class ucp
	}//namespace ucp
}// namespace ec