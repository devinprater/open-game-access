/*
 * poke_platform.cpp — melonDS's Platform layer for iOS.
 *
 * melonDS expects the frontend to provide these symbols; on iOS the app is the
 * only "frontend", so file/thread/log plumbing lives here, backed by libc and
 * pthreads. The file APIs are thin wrappers because melonDS opens ROMs, saves
 * and firmware through them.
 */
#include "Platform.h"
#include "poke_internal.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

namespace melonDS
{
namespace Platform
{

// ------------------------------------------------------------------- logging
static PokeLogForward g_logForward = nullptr;

void PokeSetLogForward(PokeLogForward forward) { g_logForward = forward; }

void Log(LogLevel level, const char* fmt, ...)
{
    (void) level;
    if (!g_logForward) return;
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    g_logForward(buffer);
}

// --------------------------------------------------------------------- files
//
// melonDS's FileHandle is opaque to the core, so this is just a FILE*.
struct IOSFileHandle
{
    FILE* f;
};

FileHandle* OpenFile(const std::string& path, FileMode mode)
{
    const char* m;
    switch (mode)
    {
        case FileMode::Read: m = "rb"; break;
        case FileMode::Write: m = "wb"; break;
        case FileMode::ReadWrite: m = "r+b"; break;
        case FileMode::Append: m = "ab"; break;
        default: return nullptr;
    }

    FILE* f = fopen(path.c_str(), m);
    if (!f && mode == FileMode::ReadWrite)
    {
        // melonDS opens saves read/write; a save that does not exist yet must
        // be created rather than reported as a missing file.
        f = fopen(path.c_str(), "w+b");
    }
    if (!f) return nullptr;

    auto* handle = new IOSFileHandle{f};
    return reinterpret_cast<FileHandle*>(handle);
}

FileHandle* OpenLocalFile(const std::string& path, FileMode mode)
{
    return OpenFile(path, mode);
}

bool CloseFile(FileHandle* file)
{
    if (!file) return false;
    auto* handle = reinterpret_cast<IOSFileHandle*>(file);
    bool ok = fclose(handle->f) == 0;
    delete handle;
    return ok;
}

bool IsEndOfFile(FileHandle* file)
{
    auto* handle = reinterpret_cast<IOSFileHandle*>(file);
    return handle ? feof(handle->f) : true;
}

bool FileReadLine(char* str, int count, FileHandle* file)
{
    auto* handle = reinterpret_cast<IOSFileHandle*>(file);
    if (!handle) return false;
    return fgets(str, count, handle->f) != nullptr;
}

u64 FilePosition(FileHandle* file)
{
    auto* handle = reinterpret_cast<IOSFileHandle*>(file);
    if (!handle) return 0;
    return (u64) ftell(handle->f);
}

bool FileSeek(FileHandle* file, s64 offset, FileSeekOrigin origin)
{
    auto* handle = reinterpret_cast<IOSFileHandle*>(file);
    if (!handle) return false;
    int whence = SEEK_SET;
    switch (origin) {
        case FileSeekOrigin::Start: whence = SEEK_SET; break;
        case FileSeekOrigin::Current: whence = SEEK_CUR; break;
        case FileSeekOrigin::End: whence = SEEK_END; break;
    }
    return fseeko(handle->f, offset, whence) == 0;
}

void FileRewind(FileHandle* file)
{
    auto* handle = reinterpret_cast<IOSFileHandle*>(file);
    if (handle) rewind(handle->f);
}

u64 FileRead(void* data, u64 size, u64 count, FileHandle* file)
{
    auto* handle = reinterpret_cast<IOSFileHandle*>(file);
    if (!handle) return 0;
    return (u64) fread(data, (size_t) size, (size_t) count, handle->f);
}

bool FileFlush(FileHandle* file)
{
    auto* handle = reinterpret_cast<IOSFileHandle*>(file);
    return handle && fflush(handle->f) == 0;
}

u64 FileWrite(const void* data, u64 size, u64 count, FileHandle* file)
{
    auto* handle = reinterpret_cast<IOSFileHandle*>(file);
    if (!handle) return 0;
    return (u64) fwrite(data, (size_t) size, (size_t) count, handle->f);
}

u64 FileWriteFormatted(FileHandle* file, const char* fmt, ...)
{
    auto* handle = reinterpret_cast<IOSFileHandle*>(file);
    if (!handle) return 0;
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (n < 0) return 0;
    return (u64) fwrite(buffer, 1, (size_t) n, handle->f);
}

u64 FileLength(FileHandle* file)
{
    auto* handle = reinterpret_cast<IOSFileHandle*>(file);
    if (!handle) return 0;
    long current = ftell(handle->f);
    fseeko(handle->f, 0, SEEK_END);
    long end = ftell(handle->f);
    fseeko(handle->f, current, SEEK_SET);
    return (u64) end;
}

std::string GetLocalFilePath(const std::string& filename) { return filename; }

bool FileExists(const std::string& name)
{
    struct stat st;
    return stat(name.c_str(), &st) == 0;
}

bool LocalFileExists(const std::string& name) { return FileExists(name); }

bool CheckFileWritable(const std::string& filepath)
{
    return access(filepath.c_str(), W_OK) == 0;
}

bool CheckLocalFileWritable(const std::string& filepath) { return CheckFileWritable(filepath); }

// ------------------------------------------------------------------- threads
//
// melonDS only uses these for the save-writer and the WiFi thread; pthreads
// map straight onto them.
Thread* Thread_Create(std::function<void()> func)
{
    auto* fn = new std::function<void()>(std::move(func));
    pthread_t thread;
    int rc = pthread_create(&thread, nullptr, [](void* arg) -> void* {
        auto* f = static_cast<std::function<void()>*>(arg);
        (*f)();
        delete f;
        return nullptr;
    }, fn);
    if (rc != 0) { delete fn; return nullptr; }

    auto* handle = new pthread_t(thread);
    return reinterpret_cast<Thread*>(handle);
}

void Thread_Free(Thread* thread)
{
    if (!thread) return;
    auto* handle = reinterpret_cast<pthread_t*>(thread);
    pthread_detach(*handle);
    delete handle;
}

void Thread_Wait(Thread* thread)
{
    if (!thread) return;
    auto* handle = reinterpret_cast<pthread_t*>(thread);
    pthread_join(*handle, nullptr);
}

struct PokeSemaphore { pthread_mutex_t m; pthread_cond_t c; int count; };

Semaphore* Semaphore_Create()
{
    auto* sema = new PokeSemaphore;
    pthread_mutex_init(&sema->m, nullptr);
    pthread_cond_init(&sema->c, nullptr);
    sema->count = 0;
    return reinterpret_cast<Semaphore*>(sema);
}

void Semaphore_Free(Semaphore* sema)
{
    if (!sema) return;
    auto* s = reinterpret_cast<PokeSemaphore*>(sema);
    pthread_mutex_destroy(&s->m);
    pthread_cond_destroy(&s->c);
    delete s;
}

void Semaphore_Reset(Semaphore* sema)
{
    auto* s = reinterpret_cast<PokeSemaphore*>(sema);
    if (!s) return;
    pthread_mutex_lock(&s->m);
    s->count = 0;
    pthread_mutex_unlock(&s->m);
}

void Semaphore_Wait(Semaphore* sema)
{
    auto* s = reinterpret_cast<PokeSemaphore*>(sema);
    if (!s) return;
    pthread_mutex_lock(&s->m);
    while (s->count <= 0) pthread_cond_wait(&s->c, &s->m);
    s->count--;
    pthread_mutex_unlock(&s->m);
}

bool Semaphore_TryWait(Semaphore* sema, int timeout_ms)
{
    auto* s = reinterpret_cast<PokeSemaphore*>(sema);
    if (!s) return false;
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (long) (timeout_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000L; }

    pthread_mutex_lock(&s->m);
    while (s->count <= 0)
    {
        if (pthread_cond_timedwait(&s->c, &s->m, &deadline) != 0)
        {
            pthread_mutex_unlock(&s->m);
            return false;
        }
    }
    s->count--;
    pthread_mutex_unlock(&s->m);
    return true;
}

void Semaphore_Post(Semaphore* sema, int count)
{
    auto* s = reinterpret_cast<PokeSemaphore*>(sema);
    if (!s) return;
    pthread_mutex_lock(&s->m);
    s->count += count > 0 ? count : 1;
    pthread_cond_signal(&s->c);
    pthread_mutex_unlock(&s->m);
}

struct PokeMutex { pthread_mutex_t m; };

Mutex* Mutex_Create()
{
    auto* mutex = new PokeMutex;
    pthread_mutex_init(&mutex->m, nullptr);
    return reinterpret_cast<Mutex*>(mutex);
}

void Mutex_Free(Mutex* mutex)
{
    if (!mutex) return;
    auto* m = reinterpret_cast<PokeMutex*>(mutex);
    pthread_mutex_destroy(&m->m);
    delete m;
}

void Mutex_Lock(Mutex* mutex)
{
    auto* m = reinterpret_cast<PokeMutex*>(mutex);
    if (m) pthread_mutex_lock(&m->m);
}

void Mutex_Unlock(Mutex* mutex)
{
    auto* m = reinterpret_cast<PokeMutex*>(mutex);
    if (m) pthread_mutex_unlock(&m->m);
}

bool Mutex_TryLock(Mutex* mutex)
{
    auto* m = reinterpret_cast<PokeMutex*>(mutex);
    return m && pthread_mutex_trylock(&m->m) == 0;
}

// -------------------------------------------------------------------- timing
//
// mach_absolute_time on Apple, clock_gettime(CLOCK_MONOTONIC) elsewhere. The
// non-Apple path exists so the core can be compiled for a desktop host and
// exercised without a device.
#ifdef __APPLE__
static u64 s_timebaseNumer = 0;
static u64 s_timebaseDenom = 1;

static void EnsureTimebase()
{
    if (s_timebaseNumer != 0) return;
    mach_timebase_info_data_t info;
    mach_timebase_info(&info);
    s_timebaseNumer = info.numer;
    s_timebaseDenom = info.denom;
}
#endif

void Sleep(u64 usecs) { usleep((useconds_t) usecs); }

u64 GetMSCount()
{
#ifdef __APPLE__
    EnsureTimebase();
    return (mach_absolute_time() * s_timebaseNumer / s_timebaseDenom) / 1000ULL;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64) ts.tv_sec * 1000ULL + (u64) ts.tv_nsec / 1000000ULL;
#endif
}

u64 GetUSCount()
{
#ifdef __APPLE__
    EnsureTimebase();
    return mach_absolute_time() * s_timebaseNumer / s_timebaseDenom;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64) ts.tv_sec * 1000000ULL + (u64) ts.tv_nsec / 1000ULL;
#endif
}

