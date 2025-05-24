#include <iostream>

#ifdef __cplusplus
extern "C"{
#endif

#include <libavutil/log.h>
#include <libavformat/avformat.h>

#ifdef __cplusplus
}
#endif

using std::cout;
using std::cin;

int main(int argc, char *argv[])
{
    AVFormatContext *fmt_ctx = NULL;
    int ret;
    char error_str[AV_ERROR_MAX_STRING_SIZE];

    av_log_set_level(AV_LOG_DEBUG);
    av_log(NULL, AV_LOG_INFO, "hello, world\n");

    // av_register_all();
    ret = avformat_open_input(&fmt_ctx, "test.mp4", NULL, NULL);
    if(ret < 0){
        av_strerror(ret, error_str, sizeof(error_str));
        av_log(NULL, AV_LOG_ERROR, "Can't open stream: %s\n", error_str);
        return -1;
    }
    av_dump_format(fmt_ctx, 0, "test.mp4", 0);

    avformat_close_input(&fmt_ctx);
    return 0;
}