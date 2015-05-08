#include <stdio.h>

#include <unistd.h>

#include <sys/types.h>

#include <sys/stat.h>

#include <sys/time.h>

#include <sys/wait.h>

#include <string.h>

#include <signal.h>

#include <fcntl.h>

#include <time.h>

#include "job.h"



#define DEBUG



int jobid=0;

int siginfo=1;

int fifo;

int globalfd;

int counttime=0;



struct waitqueue *next=NULL,*current =NULL;

struct waitqueue *head[3];



void setcounttime(int pri) {

	if (pri == 3)

		counttime=1;

	else if (pri == 2)

		counttime=2;

	else if (pri == 1)

		counttime=5;

	else {

		printf ("wrong pri number\n");

		counttime=0;

	}

}





/* 调度程序 */

void scheduler()

{

	struct jobinfo *newjob=NULL;

	struct jobcmd cmd;

	int  count = 0;

	bzero(&cmd,DATALEN);

	if((count=read(fifo,&cmd,DATALEN))<0)

		error_sys("read fifo failed");

#ifdef DEBUG

	printf("Reading whether other process send command!\n");

	if(count){

		printf("cmd cmdtype\t%d\ncmd defpri\t%d\ncmd data\t%s\n",cmd.type,cmd.defpri,cmd.data);

	}

	else

		printf("no data read\n");

#endif



	/* 更新等待队列中的作业 */

#ifdef DEBUG

	printf("Update jobs in wait queue!\n");

#endif

	updateall();



	switch(cmd.type){

	case ENQ:

		#ifdef DEBUG

		printf("Execute enq!\n");

		#endif

		do_enq(newjob,cmd);

		break;

	case DEQ:

		#ifdef DEBUG

		printf("Execute deq!\n");

		#endif

		do_deq(cmd);

		break;

	case STAT:

		#ifdef DEBUG

		printf("Execute stat!\n");

		#endif

		do_stat(cmd);

		break;

	default:

		break;

	}

	//printf("555555555555555555\n");

	if (counttime != 0) {

		printf("time isnt up\n");

		return;

	}

	/* 选择高优先级作业 */

	#ifdef DEBUG

		printf("Select which job to run next!\n");

	#endif

	next=jobselect();

	/* 作业切换 */

	#ifdef DEBUG

		printf("Select to the next job!\n");

	#endif

        #ifdef SHOW_SWITCH
		printf("before switch\n");
		do_stat(cmd);
	#endif
	
	jobswitch();

        #ifdef SHOW_SWITCH
		printf("after switch\n");
		do_stat(cmd);
	#endif
}



int allocjid()

{

	return ++jobid;

}



void updateall()

{

	struct waitqueue *p,*q,*mark=NULL;

	int i;

	//time block!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

	//printf("in updateall\n");

	//update counttime;

	if (counttime!=0) {

		counttime--;

	}

	/* 更新作业运行时间 */

	if(current) {

		//printf("update runtime\n");

		current->job->run_time += 1; /* 加1代表1000ms */

	}

	/* 更新作业等待时间及优先级 */

	for(i=2;i>=0;i--) {

		for(p = head[i]; p != NULL;){

			if(p!=current)

				p->job->wait_time += 1000;

			//printf("4444444444444444\n");

			if((p->job->wait_time) >= 10000 && (p->job->curpri) < 3){

				if (p != head[(p->job->curpri)-1]) {

					for (q = head[(p->job->curpri)-1]; q->next != p; q = q->next);

					q->next = p->next;

					mark = q;

				}

				else {

					head[(p->job->curpri)-1] = p->next;

					mark = NULL;

				}

				(p->job->curpri)++;

				p->job->wait_time = 0;

				if(head[(p->job->curpri)-1]){

					for(q = head[(p->job->curpri)-1]; q->next != NULL; q = q->next);

					q->next = p;

					p->next = NULL;

				}

				else{

					head[(p->job->curpri)-1] = p;

					head[(p->job->curpri)-1]->next = NULL;

				}

				if (mark == NULL) {

					p = head[(p->job->curpri)-1];

				}

				else p = mark->next;

				continue;

			}

			p = p->next;

		}

	}

}



struct waitqueue* jobselect()

{

	struct waitqueue *p,*prev,*select,*selectprev;

	int highest = -1,i;



	select = NULL;

	selectprev = NULL;

	for(i=2;i>=0;i--) {

		if(head[i]){

			/* 遍历等待队列中的作业，找到优先级最高的作业 */

			/*for(prev = head[i], p = head[i]; p != NULL; prev = p,p = p->next)

				if(p->job->curpri > highest){

					select = p;

					selectprev = prev;

					highest = p->job->curpri;

				}

				selectprev->next = select->next;

				if (select == selectprev)

					head = NULL;

			*/

			if(current !=NULL && (current->job->curpri) == i+1) {

				select = current->next;

			}

			if(select == NULL) {

				select = head[i];

			}

			break;

		}

	}

