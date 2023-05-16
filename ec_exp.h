/*!
\file ec_exp.h

eclib for sample Expression calculation class, new version. support functions and variables

\author	jiangyong
\email  kipway@outlook.com
\update 2023.5.15

eclib Copyright (c) 2017-2023, kipway
source repository : https://github.com/kipway/eclib

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

*/
#pragma once
#include <functional>
#include "ec_vector.hpp"
#include "ec_stack.h"
#include "ec_string.hpp"
namespace ec
{
	class exp
	{
	public:
#if (0 != USE_EC_STRING)
		using STR_ = ec::string;
#else
		using STR_ = std::string;
#endif
		enum node_type {
			node_data = 0, //data
			node_var, //variable
			node_fun, //function
			node_opt, //operator char
			node_left //left (
		};
		enum data_type {
			dt_int = 0,
			dt_double,
			dt_string
		};
		enum opt_type {
			OPT_NULL = (-1),// error
			OPT_LOGOR = 0,	// || level 0  left to right
			OPT_LOGAND,		// && level 1  left to right

			OPT_BITOR,		// |  level 2  left to right
			OPT_BITXOR,		// ^  level 3  left to right
			OPT_BITAND,		// &  level 4  left to right

			OPT_EQ,			// == level 5
			OPT_NOTEQ,		// != level 5

			OPT_LESS,		// <  level 6  left to right
			OPT_LESSEQ,		// <= level 6  left to right
			OPT_GREAT,		// >  level 6  left to right
			OPT_GREATEQ,	// >= level 6  left to right

			OPT_LBIT,		// << level 7  left to right
			OPT_RBIT,		// >> level 7  left to right

			OPT_ADD,		// +  level 8  left to right
			OPT_DEC,		// -  level 8  left to right

			OPT_MULT,		// *  level 9  left to right
			OPT_DIV,		// /  level 9  left to right
			OPT_MOD,		// %  level 9  left to right

