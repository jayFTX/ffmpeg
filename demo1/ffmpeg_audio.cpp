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
    int audio_index;
    AVPacket *pkt = NULL;
    av_log_set_level(AV_LOG_DEBUG);

    if(argc < 3){
        av_log(NULL, AV_LOG_ERROR, "Args < 3:\n");
        return -1;
    }

    char *src, *dst;
    src = argv[1];
    dst = argv[2];
    if(!src || !dst){
        av_log(NULL, AV_LOG_ERROR, "args[index] is nullptr:\n");
        return -1;
    }

    FILE *f = fopen(dst, "wb");
    if(!f){
        av_log(NULL, AV_LOG_ERROR, "Can't open dst_file:\n");
    }

    // av_register_all();
    ret = avformat_open_input(&fmt_ctx, src, NULL, NULL);
    if(ret < 0){
        av_strerror(ret, error_str, sizeof(error_str));
        av_log(NULL, AV_LOG_ERROR, "Can't open stream: %s\n", error_str);
        return -1;
    }
    av_dump_format(fmt_ctx, 0, src, 0);

    ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if(ret < 0){
        av_strerror(ret, error_str, sizeof(error_str));
        av_log(NULL, AV_LOG_ERROR, "Can't find best audio stream: %s\n", error_str);
        goto __failed;
    }

    audio_index = ret;

    pkt = av_packet_alloc();
    while(av_read_frame(fmt_ctx, pkt) >= 0){
        if(pkt->stream_index == audio_index){
            ret = fwrite(pkt->data, 1, pkt->size, f);
            if(ret != pkt->size){
                av_log(NULL, AV_LOG_WARNING, "pkt->size written is incomplete \n");
                goto __failed;
            }
        }

        av_packet_unref(pkt);
    }

__failed:
    av_packet_free(&pkt);
    avformat_close_input(&fmt_ctx);
    
    if(f)
        fclose(f);
    return 0;
}