	return select;

}



void jobswitch()

{

	struct waitqueue *p;

	int i;



	if(current && (current->job->state) == DONE){ /* 当前作业完成 */

		/* 作业完成，删除它 */

		for(i = 0;(current->job->cmdarg)[i] != NULL; i++){

			free((current->job->cmdarg)[i]);

			(current->job->cmdarg)[i] = NULL;

		}

		/* 释放空间 */

		free(current->job->cmdarg);

		free(current->job);

		free(current);



		current = NULL;

	}



	if(next == NULL && current == NULL) /* 没有作业要运行 */



		return;

	else if (next != NULL && current == NULL){ /* 开始新的作业 */



		printf("begin start new job\n");

		current = next;

		setcounttime((current->job->curpri));

		next = NULL;

		current->job->state = RUNNING;

		kill(current->job->pid,SIGCONT);

		//printf("999999999999999999999\n");

		return;

	}

	else if (next != NULL && current != NULL){ /* 切换作业 */



		printf("switch to Pid: %d\n",next->job->pid);

		kill(current->job->pid,SIGSTOP);

		/*take current out of the wait queue*/

		if (current != head[(current->job->curpri)-1]) {

			for (p = head[(current->job->curpri)-1]; p->next != current; p = p->next);

			p->next = current->next;

		}

		else {

			head[(current->job->curpri)-1] = current->next;

		}

		current->job->curpri = current->job->defpri;

		current->job->wait_time = 0;

		current->job->state = READY;



		/* 放回等待队列 */

		if(head[(current->job->defpri)-1]){

			for(p = head[(current->job->defpri)-1]; p->next != NULL; p = p->next);

			p->next = current;

			current->next = NULL;

		}

		else{

			head[(current->job->defpri)-1] = current;

			head[(current->job->defpri)-1]->next = NULL;

		}

		current = next;

		setcounttime((current->job->curpri));

		next = NULL;

		current->job->state = RUNNING;

		current->job->wait_time = 0;

		kill(current->job->pid,SIGCONT);

		return;

	}else{ /* next == NULL且current != NULL，不切换 */

		return;

	}

}



void sig_handler(int sig,siginfo_t *info,void *notused)

{

	int status;

	int ret;
    
        #ifdef SHOW_SIGCHLD
	    struct jobcmd cmd;
	#endif


	switch (sig) {

case SIGVTALRM: /* 到达计时器所设置的计时间隔 */

	scheduler();

	#ifdef DEBUG

		printf("SIGNTALRM RECEIVED!\n");

	#endif

	return;

case SIGCHLD: /* 子进程结束时传送给父进程的信号 */

        #ifdef SHOW_SIGCHLD
	   do_stat(cmd);
	#endif
	
	ret = waitpid(-1,&status,WNOHANG);

	if (ret == 0)

		return;

	if(WIFEXITED(status)){

		current->job->state = DONE;

		printf("normal termation, exit status = %d\n",WEXITSTATUS(status));

	}else if (WIFSIGNALED(status)){

		printf("abnormal termation, signal number = %d\n",WTERMSIG(status));

	}else if (WIFSTOPPED(status)){

		printf("child stopped, signal number = %d\n",WSTOPSIG(status));

	}

	return;

	default:

		return;

	}

}



void do_enq(struct jobinfo *newjob,struct jobcmd enqcmd)

