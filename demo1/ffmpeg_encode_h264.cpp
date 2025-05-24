#include <iostream>
#include <string>

#ifdef __cplusplus
extern "C"{
#endif

#include <libavutil/log.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/error.h>

#ifdef __cplusplus
}
#endif

using std::cout;
using std::cin;

int main(int argc, char *argv[])
{
    const char *filename, *codec_name;
    const AVCodec *codec;
    AVCodecContext *cctt = NULL;
    int ret, i, x, y;
    char error_str[AV_ERROR_MAX_STRING_SIZE];

    FILE *f;
    AVFrame *frame = NULL;
    AVPacket *pkt = NULL;

    uint8_t encode[] = { 0, 0, 1, 0xb7};
    
    av_log_set_level(AV_LOG_DEBUG);
    if(argc < 3){
        av_log(NULL, AV_LOG_ERROR, "args ERROR!\n");
        return -1;
    }

    filename = argv[1];
    codec_name = argv[2];

    //find encoder
    codec = avcodec_find_encoder_by_name(codec_name);
    if(!codec){
        av_log(NULL, AV_LOG_ERROR, "find encoder ERROR!\n");
        return -1;
    }
    cctt = avcodec_alloc_context3(codec);
    if(!cctt){
        av_log(NULL, AV_LOG_ERROR, "alloc encoder context ERROR!\n");
        return -1;
    }

    // put parameter
    cctt->bit_rate = 400000;
    cctt->width = 352;
    cctt->height = 288;
    cctt->time_base = (AVRational){1, 25};
    cctt->framerate = (AVRational){25, 1};
    cctt->gop_size = 10;
    cctt->max_b_frames = 1;
    cctt->pix_fmt = AV_PIX_FMT_YUV420P;

    if(codec->id == AV_CODEC_ID_H264){
        av_opt_set(cctt->priv_data, "preset", "slow", 0);
    }

    if(avcodec_open2(cctt, codec, NULL) < 0){
        av_log(NULL, AV_LOG_ERROR, "opne encoder ERROR!\n");
        return -1;
    }

    f = fopen(filename, "wb");
    if(!f){
        av_log(NULL, AV_LOG_ERROR, "open file ERROR!\n");
        return -1;
    }

    frame = av_frame_alloc();
    if(!frame){
        av_log(NULL, AV_LOG_ERROR, "alloc frame ERROR!\n");
        return -1;
    }

    frame->format = cctt->pix_fmt;
    frame->width = cctt->width;
    frame->height = cctt->height;

    ret = av_frame_get_buffer(frame, 32);
    if(ret < 0){
        av_log(NULL, AV_LOG_ERROR, "get buffer ERROR!\n");
        return -1;
    }


    /*
        encode 1 second of video
    */
    for(i = 0; i < 25; i++){
        pkt = av_packet_alloc();
        pkt->data = NULL;
        pkt->size = 0;
        fflush(stdout);

        ret = av_frame_make_writable(frame);
        if(ret < 0){
            av_log(NULL, AV_LOG_ERROR, "av_frame_make_writable ERROR!\n");
            return -1;
        }

        /* prepare a dummy image */
        for(y = 0; y < cctt->height; y++){
            for(x = 0; x < cctt->width; x++){
                frame->data[0][y * frame->linesize[0] + x] = x + y + i * 3;
            }
        }

        for(y = 0; y < cctt->height / 2; y++){
            for(x = 0; x < cctt->width / 2; x++){
                frame->data[1][y * frame->linesize[1] + x] = 128 + y + i * 2;
                frame->data[2][y * frame->linesize[2] + x] = 64 + x + i * 5;
            }
        }
        frame->pts = i;

        ret = avcodec_send_frame(cctt, frame);
        if(ret < 0){
            av_strerror(ret, error_str, sizeof(error_str));
            av_log(NULL, AV_LOG_ERROR, "Can't encode frame %s\n", error_str);
            return -1;
        }

        ret = avcodec_receive_packet(cctt, pkt);
        if(ret < 0){
            if(AVERROR(ret) == EAGAIN)
                av_log(NULL, AV_LOG_WARNING,  " output is not available in the current state \n");
            else
                av_log(NULL, AV_LOG_ERROR, "avcodec_receive_packet error \n");
        }
        fwrite(pkt->data, 1, pkt->size, f);
        av_packet_unref(pkt);
        av_packet_free(&pkt);

    }

    // for(got_output = 1; got_output; i++){
    //     fflush(stdout);
        
    //     ret = avcodec_encode_video2(cctt, &pkt, frame, &got_output);
    //     if(ret < 0){
    //         av_log(NULL, AV_LOG_ERROR, "avcodec_encode_video2 ERROR!\n");
    //         return -1;
    //     }
    // }

    av_packet_free(&pkt);
    fclose(f);
    av_frame_free(&frame);
    avcodec_free_context(&cctt);
    return 0;
}