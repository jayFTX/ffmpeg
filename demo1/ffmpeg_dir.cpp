#include <iostream>

#ifdef __cplusplus
extern "C"{
#endif

#include <libavformat/avformat.h>

#ifdef __cplusplus
}
#endif

using std::cout;
using std::cin;

int main(int argc, char *argv[])
{
    int ret;
    AVIODirContext *ctx = NULL;
    AVIODirEntry *entry = NULL;
    char error_str[AV_ERROR_MAX_STRING_SIZE];

    av_log_set_level(AV_LOG_DEBUG);
    av_log(NULL, AV_LOG_INFO, "hello, world\n");
    ret = avio_open_dir(&ctx, "./", NULL);
    if(ret < 0){
        av_strerror(ret, error_str, sizeof(error_str));
        av_log(NULL, AV_LOG_ERROR, "Can't open dir: %s\n", error_str);
        return -1;
    }

    while(1){
        ret = avio_read_dir(ctx, &entry);

        if(ret < 0){
            av_strerror(ret, error_str, sizeof(error_str));
            av_log(NULL, AV_LOG_ERROR, "Can't read dir: %s\n", error_str);
            goto __failed;
        }

        if(!entry){
            break;
        }

        av_log(NULL, AV_LOG_INFO, "%12"PRId64" %s \n", entry->size, entry->name);
        avio_free_directory_entry(&entry);

    }
__failed:
    avio_close_dir(&ctx);
    return 0;
}