{

	struct waitqueue *newnode,*p;

	int i=0,pid;

	char *offset,*argvec,*q;

	char **arglist;

	sigset_t zeromask;



	sigemptyset(&zeromask);



	/* 封装jobinfo数据结构 */

	newjob = (struct jobinfo *)malloc(sizeof(struct jobinfo));

	newjob->jid = allocjid();

	newjob->defpri = enqcmd.defpri;

	newjob->curpri = enqcmd.defpri;

	newjob->ownerid = enqcmd.owner;

	newjob->state = READY;

	newjob->create_time = time(NULL);

	newjob->wait_time = 0;

	newjob->run_time = 0;

	arglist = (char**)malloc(sizeof(char*)*(enqcmd.argnum+1));

	newjob->cmdarg = arglist;

	offset = enqcmd.data;

	argvec = enqcmd.data;

	while (i < enqcmd.argnum){

		if(*offset == ':'){

			*offset++ = '\0';

			q = (char*)malloc(offset - argvec);

			strcpy(q,argvec);

			arglist[i++] = q;

			argvec = offset;

		}else

			offset++;

	}



	arglist[i] = NULL;



#ifdef DEBUG



	printf("enqcmd argnum %d\n",enqcmd.argnum);

	for(i = 0;i < enqcmd.argnum; i++)

		printf("parse enqcmd:%s\n",arglist[i]);



#endif



	/*向等待队列中增加新的作业*/

	newnode = (struct waitqueue*)malloc(sizeof(struct waitqueue));

	newnode->next =NULL;

	newnode->job=newjob;



	if(head[(newnode->job->defpri)-1]!=NULL) {

		//printf ("head != NULL");

		for(p=head[newnode->job->defpri-1];p->next != NULL; p=p->next);

		//printf("find end");

		p->next =newnode;

		//printf("insert successful");

	}

	else {

		//printf("head == NULL");

		head[newnode->job->defpri-1]=newnode;

		//printf("insert successful");

	}

	//printf ("22222222222222222222222222\n");		

	if((pid=fork())<0)

		error_sys("enq fork failed");

	if(pid==0){

		//printf("78787878787878\n");

		newjob->pid =getpid();

		//阻塞子进程,等等执行

		raise(SIGSTOP);

	#ifdef DEBUG

		printf("begin running\n");

		for(i=0;arglist[i]!=NULL;i++)

			printf("arglist %s\n",arglist[i]);

	#endif



		//复制文件描述符到标准输出

		dup2(globalfd,1);

		// 执行命令 

		if(execv(arglist[0],arglist)<0)

			printf("exec failed\n");

		exit(1);

	}else{

		newjob->pid=pid;

		if(current == NULL || (newnode->job->defpri) > (current->job->curpri)) {//if bigger, jobswitch; else, continue;

			printf ("steal the time\n");

			next = newnode;

			sleep(10);

			jobswitch();

		}

	}

/*

	if(head)

	{

		for(p=head;p->next != NULL; p=p->next);

		p->next =newnode;

	}else

		head=newnode;



	//为作业创建进程

	if((pid=fork())<0)

		error_sys("enq fork failed");



	if(pid==0){

		newjob->pid =getpid();

		//阻塞子进程,等等执行

		raise(SIGSTOP);

#ifdef DEBUG



		printf("begin running\n");

		for(i=0;arglist[i]!=NULL;i++)

			printf("arglist %s\n",arglist[i]);

#endif



		//复制文件描述符到标准输出

		dup2(globalfd,1);

		// 执行命令 

		if(execv(arglist[0],arglist)<0)

			printf("exec failed\n");

		exit(1);

	}else{

		newjob->pid=pid;

	}

*/

/*

	if (newnode->job->defpri <= current->job->curpri) { 

	//wait

		if(head[newnode->job->defpri-1]) {

			for(p=head[newnode->job->defpri-1];p->next != NULL; p=p->next);

			p->next =newnode;

		}

		else head[newnode->job->defpri-1]=newnode;

		//为作业创建进程

		if((pid=fork())<0)

			error_sys("enq fork failed");



		if(pid==0){

			newjob->pid =getpid();

			//阻塞子进程,等等执行

			raise(SIGSTOP);

		#ifdef DEBUG



			printf("begin running\n");

			for(i=0;arglist[i]!=NULL;i++)

				printf("arglist %s\n",arglist[i]);

		#endif



			//复制文件描述符到标准输出

			dup2(globalfd,1);

			// 执行命令 

			if(execv(arglist[0],arglist)<0)

				printf("exec failed\n");

			exit(1);

		}else{

			newjob->pid=pid;

		}

	}

	else {

	//if bigger, run now

		if(head[newnode->job->defpri-1]) {

			for(p=head[newnode->job->defpri-1];p->next != NULL; p=p->next);

			p->next =newnode;

		}

		else head[newnode->job->defpri-1]=newnode;

		

		if((pid=fork())<0)

			error_sys("enq fork failed");

		if(pid==0){

			newjob->pid =getpid();

			//阻塞子进程,等等执行

			raise(SIGSTOP);

		#ifdef DEBUG



			printf("begin running\n");

			for(i=0;arglist[i]!=NULL;i++)

				printf("arglist %s\n",arglist[i]);

		#endif



			//复制文件描述符到标准输出

			dup2(globalfd,1);

			// 执行命令 

			if(execv(arglist[0],arglist)<0)

				printf("exec failed\n");

			exit(1);

		}else{

			newjob->pid=pid;

			next = newnode;

			jobswitch();

		}

		



	}

*/

}



void do_deq(struct jobcmd deqcmd)

