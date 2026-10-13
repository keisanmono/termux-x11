#include "../lorie/src/main/cpp/lorie/arrow_sink.h"
#include <cassert>
#include <string>
#include <cstring>
#include <sys/wait.h>
static std::string drain(int fd) {
 char bytes[4096]; std::string all;
 int flags=fcntl(fd,F_GETFL);fcntl(fd,F_SETFL,flags|O_NONBLOCK);
 ssize_t n;while((n=read(fd,bytes,sizeof(bytes)))>0)all.append(bytes,n);
 return all;
}
int main() {
 errno=ERANGE;r9_tracef("stage=pre_attach");assert(errno==ERANGE&&r9_sink_lost==1);
 int p[2];assert(pipe(p)==0);assert(r9_sink_attach(p[1]));
 assert(fcntl(p[1],F_GETFL)&O_NONBLOCK);assert(fcntl(p[1],F_GETFD)&FD_CLOEXEC);
 r9_tracef("stage=ready");std::string ready=drain(p[0]);
 assert(ready.find("seq=2 lost=1")!=std::string::npos);
 assert(ready.find("stage=ready\n")!=std::string::npos);
 int extra[2];assert(pipe(extra)==0);assert(r9_sink_attach(extra[1]));
 assert(fcntl(extra[1],F_GETFD)==-1);close(extra[0]); // no replacement/leak
 char block[512]={};while(write(p[1],block,sizeof(block))>0){}
 unsigned long long lost=r9_sink_lost;errno=ERANGE;r9_tracef("stage=pipe_full");
 assert(errno==ERANGE&&r9_sink_lost==lost+1);drain(p[0]);
 r9_tracef("stage=after_full");assert(drain(p[0]).find("lost=2")!=std::string::npos);
 char longline[600];memset(longline,'a',sizeof(longline)-1);longline[599]=0;
 r9_tracef("%s",longline);assert(r9_sink_lost==3);assert(drain(p[0]).empty());
 // Real closed reader, default SIGPIPE disposition: process must survive.
 signal(SIGPIPE,SIG_DFL);
 sigset_t initial;sigemptyset(&initial);sigaddset(&initial,SIGPIPE);
 pthread_sigmask(SIG_UNBLOCK,&initial,nullptr);
 close(p[0]);errno=ERANGE;r9_tracef("stage=closed");
 assert(errno==ERANGE&&r9_sink_fd==-1&&r9_sink_lost==4);
 sigset_t current;pthread_sigmask(SIG_SETMASK,nullptr,&current);
 assert(!sigismember(&current,SIGPIPE));
 // Do not consume a SIGPIPE that was already pending for this thread.
 int q[2];assert(pipe(q)==0);assert(r9_sink_attach(q[1]));close(q[0]);
 sigset_t set;sigemptyset(&set);sigaddset(&set,SIGPIPE);pthread_sigmask(SIG_BLOCK,&set,nullptr);
 raise(SIGPIPE);r9_tracef("stage=already_pending");sigpending(&current);assert(sigismember(&current,SIGPIPE));
 timespec zero={};assert(sigtimedwait(&set,nullptr,&zero)==SIGPIPE);pthread_sigmask(SIG_UNBLOCK,&set,nullptr);
 unsigned long long count=r9_sink_seq;
 for(int i=0;i<9000;i++) r9_tracef("stage=bounded");
 assert(r9_sink_seq==8192&&count<8192);
}
