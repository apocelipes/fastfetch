#include "fastfetch.h"

#include <sys/statvfs.h>

static void getKey(FFinstance* instance, FFstrbuf* key, const char* folderPath, bool showFolderPath)
{
    if(instance->config.diskKey.length == 0)
    {
        if(showFolderPath)
            ffStrbufAppendF(key, "Disk (%s)", folderPath);
        else
            ffStrbufSetS(key, "Disk");
    }
    else
    {
        ffParseFormatString(key, &instance->config.diskKey, 1,
            (FFformatarg){FF_FORMAT_ARG_TYPE_STRING, folderPath}
        );
    }
}

static void printStatvfs(FFinstance* instance, FFstrbuf* key, struct statvfs* fs)
{
    const uint32_t GB = 1024 * 1024 * 1024;

    uint32_t total     = (fs->f_blocks * fs->f_frsize) / GB;
    uint32_t available = (fs->f_bfree  * fs->f_frsize) / GB;
    uint32_t used      = total - available;
    uint8_t percentage = (used / (double) total) * 100.0;

    uint32_t files = fs->f_files - fs->f_ffree;

    if(instance->config.diskFormat.length == 0)
    {
        ffPrintLogoAndKey(instance, key, NULL);
        printf("%uGB / %uGB (%u%%)\n", used, total, percentage);
    }
    else
    {
        ffPrintFormatString(instance, key, NULL, &instance->config.diskFormat, 4,
            (FFformatarg){FF_FORMAT_ARG_TYPE_UINT, &used},
            (FFformatarg){FF_FORMAT_ARG_TYPE_UINT, &total},
            (FFformatarg){FF_FORMAT_ARG_TYPE_UINT, &files},
            (FFformatarg){FF_FORMAT_ARG_TYPE_UINT8, &percentage}
        );
    }
}

static void printStatvfsCreateKey(FFinstance* instance, const char* folderPath, struct statvfs* fs)
{
    FF_STRBUF_CREATE(key);
    getKey(instance, &key, folderPath, true);
    printStatvfs(instance, &key, fs);
    ffStrbufDestroy(&key);
}

static void printFolder(FFinstance* instance, const char* folderPath)
{
    FF_STRBUF_CREATE(key);
    getKey(instance, &key, folderPath, true);

    struct statvfs fs;
    int ret = statvfs(folderPath, &fs);
    if(ret != 0 && instance->config.diskFormat.length == 0)
    {
        ffPrintError(instance, &key, NULL, "statvfs(\"%s\", &fs) != 0 (%i)", folderPath, ret);
        ffStrbufDestroy(&key);
        return;
    }

    printStatvfs(instance, &key, &fs);
    ffStrbufDestroy(&key);
}

void ffPrintDisk(FFinstance* instance)
{
    if(instance->config.diskFolders.length == 0)
    {

        struct statvfs fsRoot;
        int rootRet = statvfs("/", &fsRoot);

        struct statvfs fsHome;
        int homeRet = statvfs("/home", &fsHome);

        if(rootRet != 0 && homeRet != 0)
        {
            FF_STRBUF_CREATE(key);
            getKey(instance, &key, "/", false);
            ffPrintError(instance, &key, NULL, "statvfs failed for both / and /home");
            ffStrbufDestroy(&key);
            return;
        }

        if(rootRet == 0)
            printStatvfsCreateKey(instance, "/", &fsRoot);

        if(homeRet == 0 && (rootRet != 0 || fsRoot.f_fsid != fsHome.f_fsid))
            printStatvfsCreateKey(instance, "/home", &fsHome);
    }
    else
    {
        uint32_t lastIndex = 0;
        while (lastIndex < instance->config.diskFolders.length)
        {
            uint32_t colonIndex = ffStrbufFirstIndexAfterC(&instance->config.diskFolders, lastIndex, ':');
            instance->config.diskFolders.chars[colonIndex] = '\0';

            printFolder(instance, instance->config.diskFolders.chars + lastIndex);

            lastIndex = colonIndex + 1;
        }
    }
}