{

	int deqid,i;

	struct waitqueue *p,*prev,*select,*selectprev;

	deqid=atoi(deqcmd.data);



#ifdef DEBUG

	printf("deq jid %d\n",deqid);

#endif

	/*current jodid==deqid,终止当前作业*/

	if (current && current->job->jid ==deqid){

		printf("teminate current job\n");

		kill(current->job->pid,SIGKILL);

		//next = jobselect();

		for(i=0;(current->job->cmdarg)[i]!=NULL;i++){

			free((current->job->cmdarg)[i]);

			(current->job->cmdarg)[i]=NULL;

		}

		free(current->job->cmdarg);

		free(current->job);

		free(current);

		current=NULL;

		counttime=0;

		//jobswitch();

	}

	else{ /* 或者在等待队列中查找deqid */

		select=NULL;

		selectprev=NULL;

		for(i=2;i>=0;i--) {

			if(head[i]){

				if (deqid != head[i]->job->jid) {

					for (prev = head[i],p = head[i]; p != NULL; prev=p,p = p->next) {

						if (p->job->jid == deqid) {

							select=p;

							selectprev=prev;

							break;

						}

					}

					selectprev->next=select->next;

				}

				else {

					select = head[i];

					head[i]=head[i]->next; 

				}

				if(select){

					for(i=0;(select->job->cmdarg)[i]!=NULL;i++){

						free((select->job->cmdarg)[i]);

						(select->job->cmdarg)[i]=NULL;

					}

				}

				free(select->job->cmdarg);

				free(select->job);

				free(select);

				select=NULL;

				/*for(prev=head[i],p=head[i];p!=NULL;prev=p,p=p->next) {

					if(p->job->jid==deqid){

						select=p;

						selectprev=prev;

						break;

					}

				}

				selectprev->next=select->next;

				if(select==selectprev)

					head=NULL;

			

				if(select){

				for(i=0;(select->job->cmdarg)[i]!=NULL;i++){

					free((select->job->cmdarg)[i]);

					(select->job->cmdarg)[i]=NULL;

				}

				}

				free(select->job->cmdarg);

				free(select->job);

				free(select);

				select=NULL;*/

			}

		}

	}

}



void do_stat(struct jobcmd statcmd)

{

	struct waitqueue *p;

	char timebuf[BUFLEN];

	int i;

	/*

	*打印所有作业的统计信息:

	*1.作业ID

	*2.进程ID

	*3.作业所有者

	*4.作业运行时间

	*5.作业等待时间

	*6.作业创建时间

	*7.作业状态

	*/



	/* 打印信息头部 */

	printf("JOBID\tPID\tOWNER\tRUNTIME\tWAITTIME\tCREATTIME\t\tSTATE\n");

	if(current){

		strcpy(timebuf,ctime(&(current->job->create_time)));

		timebuf[strlen(timebuf)-1]='\0';

		printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\n",

			current->job->jid,

			current->job->pid,

			current->job->ownerid,

			current->job->run_time,

			current->job->wait_time,

			timebuf,"RUNNING");

	}

	for (i=2;i>=0;i--) {

		for(p=head[i];p!=NULL;p=p->next){

			if (p == current)

				continue;

			strcpy(timebuf,ctime(&(p->job->create_time)));

			timebuf[strlen(timebuf)-1]='\0';

			printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\n",

				p->job->jid,

				p->job->pid,

				p->job->ownerid,

				p->job->run_time,

				p->job->wait_time,

				timebuf,

				"READY");

		}

	}

}



int main()

{

	struct timeval interval;

	struct itimerval new,old;

	struct stat statbuf;

	struct sigaction newact,oldact1,oldact2;

	int i;

	#ifdef DEBUG

		printf("DEBUG IS OPEN!");

	#endif

	

	for (i=0;i<3;i++)

		head[i]=NULL;	

	

	if(stat("/tmp/server",&statbuf)==0){

		/* 如果FIFO文件存在,删掉 */

		if(remove("/tmp/server")<0)

			error_sys("remove failed");

	}



	if(mkfifo("/tmp/server",0666)<0)

		error_sys("mkfifo failed");

	/* 在非阻塞模式下打开FIFO */

	if((fifo=open("/tmp/server",O_RDONLY|O_NONBLOCK))<0)

		error_sys("open fifo failed");



	/* 建立信号处理函数 */

	newact.sa_sigaction=sig_handler;

	sigemptyset(&newact.sa_mask);

	newact.sa_flags=SA_SIGINFO;

	sigaction(SIGCHLD,&newact,&oldact1);

	sigaction(SIGVTALRM,&newact,&oldact2);

	//about time block!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

	/* 设置时间间隔为1000毫秒 */

	interval.tv_sec=1;

	interval.tv_usec=0;



	new.it_interval=interval;

	new.it_value=interval;

	setitimer(ITIMER_VIRTUAL,&new,&old);



	while(siginfo==1);



	close(fifo);

	close(globalfd);

	return 0;

}