// -------------------------------------------------------- optional callbacks
//
// Everything below is a hook into a desktop frontend (WiFi, camera, mic,
// firmware UI). The app drives none of them, and the DS games this app runs
// never touch them, so they are safe no-ops. They are defined (rather than
// left unresolved) because the core references them unconditionally.

void SignalStop(StopReason reason, void* userdata)
{
    // melonDS calls this when the emulated console stops itself (power off, a
    // bad exception region, GBA mode). The app learns about it here so it can
    // stop the frame loop and say why rather than keep spinning.
    (void) userdata;
    poke_internal_signal_stop((int) reason);
}


void WriteNDSSave(const u8* savedata, u32 savelen, u32 writeoffset, u32 writelen, void* userdata)
{
    (void) savedata; (void) savelen; (void) writeoffset; (void) writelen; (void) userdata;
}

void WriteGBASave(const u8* savedata, u32 savelen, u32 writeoffset, u32 writelen, void* userdata)
{
    (void) savedata; (void) savelen; (void) writeoffset; (void) writelen; (void) userdata;
}

void WriteFirmware(const Firmware& firmware, u32 writeoffset, u32 writelen, void* userdata)
{
    (void) firmware; (void) writeoffset; (void) writelen; (void) userdata;
}

void WriteDateTime(int year, int month, int day, int hour, int minute, int second, void* userdata)
{
    (void) year; (void) month; (void) day; (void) hour; (void) minute; (void) second; (void) userdata;
}