			OPT_UNVAL,		// 负 level 10 right to left
			OPT_BITNOT,		// ~  level 10 right to left
			OPT_LOGNOT		// !  level 10 right to left
		};
		class val_
		{
		public:
			union uval {
				int64_t iv;
				double  fv;
			}_v;
			STR_  _str;
			int dtype;
			val_() {
				_v.iv = 0;
				dtype = dt_int;
			}
			val_(double v) {
				_v.fv = v;
				dtype = dt_double;
			}
			val_(int v) {
				_v.iv = v;
				dtype = dt_int;
			}
			val_(int64_t v) {
				_v.iv = v;
				dtype = dt_int;
			}
			~val_() {
			}
			template<typename T>
			void getval(T &v) {
				switch (dtype) {
				case dt_int:
					v = (T)(_v.iv);
					break;
				case dt_double:
					v = (T)(_v.fv);
					break;
				}
			}
			inline double getdbl() {
				double v = 0;
				getval(v);
				return v;
			}
			inline int64_t getint64() {
				int64_t v = 0;
				getval(v);
				return v;
			}
			void prtval(bool endline = true) {
				if (dt_int == dtype)
					printf("%lld", (long long)_v.iv);
				else if (dt_double == dtype)
					printf("%f", _v.fv);
				else if (dt_string == dtype)
					printf("%s", _str.c_str());
				else
					printf("OPT:%lld", (long long)_v.iv);
				if (endline)
					printf("\n");
			}
		};
		using ExtFunArgs = ec::vector<val_>;
		class inode_
		{
		public:
			inode_() {
			}
			virtual ~inode_() {
			}
			_USE_EC_OBJ_ALLOCATOR
		public:
			virtual int type() = 0;
			virtual bool getval(val_ &v, std::function<int(const char* varname, val_&var)>getvar) {
				return false;
			}
			virtual int  opt_level() {
				return -1;
			}
			virtual int  opt() {
				return -1;
			}
		};
		using nodePtrVector = ec::vector<inode_*>;
		using funExpArgs = ec::vector<nodePtrVector>;
		class node_left_ : public inode_ // left (
		{
		public:
			node_left_() {}
			virtual int type() {
				return node_left;
			};
		};
		class node_data_ : public inode_
		{
		public:
			node_data_() {
			}
			node_data_(int64_t v) {
				_val.dtype = dt_int;
				_val._v.iv = v;
			}
			node_data_(double v) {
				_val.dtype = dt_double;
				_val._v.fv = v;
			}
			node_data_(STR_ &v) {
				_val.dtype = dt_string;
				_val._str = v;
			}
			virtual ~node_data_() {
			}
		protected:
			val_ _val;
		public:
			virtual int type() {
				return node_data;
			};
			virtual bool getval(val_ &v, std::function<int(const char* varname, val_&var)>getvar) {
				v = _val;
				return true;
			};
		};
		class node_var_ : public inode_
		{
		public:
			node_var_(const char* sname) {
				_name = sname;
			}
		protected:
			STR_ _name;
		public:
			virtual int type() {
				return node_var;
			};
			virtual bool getval(val_ &v, std::function<int(const char* varname, val_&var)>getvar) {
				return 0 == getvar(_name.c_str(), v);
			};
		};
		class node_fun_ : public inode_
		{
		public:
			node_fun_() {
			}
			virtual ~node_fun_() {
				for (auto &i : _args) {
					for (auto &p : i)
						delete p;
					i.clear();
				}
				_args.clear();
			}
			STR_ _name;
			funExpArgs _args;
		public:
			virtual int type() {
				return node_fun;
			};
		};
		class node_opt_ : public inode_
		{
		public:
			node_opt_() {
				_opt = -1;
			}
			node_opt_(int opt) :_opt(opt) {
			}
		protected:
			int _opt;
		public:
			virtual int type() {
				return node_opt;
			};
			virtual int  opt_level() {
				return optlevel(_opt);
			}
			virtual int  opt() {
				return _opt;
			}
		};
		static bool ishex16(const char* s)
		{
			int nl = (int)strlen(s);
			if (nl < 3)
				return false;
			char c;
			if (*s == '0' && (*(s + 1) == 'x' || *(s + 1) == 'X')) {
				s += 2;
				while (*s) {
					c = *s;
					if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
						s++;
						continue;
					}
					else
						return false;
				}
				return true;
			}
			return false;
		}
		static bool isnumber(const char* s)
		{
			char c;
			int ndot = 0;
			while (*s) {
				c = *s;
				if ((c < '0' || c > '9') && c != '.')
					return false;
				if (c == '.') {
					if (ndot > 0)
						return false;
					ndot++;
				}
				s++;
			}
			return true;
		}
		static int64_t hex162ll(const char* s)
		{
			const char *pc = s;
			char c;
			uint64_t dwval = 0, dwt;
			int k = 0, n = (int)strlen(s);
			n--;
			while (n > 1) {
				dwt = 0;
				c = pc[n];
				if (c >= '0' && c <= '9')
					dwt = c - '0';
				else if (c >= 'a' && c <= 'f')
					dwt = 0x0A + c - 'a';
				else if (c >= 'A' && c <= 'F')
					dwt = 0x0A + c - 'A';
				dwt <<= (k * 4);
				dwval += dwt;
				k++;
				n--;
			}
			return (int64_t)dwval;
		}
		static int	GetOptType(char c)
		{
			if (c == '|')	return OPT_BITOR;
			else if (c == '^')	return OPT_BITXOR;
			else if (c == '&')	return OPT_BITAND;
			else if (c == '<')	return OPT_LESS;
			else if (c == '>')	return OPT_GREAT;
			else if (c == '+')	return OPT_ADD;
			else if (c == '-')	return OPT_DEC;
			else if (c == '*')	return OPT_MULT;
			else if (c == '/')	return OPT_DIV;
			else if (c == '%')	return OPT_MOD;
			else if (c == '~')	return OPT_BITNOT;
			return OPT_NULL;
		}
		static int	optlevel(int opt)
		{
			int nr = 0;
			switch (opt) {
			case OPT_LOGOR: nr = 0; break;
			case OPT_LOGAND: nr = 1; break;
			case OPT_BITOR: nr = 2; break;
			case OPT_BITXOR: nr = 3; break;
			case OPT_BITAND: nr = 4; break;

			case OPT_EQ:
			case OPT_NOTEQ:
				nr = 5;
				break;
			case OPT_LESS:
			case OPT_LESSEQ:
			case OPT_GREAT:
			case OPT_GREATEQ:
				nr = 6;
				break;
			case OPT_LBIT:
			case OPT_RBIT:
				nr = 7;
				break;
			case OPT_ADD:
			case OPT_DEC:
				nr = 8;
				break;
			case OPT_MULT:
			case OPT_DIV:
			case OPT_MOD:
				nr = 9;
				break;
			case OPT_UNVAL:
			case OPT_BITNOT:
			case OPT_LOGNOT:
				nr = 10;
				break;
			default:
				nr = 0;
				break;
			}
			return nr;
		}
	private:
		void parse_outvar(STR_& skey, nodePtrVector& vout, int& prenode)  const
		{
			if (isnumber(skey.c_str())) {
				if (strchr(skey.c_str(), '.'))
					vout.push_back(new node_data_(atof(skey.c_str())));
				else
					vout.push_back(new node_data_((int64_t)atoll(skey.c_str())));
				prenode = node_data;
			}
			else if (ishex16(skey.c_str())) {
				vout.push_back(new node_data_(hex162ll(skey.c_str())));
				prenode = node_data;
			}
			else {
				vout.push_back(new node_var_(skey.c_str()));
				prenode = node_var;
			}
			skey.clear();
		}
		void clearstack(ec::stack<inode_*> &stk)  const
		{
			while (!stk.empty()) {
				delete stk.top();
				stk.pop();
			}
		}
		using const_strp = const char *;
		bool getfunexpstr(const_strp& ps, const_strp& pend, STR_& exp) const
		{
			int ny = 0, nk = 1;
			exp.clear();
			ps++;
			while (ps < pend && nk) {
				if (0 == ny % 2) {
					if (*ps == '(')
						++nk;
					else if (*ps == ')') {
						--nk;
						if (!nk)
							return true;
					}
				}
				if (*ps == '"')
					++ny;
				exp += *ps++;
			}
			return false;
		}
		bool parsefunexp(const char* sexp, funExpArgs& args)  const
		{
			const char* pc = sexp;
			const char* ps = sexp;
			int ny = 0, nk = 0;
			while (*pc) {
				switch (*pc) {
				case '"':
					++ny;
					break;
				case '(':
					++nk;
					break;
				case ')':
					--nk;
					break;
				case ',':
					if (!(ny % 2) && !nk) {
						nodePtrVector arg;
						if (parse(ps, pc - ps, arg))
							return false;
						if (!arg.empty())
							args.push_back(std::move(arg));
						ps = pc + 1;
					}
					break;
				}
				++pc;
			}
			if (pc > ps) {
				nodePtrVector arg;
				if (parse(ps, pc - ps, arg))
					return false;
				if (!arg.empty())
					args.push_back(std::move(arg));
			}
			return true;
		}
		bool getstring(const_strp& ps, const_strp& pend, STR_& exp) const
		{
			exp.clear();
			ps++;
			while (ps < pend) {
				if (*ps == '\\') {
					if (ps + 1 == pend)
						return true;
					switch (*(ps + 1)) {
					case 't':
						exp += '\t';
						break;
					case 'n':
						exp += '\n';
						break;
					case '\'':
						exp += '\'';
						break;
					case '"':
						exp += '"';
						break;
					case 'r':
						exp += '\r';
						break;
					case '\\':
						exp += '\\';
						break;
					}
					++ps;
					++ps;
				}
				else if (*ps == '"')
					return true;
				else
					exp += *ps++;
			}
			return false;
		}
	public:
		int parse(const char* s, size_t size, nodePtrVector& vout) const
		{
			const char *ps = s, *pend = s + size;
			int np1, np2;
			int prenode = node_left;
			STR_ skey = "", stmp;
			char c;
			ec::stack<inode_*> optstk;
			while (ps < pend) {
				c = *ps;
				if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
					ps++;
					continue;
				}
				else if (c == '(') {
					if (skey.length() > 0) { // parse function
						node_fun_ *pfun = new node_fun_();
						pfun->_name = skey;
						if (!getfunexpstr(ps, pend, stmp) ||
							!parsefunexp(stmp.c_str(), pfun->_args)
							) {
							delete pfun;
							clearstack(optstk);
							return -1;
						}
						vout.push_back(pfun);
						skey.clear();
					}
					else {
						optstk.push(new node_left_());
						prenode = node_left;
					}
				}
				else if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '<' || c == '>' || c == '&' || c == '|' || c == '~' || c == '^' || c == '=' || c == '!') {
					if (skey.size()) // number or variant
						parse_outvar(skey, vout, prenode);
					int opt = OPT_NULL;
					if (c == '<') {
						if (*(ps + 1) == '=') { // <=
							ps++;
							opt = OPT_LESSEQ;
						}
						else if (*(ps + 1) == '<') { // <<
							ps++;
							opt = OPT_LBIT;
						}
						else
							opt = OPT_LESS;
					}
					else if (c == '>') {
						if (*(ps + 1) == '=') { // >=
							ps++;
							opt = OPT_GREATEQ;
						}
						else if (*(ps + 1) == '>') { // >>
							ps++;
							opt = OPT_RBIT;
						}
						else
							opt = OPT_GREAT;
					}
					else if (c == '|') {
						if (*(ps + 1) == '|') { // ||
							ps++;
							opt = OPT_LOGOR;
						}
						else
							opt = OPT_BITOR;
					}
					else if (c == '&') {
						if (*(ps + 1) == '&') { // &&
							ps++;
							opt = OPT_LOGAND;
						}
						else
							opt = OPT_BITAND;
					}
					else if (c == '-') {
						if (prenode == node_opt || prenode == node_left)
							opt = OPT_UNVAL; // -负
						else
							opt = OPT_DEC; // - 减
					}
					else if (c == '=') {
						if (*(ps + 1) == '=') { // ==
							ps++;
							opt = OPT_EQ;
						}
						else {
							clearstack(optstk);
							return -1;
						}
					}
					else if (c == '!') {
						if (*(ps + 1) == '=') { // !=
							ps++;
							opt = OPT_NOTEQ;
						}
						else
							opt = OPT_NOTEQ;;
					}
					else
						opt = GetOptType(c);

					if (OPT_NULL == opt) {
						clearstack(optstk);
						return -1;
					}
					prenode = node_opt;
					np1 = optlevel(opt);
					while (!optstk.empty()) {
						if (optstk.top()->type() == node_left) { // left (
							break;
						}
						np2 = optstk.top()->opt_level();
						if (np2 < np1 || (np2 == np1 && np2 >= OPT_UNVAL)) // < OR (== and level right->left
							break;
						vout.push_back(optstk.top());
						optstk.pop();
					}
					optstk.push(new node_opt_(opt));
				}
				else if (c == ')') { // pop and out until (
					if (skey.size())
						parse_outvar(skey, vout, prenode);

					bool bkp = false;
					while (!optstk.empty()) {
						if (optstk.top()->type() == node_left) {
							optstk.pop();
							bkp = true;
							break;
						}
						vout.push_back(optstk.top());
						optstk.pop();
					}
					if (!bkp) {
						clearstack(optstk);
						return -1;
					}
				}
				else if (c == '"') {
					if (!getstring(ps, pend, stmp)) {
						clearstack(optstk);
						return -1;
					}
					optstk.push(new node_data_(stmp));
				}
				else
					skey += c;
				ps++;
			}// while(ps)
			if (skey.size())
				parse_outvar(skey, vout, prenode);
			while (!optstk.empty()) {// pop all to out
				if (optstk.top()->type() == node_left) {
					clearstack(optstk);
					return -1;
				}
				vout.push_back(optstk.top());
				optstk.pop();
			}
			return 0;
		}

		using fun_getvar = std::function<int(const char* varname, val_&var)>;
		int eval(nodePtrVector& nodes
			, std::function<int(const char* varname, val_&var)>getvar
			, std::function<int(node_fun_* pfun, fun_getvar getvar, val_&var)>dofunction
			, val_ &vret
		) const
		{
			ec::stack<val_> stk;
			int  nodetype;
			val_ v;
			bool bret;
			for (auto i = 0u; i < nodes.size(); i++) {
				nodetype = nodes[i]->type();
				if (node_data == nodetype
					|| node_var == nodetype
					) {
					if (!nodes[i]->getval(v, getvar))
						return -1;
					stk.push(std::move(v));
				}
				else if (node_fun == nodetype) {
					if (0 == dofunction((node_fun_*)nodes[i], getvar, v))
						stk.push(std::move(v));
					else
						return -1;
				}
				else if (node_opt == nodetype) {
					bret = false;
					switch (nodes[i]->opt()) {
					case OPT_LOGOR: // ||
						bret = opt_logor(stk);
						break;
					case OPT_LOGAND: // &&
						bret = opt_logand(stk);
						break;
					case OPT_BITOR: // |
						bret = opt_bitor(stk);
						break;
					case OPT_BITXOR: // ^
						bret = opt_bitxor(stk);
						break;
					case OPT_BITAND: // &
						bret = opt_bitand(stk);
						break;
					case OPT_EQ: // ==
						bret = opt_eq(stk);
						break;
					case OPT_NOTEQ:	// !=
						bret = opt_noteq(stk);
						break;
					case OPT_LESS: // <
						bret = opt_less(stk);
						break;
					case OPT_LESSEQ: // <=
						bret = opt_lesseq(stk);
						break;
					case OPT_GREAT: // >
						bret = opt_great(stk);
						break;
					case OPT_GREATEQ: // >=
						bret = opt_greateq(stk);
						break;
					case OPT_LBIT: // <<
						bret = opt_lbit(stk);
						break;
					case OPT_RBIT: // >>
						bret = opt_rbit(stk);
						break;
					case OPT_ADD: // +
						bret = opt_add(stk);
						break;
					case OPT_DEC: // -
						bret = opt_dec(stk);
						break;
					case OPT_MULT: // *
						bret = opt_mult(stk);
						break;
					case OPT_DIV: // /
						bret = opt_div(stk);
						break;
					case OPT_MOD: // %
						bret = opt_mod(stk);
						break;
					case OPT_UNVAL: // - (负)
						bret = opt_unval(stk);
						break;
					case OPT_BITNOT: // ~
						bret = opt_bitnot(stk);
						break;
					case OPT_LOGNOT: // !
						bret = opt_lognot(stk);
						break;
					}
					if (!bret)
						return -1;
				}
			}
			if (stk.size() != 1u)
				return -1;
			vret = stk.top();
			return 0;
		}

	private:
		bool pop1(ec::stack<val_> &stk, val_& v) const
		{
			if (stk.empty())
				return false;
			v = stk.top();
			stk.pop();
			return true;
		}
		bool pop2(ec::stack<val_> &stk, val_& v1, val_& v2) const
		{
			if (stk.size() < 2)
				return false;
			v2 = stk.top();
			stk.pop();
			v1 = stk.top();
			stk.pop();
			return true;
		}
		bool opt_logor(ec::stack<val_> &stk) const // ||
		{
			val_ v1, v2;
			if (!pop2(stk, v1, v2) || dt_string == v1.dtype || dt_string == v2.dtype)
				return false;
			if (v1.dtype == v2.dtype && v1.dtype == dt_int)
				stk.push(val_(v1.getint64() || v2.getint64() ? 1 : 0));
			else
				stk.push(val_(v1.getdbl() || v2.getdbl() ? 1 : 0));
			return true;
		}
		bool opt_logand(ec::stack<val_> &stk) const // &&
		{
			val_ v1, v2;
			if (!pop2(stk, v1, v2) || dt_string == v1.dtype || dt_string == v2.dtype)
				return false;
			if (v1.dtype == v2.dtype && v1.dtype == dt_int)
				stk.push(val_(v1.getint64() && v2.getint64() ? 1 : 0));
			else
				stk.push(val_(v1.getdbl() && v2.getdbl() ? 1 : 0));
			return true;
		}
		bool opt_bitor(ec::stack<val_> &stk) const // |
		{
			val_ v1, v2;
			if (!pop2(stk, v1, v2) || dt_string == v1.dtype || dt_string == v2.dtype)
				return false;
			stk.push(val_(v1.getint64() | v2.getint64()));
			return true;
		}
		bool opt_bitxor(ec::stack<val_> &stk) const // ^
		{
			val_ v1, v2;
			if (!pop2(stk, v1, v2) || dt_string == v1.dtype || dt_string == v2.dtype)
				return false;
			stk.push(val_(v1.getint64() ^ v2.getint64()));
			return true;
		}
		bool opt_bitand(ec::stack<val_> &stk)  const // &
		{
			val_ v1, v2;
			if (!pop2(stk, v1, v2) || dt_string == v1.dtype || dt_string == v2.dtype)
				return false;
			stk.push(val_(v1.getint64() & v2.getint64()));
			return true;
		}
		bool opt_eq(ec::stack<val_> &stk) const // ==
		{
			val_ v1, v2;
			if (!pop2(stk, v1, v2) || dt_string == v1.dtype || dt_string == v2.dtype)
				return false;
			if (v1.dtype == v2.dtype && v1.dtype == dt_int)
				stk.push(val_(v1.getint64() == v2.getint64() ? 1 : 0));
			else
				stk.push(val_(v1.getdbl() == v2.getdbl() ? 1 : 0));
			return true;
		}
		bool opt_noteq(ec::stack<val_> &stk) const // !=
		{
			val_ v1, v2;
			if (!pop2(stk, v1, v2) || dt_string == v1.dtype || dt_string == v2.dtype)
				return false;
			if (v1.dtype == v2.dtype && v1.dtype == dt_int)
				stk.push(val_(v1.getint64() != v2.getint64() ? 1 : 0));
			else
				stk.push(val_(v1.getdbl() != v2.getdbl() ? 1 : 0));
			return true;
		}
		bool opt_less(ec::stack<val_> &stk) const // <
		{
			val_ v1, v2;
			if (!pop2(stk, v1, v2) || dt_string == v1.dtype || dt_string == v2.dtype)
				return false;
			if (v1.dtype == v2.dtype && v1.dtype == dt_int)
				stk.push(val_(v1.getint64() < v2.getint64() ? 1 : 0));
			else
				stk.push(val_(v1.getdbl() < v2.getdbl() ? 1 : 0));
			return true;
		}
		bool opt_lesseq(ec::stack<val_> &stk) const // <=
		{
			val_ v1, v2;
			if (!pop2(stk, v1, v2) || dt_string == v1.dtype || dt_string == v2.dtype)
				return false;
			if (v1.dtype == v2.dtype && v1.dtype == dt_int)
				stk.push(val_(v1.getint64() <= v2.getint64() ? 1 : 0));
			else
				stk.push(val_(v1.getdbl() <= v2.getdbl() ? 1 : 0));
			return true;
		}
		bool opt_great(ec::stack<val_> &stk)  const// >
		{
			val_ v1, v2;
			if (!pop2(stk, v1, v2) || dt_string == v1.dtype || dt_string == v2.dtype)
				return false;
			if (v1.dtype == v2.dtype && v1.dtype == dt_int)
				stk.push(val_(v1.getint64() > v2.getint64() ? 1 : 0));
			else
				stk.push(val_(v1.getdbl() > v2.getdbl() ? 1 : 0));
			return true;
		}
		bool opt_greateq(ec::stack<val_> &stk) const // >=
		{
			val_ v1, v2;
			if (!pop2(stk, v1, v2) || dt_string == v1.dtype || dt_string == v2.dtype)
				return false;
			if (v1.dtype == v2.dtype && v1.dtype == dt_int)
				stk.push(val_(v1.getint64() >= v2.getint64() ? 1 : 0));
			else
				stk.push(val_(v1.getdbl() >= v2.getdbl() ? 1 : 0));
			return true;
		}
		bool opt_lbit(ec::stack<val_> &stk) const //  <<
		{
			val_ v1, v2;
			if (!pop2(stk, v1, v2) || dt_string == v1.dtype || dt_string == v2.dtype)
				return false;
			stk.push(val_(v1.getint64() << v2.getint64()));
			return true;
		}
		bool opt_rbit(ec::stack<val_> &stk) const //  >>
		{
			val_ v1, v2;
			if (!pop2(stk, v1, v2) || dt_string == v1.dtype || dt_string == v2.dtype)
				return false;
			stk.push(val_(v1.getint64() >> v2.getint64()));
			return true;
		}
		bool opt_add(ec::stack<val_> &stk)  const // +
		{
			val_ v1, v2;
			if (!pop2(stk, v1, v2) || dt_string == v1.dtype || dt_string == v2.dtype)
				return false;
			if (v1.dtype == v2.dtype && v1.dtype == dt_int)
				stk.push(val_(v1.getint64() + v2.getint64()));
			else
				stk.push(val_(v1.getdbl() + v2.getdbl()));
			return true;
		}
		bool opt_dec(ec::stack<val_> &stk)  const // -
		{
			val_ v1, v2;
			if (!pop2(stk, v1, v2) || dt_string == v1.dtype || dt_string == v2.dtype)
				return false;
			if (v1.dtype == v2.dtype && v1.dtype == dt_int)
				stk.push(val_(v1.getint64() - v2.getint64()));
			else
				stk.push(val_(v1.getdbl() - v2.getdbl()));
			return true;
		}
		bool opt_mult(ec::stack<val_> &stk)  const // *
		{
			val_ v1, v2;
			if (!pop2(stk, v1, v2) || dt_string == v1.dtype || dt_string == v2.dtype)
				return false;
			if (v1.dtype == v2.dtype && v1.dtype == dt_int)
				stk.push(val_(v1.getint64() * v2.getint64()));
			else
				stk.push(val_(v1.getdbl() * v2.getdbl()));
			return true;
		}
		bool opt_div(ec::stack<val_> &stk) const  // /
		{
			val_ v1, v2;
			if (!pop2(stk, v1, v2) || dt_string == v1.dtype || dt_string == v2.dtype)
				return false;
			if (v1.dtype == v2.dtype && v1.dtype == dt_int) {
				if (!v2.getint64())
					return false;
				stk.push(val_(v1.getint64() / v2.getint64()));
			}
			else {
				if (!v2.getdbl())
					return false;
				stk.push(val_(v1.getdbl() / v2.getdbl()));
			}
			return true;
		}
		bool opt_mod(ec::stack<val_> &stk)  const // %
		{
			val_ v1, v2;
			if (!pop2(stk, v1, v2) || dt_string == v1.dtype || dt_string == v2.dtype)
				return false;
			if (!v2.getint64())
				return false;
			stk.push(val_(v1.getint64() % v2.getint64()));
			return true;
		}
		bool opt_unval(ec::stack<val_> &stk)  const // - (负)
		{
			val_ v1;
			if (!pop1(stk, v1) || dt_string == v1.dtype)
				return false;
			if (v1.dtype == dt_double)
				stk.push(val_(-1 * v1.getdbl()));
			else
				stk.push(val_(-1 * v1.getint64()));
			return true;
		}
		bool opt_bitnot(ec::stack<val_> &stk)  const // ~
		{
			val_ v1;
			if (!pop1(stk, v1) || dt_string == v1.dtype)
				return false;
			stk.push(val_(~v1.getint64()));
			return true;
		}
		bool opt_lognot(ec::stack<val_> &stk) const  // !
		{
			val_ v1;
			if (!pop1(stk, v1) || dt_string == v1.dtype)
				return false;
			if (v1.dtype == dt_double)
				stk.push(val_(v1.getdbl() ? 0 : 1));
			else
				stk.push(val_(v1.getint64() ? 0 : 1));
			return true;
		}
	}; // exp

	class doexp {
	public:
		doexp(const doexp&) = delete;
		doexp& operator = (const doexp&) = delete;
		doexp() {
		}
		int parse(const char* str, size_t size)
		{
			exp e;
			clear();
			return e.parse(str, size, _vo);
		}
		using fun_getvar = std::function<int(const char* varname, exp::val_& var)>;
		using fun_dofun = std::function<int(const char*sname, exp::ExtFunArgs& args, exp::val_& valout)>;
		int eval(fun_getvar getvar, fun_dofun dofun, exp::val_ &vout) {
			exp e;
			return e.eval(_vo, getvar, [&](exp::node_fun_* pfun, fun_getvar getvar, exp::val_& valout) {
				return dofunexp(pfun, getvar, dofun, valout);
			}, vout);
		}
		virtual ~doexp() {
			clear();
		}
		bool empty() {
			return _vo.empty();
		}
		doexp& operator = (doexp&& v) // for move
		{
			clear();
			_vo = std::move(v._vo);
			return *this;
		}
	private:
		exp::nodePtrVector _vo;
		void clear() {
			for (auto &i : _vo)
				delete i;
			_vo.clear();
		}
		int dofunexp(exp::node_fun_* pfun, fun_getvar getvar, fun_dofun dofunction, exp::val_ &varout)
		{
			exp::ExtFunArgs expvals;
			for (auto i = 0u; i < pfun->_args.size(); i++) {
				ec::exp::val_ vret;
				ec::exp exp;
				if (0 != exp.eval(pfun->_args[i], getvar,
					[&](exp::node_fun_* pfunc, fun_getvar getvarc, exp::val_& valfun) {
					return dofunexp(pfunc, getvarc, dofunction, valfun);
				}, vret))
					return -1;
				expvals.push_back(vret);
			}
			return dofunction(pfun->_name.c_str(), expvals, varout);;
		}
	};
}

