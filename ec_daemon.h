/*!
\file ec_daemon.h
\author	jiangyong
\author kipway@outlook.com
\update 2020.9.6

daemon
	a class  for linux daemon server

eclib 3.0 Copyright (c) 2017-2020, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/

#pragma once

#ifndef _WIN32
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <mntent.h>
#include<sys/types.h>
#include<fcntl.h>
#include<sys/statfs.h>
#include <sys/time.h>
#include<sys/ipc.h> 
#include<sys/msg.h> 
#include <termios.h>
#include <atomic>
namespace ec
{
	template <class _CLS>
	class daemon
	{
	public:
		static daemon<_CLS>* _pdaemon;
		struct t_msg {
			long mtype;
			char mtext[];
		};
		class cIpcMsg
		{
		public:
			cIpcMsg(int key)
			{
				_qid = msgget(key, IPC_CREAT | 0666);
			}
			int snd(const char* str)
			{
				if (_qid < 0)
					return -1;
				t_msg *pmsg = (t_msg*)malloc(strlen(str) + sizeof(t_msg) + 1);
				if (!pmsg)
					return -1;
				pmsg->mtype = 1;
				memcpy(pmsg->mtext, str, strlen(str) + 1);
				int nr = msgsnd(_qid, pmsg, strlen(str) + 1, IPC_NOWAIT);
				free(pmsg);
				return nr;
			}

			int rcv(char* pbuf, size_t szbuf)
			{
				if (_qid < 0)
					return -1;
				t_msg *pmsg = (t_msg*)malloc(szbuf + sizeof(t_msg));
				if (!pmsg)
					return -1;
				ssize_t szr = msgrcv(_qid, pmsg, szbuf, 0, IPC_NOWAIT);
				if (szr < 0) {
					free(pmsg);
					return -1;
				}
				memcpy(pbuf, pmsg->mtext, szr);
				free(pmsg);
				return (int)szr;
			}
			int del()
			{
				if (_qid < 0)
					return -1;
				return msgctl(_qid, IPC_RMID, NULL);
			}
		protected:
			int _qid;
		};

		class cFLock
		{
		public:
			cFLock()
			{
				m_sfile[0] = '\0';
				m_nlockfile = -1;
			}
			cFLock(const char* sfile)
			{
				Init(sfile);
			}
			~cFLock()
			{
				if (m_nlockfile >= 0)
					close(m_nlockfile);
				m_nlockfile = -1;
			}
		public:
			void Init(const char* sfile)
			{
				m_sfile[0] = '\0';
				strcpy(m_sfile, sfile);
				m_nlockfile = -1;
			}
			void Close()
			{
				if (m_nlockfile >= 0)
					close(m_nlockfile);
				m_nlockfile = -1;
			}
			int CheckLock()//return -1:error;  0: success, not lock; >0 : the pid locked
			{
				m_nlockfile = open(m_sfile, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
				if (m_nlockfile < 0)
					return -1;
				struct flock fl;
				fl.l_start = 0;
				fl.l_whence = SEEK_SET;
				fl.l_len = 0;
				fl.l_type = F_WRLCK;
				if (fcntl(m_nlockfile, F_GETLK, &fl) < 0)
					return -1;
				if (fl.l_type == F_UNLCK) // if unlock lock and return current pid
					return Lock();
				return fl.l_pid; // return the pid
			}

			int Lock() //lock and write pid to file
			{
				char buf[32];
				struct flock fl;
				fl.l_start = 0;
				fl.l_whence = SEEK_SET;
				fl.l_len = 0;
				fl.l_type = F_WRLCK;
				fl.l_pid = getpid();
				if (fcntl(m_nlockfile, F_SETLKW, &fl) < 0) //Blocking lock
					return -1;
				if (ftruncate(m_nlockfile, 0))
					return -1;
				lseek(m_nlockfile,0,SEEK_SET);
				sprintf(buf, "%ld\n", (long)getpid());
				if (write(m_nlockfile, buf, strlen(buf)) <= 0)
					return -1;
				return 0;
			}
			static  int GetLockPID(const char *spidfile)//get lock PID. ret -1:err; 0:not lock; >0 PID;
			{
				int nlockfile = open(spidfile, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
				if (nlockfile < 0)
					return -1;
				struct flock fl;
				fl.l_start = 0;
				fl.l_whence = SEEK_SET;
				fl.l_len = 0;
				fl.l_type = F_WRLCK;
				if (fcntl(nlockfile, F_GETLK, &fl) < 0) {
					close(nlockfile);
					return -1;
				}
				if (fl.l_type == F_UNLCK) { //not lock                
					close(nlockfile);
					return 0;
				}
				close(nlockfile);
				return fl.l_pid; //return lock pid
			}
		protected:
			int     m_nlockfile;
			char    m_sfile[512];
		};
	protected:
		cFLock _flck;
		_CLS* _pcls;
		int _msgkey;
		char _spidfile[512];
		char _sDaemon[512];
		char _sVer[512];
	private:
		std::atomic_bool _bstop;
		std::atomic_bool _brun;
	public:
		daemon() : _pcls(nullptr), _bstop(false), _brun(false)
		{
			memset(_spidfile, 0, sizeof(_spidfile));
			memset(_sDaemon, 0, sizeof(_sDaemon));
			memset(_sVer, 0, sizeof(_sVer));
		}
		void Init(const char* spidfile, const char* sDaemonName, const char* sver, int nmsgkey)
		{
			strcpy(_spidfile, spidfile);
			strcpy(_sDaemon, sDaemonName);
			strcpy(_sVer, sver);
			_msgkey = nmsgkey;
		}
		inline const char* pidfile()
		{
			return _spidfile;
		}
		inline const char* daemonname()
		{
			return _sDaemon;
		}
		static void CloseIO()
		{
			int fd = open("/dev/null", O_RDWR);
			if (fd < 0)
				return;
			dup2(fd, 0);
			dup2(fd, 1);
			dup2(fd, 2);
			close(fd);
		}
		
		void stopmain()
		{
			while (_brun) {
				_bstop = true;
				usleep(50 * 1000);
			}
			_bstop = false;
		}
		void stopcls()
		{
			if(_pcls)
				_pcls->stop();
			_pcls = nullptr;
		}
		static void exithandler(int ns)
		{
			if (daemon<_CLS>::_pdaemon) {
				daemon<_CLS>::_pdaemon->stopmain();
				daemon<_CLS>::_pdaemon->stopcls();
				daemon<_CLS>::_pdaemon = nullptr; //not free memery,exit will release the memory
			}
			exit(0);
		}
		int start()
		{
			_flck.Init(_spidfile);
			int nlock = _flck.CheckLock();
			if (nlock == -1) {
				printf("Access Error! Please use root account!\n");
				return 1;
			}
			else if (nlock) {
				printf("%s alreay runging! pid = %d\n", _sDaemon, nlock);
				return 0;
			}

			pid_t pid = fork();
			if (pid < 0)
				return (-1);
			else if (pid > 0) { //parent
				_flck.Close();
				cIpcMsg msg(_msgkey); // send
				char smsg[512] = { 0 };

				int i, n = 30, nm = 0;
				for (i = 0; i < n; i++)
				{
					sleep(1);
					if (msg.rcv(smsg, sizeof(smsg)) > 0)
					{
						printf("%s", smsg);
						fflush(stdout);
						if (!strcasecmp("finished", smsg)) {
							printf("\n");
							break;
						}
						nm++;
					}
					if (!nm && i > 5)
						n = 10;
				}
				msg.del();
				CloseIO();
				return 0;
			}
			else {
				cIpcMsg msg(_msgkey);
				setsid(); // become session leader
				if (chdir("/"))
				{
					msg.snd("chdir failed\n");
					msg.snd("finished");
					return 2;
				}
				umask(0); // clear file mode creation mask
				_flck.Lock();//relock

				_CLS* pcls = new _CLS();
				if (!pcls)
				{
					msg.snd("Start failed! no enough memery!\n");
					msg.snd("finished");
					_flck.Close();
					return 3;
				}

				if (msg.snd("\nstart...\r") < 0)
					printf("send message failed\n");
				if (!pcls->start())
				{
					msg.snd("Start failed!\n");
					msg.snd("finished");
					_flck.Close();
					exit(4);
					return 4;
				}
				_pcls = pcls;
				msg.snd("Start success!\n\n");
				msg.snd("finished");
				
				signal(SIGTERM, exithandler);
				while (!_bstop) {
					pcls->runtime();
				}
				_brun = false;
				_bstop = false;	
			}
			return 0;
		}
		
		void stop(int nsec = 300)
		{
			int i, nlock = cFLock::GetLockPID(_spidfile);
			bool bexit = true;
			if (nlock == -1) {
				printf("Access Error! Please use root account!\n");
				printf("\n");
				return;
			}
			else if (nlock > 0) {
				printf("stop %s... pid = %d\n", _sDaemon, nlock);
				bexit = false;
				kill(nlock, SIGTERM); // send term sig
				for (i = 0; i < nsec; i++) {
					if (cFLock::GetLockPID(_spidfile) <= 0) {
						bexit = true;
						break;
					}
					sleep(1);
				}
				if (!bexit) {
					kill(nlock, SIGKILL); //force kill
					printf("%d second Timeout,%s be killed!\n", nsec, _sDaemon);
				}
				else
					printf("%s stoped gracefully!\n", _sDaemon);
				printf("\n");
				return;
			}
			printf("%s not run!\n", _sDaemon);
			printf("\n");
		}

		void status()
		{
			int nlock = cFLock::GetLockPID(_spidfile);
			if (nlock == 0)
				printf("%s not run!\n", _sDaemon);
			else if (nlock == -1)
				printf("Access Error! Please use root account!\n");
			else
				printf("%s is runing!\n", _sDaemon);
			printf("\n");
		}

		void usage()
		{
			printf("usage:%s [-start] | [-stop] | [-status] | [-ver]\n", _sDaemon);
			printf("demo:\n");
			printf("%s -start\n", _sDaemon);
			printf("%s -stop\n", _sDaemon);
			printf("%s -status\n", _sDaemon);
			printf("%s -ver\n", _sDaemon);
			printf("\n");
			printf("%s %s\n", _sDaemon, _sVer);
			printf("\n");
		}

		void docmd(int argc, char** argv)
		{
			_CLS* pcls = new _CLS();
			if (pcls){
				pcls->docmd(argc, argv);
				delete pcls;
			}
		}
	};
}
#endif// ifndef _WIN32
