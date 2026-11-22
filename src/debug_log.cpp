#include "debug_log.hpp"
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#if defined(__ANDROID__)
#include <android/log.h>
#include <sys/syscall.h>
#else
#include <sys/syscall.h>
#endif

namespace astrae::debug {
namespace {
constexpr const char *kPath="/storage/emulated/0/Android/data/com.nc.astraeoratio.singleplayer/files/debug.log";
constexpr const char *kTag="AstraeOffline";
constexpr off_t kRotateBytes=4*1024*1024;
pthread_mutex_t g_mutex=PTHREAD_MUTEX_INITIALIZER;
int g_fd=-1;
bool g_initialized=false;
bool g_file_ready=false;
const char *level_name(Level level){switch(level){case Level::Debug:return "D";case Level::Info:return "I";case Level::Warning:return "W";case Level::Error:return "E";}return "?";}
#if defined(__ANDROID__)
int android_priority(Level level){switch(level){case Level::Debug:return ANDROID_LOG_DEBUG;case Level::Info:return ANDROID_LOG_INFO;case Level::Warning:return ANDROID_LOG_WARN;case Level::Error:return ANDROID_LOG_ERROR;}return ANDROID_LOG_INFO;}
#endif
bool ensure_directory(const char *path){
    if(!path||!*path)return false;char buffer[512]{};size_t n=std::strlen(path);if(n>=sizeof(buffer))return false;std::memcpy(buffer,path,n+1);
    for(char *p=buffer+1;*p;++p){if(*p!='/')continue;*p='\0';if(mkdir(buffer,0700)!=0&&errno!=EEXIST){*p='/';return false;}struct stat st{};if(stat(buffer,&st)!=0||!S_ISDIR(st.st_mode)){*p='/';return false;}*p='/';}
    if(mkdir(buffer,0700)!=0&&errno!=EEXIST)return false;struct stat final_st{};return stat(buffer,&final_st)==0&&S_ISDIR(final_st.st_mode);
}
void write_all(int fd,const char *data,size_t length){while(length){ssize_t n=::write(fd,data,length);if(n>0){data+=n;length-=static_cast<size_t>(n);continue;}if(n<0&&errno==EINTR)continue;break;}}
long current_tid(){
#if defined(SYS_gettid)
    return static_cast<long>(syscall(SYS_gettid));
#else
    return static_cast<long>(getpid());
#endif
}
void emit_locked(Level level,const char *file,int line,const char *message){
    timespec ts{};clock_gettime(CLOCK_REALTIME,&ts);tm local{};localtime_r(&ts.tv_sec,&local);char stamp[64]{};std::snprintf(stamp,sizeof(stamp),"%04d-%02d-%02d %02d:%02d:%02d.%03ld",local.tm_year+1900,local.tm_mon+1,local.tm_mday,local.tm_hour,local.tm_min,local.tm_sec,ts.tv_nsec/1000000L);
    char record[4608]{};const char *base=file?std::strrchr(file,'/'):nullptr;base=base?base+1:(file?file:"?");int count=std::snprintf(record,sizeof(record),"%s [%s] [pid=%ld tid=%ld] [%s:%d] %s\n",stamp,level_name(level),static_cast<long>(getpid()),current_tid(),base,line,message?message:"(null)");if(count<0)return;size_t length=static_cast<size_t>(count);if(length>=sizeof(record))length=sizeof(record)-1;
    if(g_fd>=0){struct stat st{};if(fstat(g_fd,&st)==0&&st.st_size>kRotateBytes){close(g_fd);g_fd=-1;char backup[560]{};std::snprintf(backup,sizeof(backup),"%s.1",kPath);(void)unlink(backup);(void)rename(kPath,backup);g_fd=open(kPath,O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC,0600);g_file_ready=g_fd>=0;}if(g_fd>=0){write_all(g_fd,record,length);if(level==Level::Warning||level==Level::Error)(void)fsync(g_fd);}}
#if defined(__ANDROID__)
    __android_log_write(android_priority(level),kTag,message?message:"(null)");
#else
    (void)::write(STDERR_FILENO,record,length);
#endif
}
}
bool initialize() noexcept{
    pthread_mutex_lock(&g_mutex);if(g_initialized&&g_file_ready){pthread_mutex_unlock(&g_mutex);return true;}
    g_initialized=true;const char *slash=std::strrchr(kPath,'/');bool dir_ok=false;if(slash){char directory[512]{};size_t length=static_cast<size_t>(slash-kPath);if(length<sizeof(directory)){std::memcpy(directory,kPath,length);directory[length]='\0';dir_ok=ensure_directory(directory);}}
    if(g_fd<0&&dir_ok){g_fd=open(kPath,O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC,0600);if(g_fd>=0){(void)fchmod(g_fd,0600);g_file_ready=true;}}
    const bool ready=g_file_ready;char message[512]{};std::snprintf(message,sizeof(message),"debug logger initialized; path=%s; file_ready=%s; errno=%d (%s)",kPath,ready?"true":"false",ready?0:errno,ready?"ok":std::strerror(errno));emit_locked(ready?Level::Info:Level::Error,"debug_log.cpp",0,message);pthread_mutex_unlock(&g_mutex);return ready;
}
void shutdown() noexcept{pthread_mutex_lock(&g_mutex);if(g_fd>=0){(void)fsync(g_fd);close(g_fd);g_fd=-1;}g_file_ready=false;g_initialized=false;pthread_mutex_unlock(&g_mutex);}
void write(Level level,const char *file,int line,const char *format,...) noexcept{
    char message[3500]{};va_list args;va_start(args,format);if(format)std::vsnprintf(message,sizeof(message),format,args);else std::snprintf(message,sizeof(message),"(null format)");va_end(args);
    pthread_mutex_lock(&g_mutex);if(g_fd<0){g_initialized=true;const char *slash=std::strrchr(kPath,'/');bool dir_ok=false;if(slash){char directory[512]{};size_t length=static_cast<size_t>(slash-kPath);if(length<sizeof(directory)){std::memcpy(directory,kPath,length);directory[length]='\0';dir_ok=ensure_directory(directory);}}if(dir_ok){g_fd=open(kPath,O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC,0600);if(g_fd>=0){(void)fchmod(g_fd,0600);g_file_ready=true;}}}
    emit_locked(level,file,line,message);pthread_mutex_unlock(&g_mutex);
}
void write_raw(Level level,const char *message) noexcept{write(level,"<runtime>",0,"%s",message?message:"(null)");}
bool file_ready() noexcept{pthread_mutex_lock(&g_mutex);bool ready=g_file_ready;pthread_mutex_unlock(&g_mutex);return ready;}
const char *file_path() noexcept{return kPath;}
}
