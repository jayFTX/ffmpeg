#include <iostream>

#ifdef __cplusplus
extern "C"{
#endif

#include <libavutil/log.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>

#ifdef __cplusplus
}
#endif

using std::cout;
using std::cin;

static void encode(AVCodecContext *c, AVFrame *frame, AVPacket *pkt, FILE *f)
{
    int ret;

    if(frame){
        std::cout << "Send frame %3"PRId64"\n" << frame->pts;
    }

    ret = avcodec_send_frame(c, frame);
    if(ret < 0){
        av_log(NULL, AV_LOG_ERROR, "avcodec_send_frame error\n");
        exit(1);
    }

    while(ret >= 0){
        ret = avcodec_receive_packet(c, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "avcodec_receive_packet error\n");
            exit(1);
        }

        printf("Write packet %3"PRId64" (size=%5d)\n", pkt->pts, pkt->size);
        fwrite(pkt->data, 1, pkt->size, f);
        av_packet_unref(pkt);
    }
}

int main(int argc, char *argv[])
{
    const char *filename, *codec_name;
    const AVCodec *codec;
    AVCodecContext *c = NULL;
    AVFrame *frame;
    AVPacket *pkt;
    FILE *f;
    int i, x, y, ret;
    uint8_t endcode[] = { 0, 0, 1, 0xb7 };
    
    if(argc < 2){
        av_log(NULL, AV_LOG_ERROR, "args error\n");
        return -1;
    }

    filename = argv[1];
    codec_name = argv[2];

    codec = avcodec_find_encoder_by_name(codec_name);
    if(!codec){
        av_log(NULL, AV_LOG_ERROR, "avcodec_find_encoder_by_name error\n");
        return -1;
    }

    c = avcodec_alloc_context3(codec);
    if(!c){
        av_log(NULL, AV_LOG_ERROR, "avcodec_alloc_context3 error\n");
        return -1;
    }

    pkt = av_packet_alloc();
    if(!pkt){
        av_log(NULL, AV_LOG_ERROR, "av_packet_alloc error\n");
        return -1;
    }

    /**/
        /* put sample parameters */
    c->bit_rate = 400000;
    /* resolution must be a multiple of two */
    c->width = 352;
    c->height = 288;
    /* frames per second */
    c->time_base = (AVRational){1, 25};
    c->framerate = (AVRational){25, 1};

    /* emit one intra frame every ten frames
     * check frame pict_type before passing frame
     * to encoder, if frame->pict_type is AV_PICTURE_TYPE_I
     * then gop_size is ignored and the output of encoder
     * will always be I frame irrespective to gop_size
     */
    c->gop_size = 10;
    c->max_b_frames = 1;
    c->pix_fmt = AV_PIX_FMT_YUV420P;

    if (codec->id == AV_CODEC_ID_H264)
        av_opt_set(c->priv_data, "preset", "slow", 0);
    /**/
    ret = avcodec_open2(c, codec, NULL);
    if(ret){
        av_log(NULL, AV_LOG_ERROR, "avcodec_open2 error\n");
        return -1; 
    }

    f = fopen(filename, "wb");
    if (!f) {
        av_log(NULL, AV_LOG_ERROR, "fopen error\n");
        return -1;
    }

    frame = av_frame_alloc();
    if (!frame) {
        av_log(NULL, AV_LOG_ERROR, "av_frame_alloc error\n");
        return -1;
    }
    frame->format = c->pix_fmt;
    frame->width  = c->width;
    frame->height = c->height;

    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "av_frame_get_buffer error\n");
        return -1;
    }

    /* encode 1 second of video */
    for (i = 0; i < 25; i++) {
        fflush(stdout);

        /* Make sure the frame data is writable.
           On the first round, the frame is fresh from av_frame_get_buffer()
           and therefore we know it is writable.
           But on the next rounds, encode() will have called
           avcodec_send_frame(), and the codec may have kept a reference to
           the frame in its internal structures, that makes the frame
           unwritable.
           av_frame_make_writable() checks that and allocates a new buffer
           for the frame only if necessary.
         */
        ret = av_frame_make_writable(frame);
        if (ret < 0)
            exit(1);

        /* Prepare a dummy image.
           In real code, this is where you would have your own logic for
           filling the frame. FFmpeg does not care what you put in the
           frame.
         */
        /* Y */
        for (y = 0; y < c->height; y++) {
            for (x = 0; x < c->width; x++) {
                frame->data[0][y * frame->linesize[0] + x] = x + y + i * 3;
            }
        }

        /* Cb and Cr */
        for (y = 0; y < c->height/2; y++) {
            for (x = 0; x < c->width/2; x++) {
                frame->data[1][y * frame->linesize[1] + x] = 128 + y + i * 2;
                frame->data[2][y * frame->linesize[2] + x] = 64 + x + i * 5;
            }
        }

        frame->pts = i;

        /* encode the image */
        encode(c, frame, pkt, f);
    }

    /* flush the encoder */
    encode(c, NULL, pkt, f);

    /* Add sequence end code to have a real MPEG file.
    It makes only sense because this tiny examples writes packets
    directly. This is called "elementary stream" and only works for some
    codecs. To create a valid file, you usually need to write packets
    into a proper file format or protocol; see mux.c.
    */
    if (codec->id == AV_CODEC_ID_MPEG1VIDEO || codec->id == AV_CODEC_ID_MPEG2VIDEO)
        fwrite(endcode, 1, sizeof(endcode), f);
    fclose(f);

    avcodec_free_context(&c);
    av_frame_free(&frame);
    av_packet_free(&pkt);
}