// WiFi: this build is deliberately offline. melonDS calls these only when a
// game tries to use wireless, which the DS games here do for C-Gear; the
// script tells the player those screens are out of scope.
void MP_Begin(void* userdata) { (void) userdata; }
void MP_End(void* userdata) { (void) userdata; }
int MP_SendPacket(u8* data, int len, u64 timestamp, void* userdata) { (void) data; (void) len; (void) timestamp; (void) userdata; return 0; }
int MP_RecvPacket(u8* data, u64* timestamp, void* userdata) { (void) data; (void) timestamp; (void) userdata; return 0; }
int MP_SendCmd(u8* data, int len, u64 timestamp, void* userdata) { (void) data; (void) len; (void) timestamp; (void) userdata; return 0; }
int MP_SendReply(u8* data, int len, u64 timestamp, u16 aid, void* userdata) { (void) data; (void) len; (void) timestamp; (void) aid; (void) userdata; return 0; }
int MP_SendAck(u8* data, int len, u64 timestamp, void* userdata) { (void) data; (void) len; (void) timestamp; (void) userdata; return 0; }
int MP_RecvHostPacket(u8* data, u64* timestamp, void* userdata) { (void) data; (void) timestamp; (void) userdata; return 0; }
u16 MP_RecvReplies(u8* data, u64 timestamp, u16 aidmask, void* userdata) { (void) data; (void) timestamp; (void) aidmask; (void) userdata; return 0; }

