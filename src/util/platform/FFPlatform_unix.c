#include "FFPlatform_private.h"
#include "util/stringUtils.h"
#include "fastfetch_config.h"
#include "common/io/io.h"

#include <unistd.h>
#include <pwd.h>
#include <limits.h>
#include <sys/utsname.h>

#ifdef __APPLE__
    #include <libproc.h>
#elif defined(__FreeBSD__)
    #include <sys/sysctl.h>
#endif

static void platformPathAddEnv(FFlist* dirs, const char* env)
{
    const char* envValue = getenv(env);
    if(!ffStrSet(envValue))
        return;

    FF_STRBUF_AUTO_DESTROY value = ffStrbufCreateA(64);
    ffStrbufAppendS(&value, envValue);

    uint32_t startIndex = 0;
    while (startIndex < value.length)
    {
        uint32_t colonIndex = ffStrbufNextIndexC(&value, startIndex, ':');
        value.chars[colonIndex] = '\0';

        if(!ffStrSet(value.chars + startIndex))
        {
            startIndex = colonIndex + 1;
            continue;
        }

        ffPlatformPathAddAbsolute(dirs, value.chars + startIndex);

        startIndex = colonIndex + 1;
    }
}

static void getHomeDir(FFPlatform* platform, const struct passwd* pwd)
{
    const char* home = pwd ? pwd->pw_dir : getenv("HOME");
    ffStrbufAppendS(&platform->homeDir, home);
    ffStrbufEnsureEndsWithC(&platform->homeDir, '/');
}

static void getCacheDir(FFPlatform* platform)
{
    const char* cache = getenv("XDG_CACHE_HOME");
    if(ffStrSet(cache))
    {
        ffStrbufAppendS(&platform->cacheDir, cache);
        ffStrbufEnsureEndsWithC(&platform->cacheDir, '/');
    }
    else
    {
        ffStrbufAppend(&platform->cacheDir, &platform->homeDir);
        ffStrbufAppendS(&platform->cacheDir, ".cache/");
    }
}

static void getConfigDirs(FFPlatform* platform)
{
    platformPathAddEnv(&platform->configDirs, "XDG_CONFIG_HOME");
    ffPlatformPathAddHome(&platform->configDirs, platform, ".config/");

    #if defined(__APPLE__)
        ffPlatformPathAddHome(&platform->configDirs, platform, "Library/Preferences/");
        ffPlatformPathAddHome(&platform->configDirs, platform, "Library/Application Support/");
    #endif

    ffPlatformPathAddHome(&platform->configDirs, platform, "");
    platformPathAddEnv(&platform->configDirs, "XDG_CONFIG_DIRS");

    #if !defined(__APPLE__)
        ffPlatformPathAddAbsolute(&platform->configDirs, FASTFETCH_TARGET_DIR_ETC "/xdg/");
    #endif

    ffPlatformPathAddAbsolute(&platform->configDirs, FASTFETCH_TARGET_DIR_ETC "/");
    ffPlatformPathAddAbsolute(&platform->configDirs, FASTFETCH_TARGET_DIR_INSTALL_SYSCONF "/");
}

static void getDataDirs(FFPlatform* platform)
{
    platformPathAddEnv(&platform->dataDirs, "XDG_DATA_HOME");
    ffPlatformPathAddHome(&platform->dataDirs, platform, ".local/share/");

    // Add ${currentExePath}/../share
    FF_STRBUF_AUTO_DESTROY exePath = ffStrbufCreateA(PATH_MAX);
    #ifdef __linux__
        ssize_t exePathLen = readlink("/proc/self/exe", exePath.chars, exePath.allocated);
    #elif defined(__APPLE__)
        int exePathLen = proc_pidpath((int) getpid(), exePath.chars, exePath.allocated);
    #elif defined(__FreeBSD__)
        size_t exePathLen = exePath.allocated;
        if(sysctl(
            (int[]){CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, (int) getpid()}, 4,
            exePath.chars, &exePathLen,
            NULL, 0
        ))
            exePathLen = 0;
    #endif
    if (exePathLen > 0)
    {
        exePath.length = (uint32_t) exePathLen;
        ffStrbufSubstrBeforeLastC(&exePath, '/');
        ffStrbufSubstrBeforeLastC(&exePath, '/');
        ffStrbufAppendS(&exePath, "/share");
        ffPlatformPathAddAbsolute(&platform->dataDirs, exePath.chars);
    }

    #ifdef __APPLE__
        ffPlatformPathAddHome(&platform->dataDirs, platform, "Library/Application Support/");
    #endif

    ffPlatformPathAddHome(&platform->dataDirs, platform, "");
    platformPathAddEnv(&platform->dataDirs, "XDG_DATA_DIRS");
    ffPlatformPathAddAbsolute(&platform->dataDirs, FASTFETCH_TARGET_DIR_USR "/local/share/");
    ffPlatformPathAddAbsolute(&platform->dataDirs, FASTFETCH_TARGET_DIR_USR "/share/");
}

static void getUserName(FFPlatform* platform, const struct passwd* pwd)
{
    const char* user = getenv("USER");
    if(!ffStrSet(user) && pwd)
        user = pwd->pw_name;

    ffStrbufAppendS(&platform->userName, user);
}

static void getHostName(FFPlatform* platform, const struct utsname* uts)
{
    char hostname[256];
    if(gethostname(hostname, sizeof(hostname)) == 0)
        ffStrbufAppendS(&platform->hostName, hostname);

    if(platform->hostName.length == 0)
        ffStrbufAppendS(&platform->hostName, uts->nodename);
}

#ifdef __linux__
#include <netdb.h>
static void getDomainName(FFPlatform* platform)
{
    struct addrinfo hints = {0};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;

    struct addrinfo* info = NULL;

    if(getaddrinfo(platform->hostName.chars, "80", &hints, &info) != 0)
        return;

    struct addrinfo* current = info;
    while(platform->domainName.length == 0 && current != NULL)
    {
        ffStrbufAppendS(&platform->domainName, current->ai_canonname);
        current = current->ai_next;
    }

    freeaddrinfo(info);
}
#endif

static void getUserShell(FFPlatform* platform, const struct passwd* pwd)
{
    const char* shell = getenv("SHELL");
    if(!ffStrSet(shell) && pwd)
        shell = pwd->pw_shell;

    ffStrbufAppendS(&platform->userShell, shell);
}

void ffPlatformInitImpl(FFPlatform* platform)
{
    struct passwd* pwd = getpwuid(getuid());

    struct utsname uts;
    if(uname(&uts) != 0)
        memset(&uts, 0, sizeof(uts));

    getHomeDir(platform, pwd);
    getCacheDir(platform);
    getConfigDirs(platform);
    getDataDirs(platform);

    getUserName(platform, pwd);
    getHostName(platform, &uts);

    #ifdef __linux__
        getDomainName(platform);
    #endif

    getUserShell(platform, pwd);

    ffStrbufAppendS(&platform->systemName, uts.sysname);
    ffStrbufAppendS(&platform->systemRelease, uts.release);
    ffStrbufAppendS(&platform->systemVersion, uts.version);
    ffStrbufAppendS(&platform->systemArchitecture, uts.machine);
}