/*
int getvar(const char* svar, ec::exp::val_ &var)
{
	if (ec::strieq("x", svar)) {
		var.dtype = ec::exp::dt_int;
		var._v.iv = 10;
		return 0;
	}
	return -1;
}

int dofun(const char*sname, ec::exp::ExtFunArgs &args, ec::exp::val_& valout)
{
	if (ec::strieq("add", sname)) {
		if (args.size() != 2)
			return -1;
		if (args[0].dtype != args[1].dtype) {
			valout.dtype = ec::exp::dt_double;
			valout._v.fv = args[0].getdbl() + args[1].getdbl();
		}
		else {
			valout.dtype = ec::exp::dt_int;
			valout._v.iv = args[0].getint64() + args[1].getint64();
		}
	}
	else if (ec::strieq("atoi", sname)) {
		if (args.size() != 1 || ec::exp::dt_string != args[0].dtype)
			return -1;
		valout.dtype = ec::exp::dt_int;
		valout._v.iv = atoll(args[0]._str.c_str());
	}
	else if (ec::strieq("fork", sname)) // simulate if else
	{
		if (args.size() != 3)
			return -1;
		valout = args[0].getint64() ? args[1] : args[2];
	}
	else
		return -1;
	return 0;
}

int main()
{
	std::string sexp = "2+3*5+x+add(1,x+atoi(\"356\"))"; // x=10
//	std::string sexp = "2+3*5+x+add(1,x+fork(x>0,10,-10))"; // x=10

	ec::doexp exp;
	if (exp.parse(sexp.data(), sexp.size())) {
		printf("%s parse failed.", sexp.c_str());
		return 0;
	}
	ec::exp::val_ vret;
	if (exp.eval(getvar, dofun, vret)) {
		printf("%s eval failed.\n", sexp.c_str());
		return 0;
	}
	printf("%s = ", sexp.c_str());
	vret.prtval();
	return 0;
}
*/