int Net_SendPacket(u8* data, int len, void* userdata) { (void) data; (void) len; (void) userdata; return 0; }
int Net_RecvPacket(u8* data, void* userdata) { (void) data; (void) userdata; return 0; }

void Camera_Start(int num, void* userdata) { (void) num; (void) userdata; }
void Camera_Stop(int num, void* userdata) { (void) num; (void) userdata; }
void Camera_CaptureFrame(int num, u32* frame, int width, int height, bool yuv, void* userdata)
{
    (void) num; (void) frame; (void) width; (void) height; (void) yuv; (void) userdata;
}

void Mic_Start(void* userdata) { (void) userdata; }
void Mic_Stop(void* userdata) { (void) userdata; }
// No microphone capture: the DS mic is used by a handful of minigames, none of
// which are on the accessibility script's path.
int Mic_ReadInput(s16* data, int maxlength, void* userdata)
{
    (void) data; (void) maxlength; (void) userdata;
    return 0;
}

AACDecoder* AAC_Init() { return nullptr; }

// The fork added the rest of the AAC decoder interface (deinit/configure/decode).
// The platform layer here offers no AAC backend, so AAC_Init returning nullptr is
// the whole story: the DS games this app runs never use the DSi AAC codec, and
// melonDS only calls these when a decoder exists.
void AAC_DeInit(AACDecoder* dec) { (void) dec; }

bool AAC_Configure(AACDecoder* dec, int frequency, int channels)
{
    (void) dec; (void) frequency; (void) channels;
    return false;
}

bool AAC_DecodeFrame(AACDecoder* dec, const void* input, int inputlen,
                     void* output, int outputlen)
{
    (void) dec; (void) input; (void) inputlen; (void) output; (void) outputlen;
    return false;
}

// ------------------------------------------------- GBA add-on cart hooks
//
// The melonDS-lua fork is newer than the platform layer here; its GBACart.cpp
// calls these three for the rumble pak, the Guitar Grip and the motion pak.
// None of them exist on iOS (no GBA slot, no rumble motor wired to the core,
// no accelerometer feed), and the DS games this app runs never mount an add-on
// cart, so they are no-ops like the WiFi/camera/mic hooks above. They must be
// DEFINED: the core references them unconditionally, so leaving them out fails
// the link.

bool Addon_KeyDown(KeyType type, void* userdata)
{
    (void) type; (void) userdata;
    return false;
}

void Addon_RumbleStart(u32 len, void* userdata)
{
    (void) len; (void) userdata;
}

void Addon_RumbleStop(void* userdata)
{
    (void) userdata;
}

float Addon_MotionQuery(MotionQueryType type, void* userdata)
{
    (void) type; (void) userdata;
    return 0.0f;
}

} // namespace Platform
} // namespace melonDS
