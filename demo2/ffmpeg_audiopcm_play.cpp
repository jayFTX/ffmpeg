#include <iostream>
#include <fstream>
#include <string>
#include <vector>

#ifdef __cplusplus
extern "C"{
#endif
#include <unistd.h>
#include <pthread.h>
#include <SDL3/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>

#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>


#ifdef __cplusplus
}
#endif

#define WINDOW_WIDTH 432
#define WINDOW_HEIGHT 768


static char error_str[AV_ERROR_MAX_STRING_SIZE];
static std::fstream of;
static pthread_cond_t cond;
static pthread_mutex_t mutex;
static bool buffer_ready = false;
static pthread_t vpid;
static pthread_t apid;
static pthread_t rpid;

//
static SDL_AudioStream *audio_stream = NULL;
static SDL_Texture *texture = NULL;
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;

static SDL_AudioSpec audio_spec;
static bool running = true;

//
SwrContext *swr_ctx = NULL;
int dst_rate = 44100;
AVChannelLayout dst_ch_layout = AV_CHANNEL_LAYOUT_MONO;
enum AVSampleFormat dst_sample_fmt = AV_SAMPLE_FMT_S16;
int dst_nb_samples = 0, max_dst_nb_samples = 0;
//

#define AUDIO_BUFFER_SIZE 100
#define VIDEO_BUFFER_SIZE 100
//
typedef struct FrameQueue{
    AVFrame *a_buf[AUDIO_BUFFER_SIZE];
    AVFrame *v_buf[VIDEO_BUFFER_SIZE];
    int a_buf_index;
    int a_buf_in;
    int v_buf_index;
    int v_buf_in;

    pthread_mutex_t audio_mutex;
    pthread_mutex_t video_mutex;
    pthread_cond_t audio_cond;
    pthread_cond_t video_cond;
}FrameQueue;

typedef struct AV_Context{
    AVFormatContext *ifc;
    int audio_index;
    int video_index;

    AVCodecContext *acodec;
    AVCodecContext *vcodec;
    const AVCodec *acode;
    const AVCodec *vcode;

    // std::vector<AVPacket *> a_buf;
    // std::vector<AVPacket *> v_buf;
    AVPacket *a_buf[AUDIO_BUFFER_SIZE];
    AVPacket *v_buf[VIDEO_BUFFER_SIZE];
    int a_buf_index;
    int a_buf_in;
    int v_buf_index;
    int v_buf_in;

    FrameQueue q;

    pthread_mutex_t audio_mutex;
    pthread_mutex_t video_mutex;
    pthread_cond_t audio_cond;
    pthread_cond_t video_cond;

    bool read_pkt_flags = true;
    bool video_decode_flags = true;
    bool audio_decode_flags = true;

}AV_Context;

#if 0
static std::vector<uint8_t> s_vaudio;

void audio_stream_cb(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)
{
    while(additional_amount > 0 && !s_vaudio.empty()){
        SDL_Log("additional_amount = %d \n s_vaudio.size() = %ld \n", additional_amount, s_vaudio.size());
        int len = std::min<int>(s_vaudio.size(), additional_amount);
        SDL_PutAudioStreamData(stream, s_vaudio.data(), len);
        s_vaudio.erase(s_vaudio.begin(), s_vaudio.begin() + len);
        additional_amount -= len;
    }
}
#else
static uint8_t *audio_buf = NULL;
static long int a_in = 0;
static long int a_index = 0;
static uint8_t *audio_pos = NULL;
static long int audio_frames = 0;
static long int video_frames = 0;
static long int audio_pkts = 0;
static long int video_pkts = 0;

int video_decode(AVCodecContext *vcodec, AVPacket *pkt, AVFrame *frame);
int audio_decode(AVCodecContext *acodec, AVPacket *pkt, AVFrame *frame);

void audio_stream_cb(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)
{
    pthread_mutex_lock(&mutex);
    // pthread_cond_wait(&cond, &mutex);
    while(!buffer_ready){
        pthread_cond_wait(&cond, &mutex);
    }
    long int available = a_index - a_in;
    if (available <= 0) {
        pthread_mutex_unlock(&mutex);
        return;
    }
    int len = (additional_amount < available) ? additional_amount : available;

    SDL_PutAudioStreamData(stream, audio_buf + a_in, len);
    a_in += len;

    if (a_in >= a_index) {
        buffer_ready = false;
        a_in = a_index = 0;  // 重置指针（如果没有使用环形缓冲）
    }
    // long int auido_len = a_index - a_in -1;
    // while(additional_amount > 0 && auido_len > 0){
        
    //     SDL_Log("additional_amount = %d \n audio_buf = %ld \n", additional_amount, auido_len);
    //     int len = additional_amount > auido_len ? auido_len : additional_amount;
        
    //     SDL_PutAudioStreamData(stream, audio_pos, len);
    //     auido_len -= len;
    //     additional_amount -= len;
    //     a_in += len;
    //     audio_pos = audio_buf + a_in;

    // }

    // buffer_ready = false;
    pthread_mutex_unlock(&mutex);

}
#endif

void *audio_process_frame_t(void *args)
{
    av_log(NULL, AV_LOG_INFO, "audio_decode thread start!\n");
    AV_Context *ctx = (AV_Context *)args;
    int ret, ai_index;
    AVFrame *frame = av_frame_alloc();
    if(!frame){
        av_log(NULL, AV_LOG_ERROR, "av_frame_alloc error\n");
        return NULL;
    }
    while(true){
        pthread_mutex_lock(&ctx->audio_mutex);
        while(ctx->a_buf_in == ctx->a_buf_index){
            // if(ctx->a_buf_index == AUDIO_BUFFER_SIZE){
            //     ctx->a_buf_index = 0;
            //     ctx->a_buf_in = 0;
            // }
            if(ctx->read_pkt_flags == false){
                pthread_mutex_unlock(&ctx->audio_mutex);
                goto __finished;
            }
            pthread_cond_wait(&ctx->audio_cond, &ctx->audio_mutex);
        }
        /* consume */
        ai_index = (ctx->a_buf_in++) % AUDIO_BUFFER_SIZE;
        av_log(NULL, AV_LOG_INFO, "consume %d audio_pkt\n", ctx->a_buf_in);
        ret = audio_decode(ctx->acodec, ctx->a_buf[ai_index], frame);
        if(ret < 0){
            av_log(NULL, AV_LOG_ERROR, "audio_decode error\n");
        }

        pthread_cond_signal(&ctx->audio_cond);
        pthread_mutex_unlock(&ctx->audio_mutex);

        av_packet_unref(ctx->a_buf[ai_index]);
    }
__finished:
    ctx->audio_decode_flags = true;
    av_frame_free(&frame);
    av_log(NULL, AV_LOG_INFO, "audio_decode thread exit!\n");
    // ctx->decode_flags = false;
    return NULL;
}

void *video_process_frame_t(void *args)
{
    av_log(NULL, AV_LOG_INFO, "video_decode thread start!\n");
    AV_Context *ctx = (AV_Context *)args;
    int ret, vi_index, vwq_index;
    AVFrame *frame = av_frame_alloc();
    if(!frame){
        av_log(NULL, AV_LOG_ERROR, "av_frame_alloc error\n");
        return NULL;
    }
    while(true){
        pthread_mutex_lock(&ctx->video_mutex);
        while(ctx->v_buf_in == ctx->v_buf_index){
            // if(ctx->v_buf_index == VIDEO_BUFFER_SIZE){
            //     ctx->v_buf_index = 0;
            //     ctx->v_buf_in = 0;
            // }
            if(ctx->read_pkt_flags == false){
                pthread_mutex_unlock(&ctx->video_mutex);
                goto __finished;
            }
            pthread_cond_wait(&ctx->video_cond, &ctx->video_mutex);
        }
        /* consume */
        // ret = video_decode(ctx->vcodec, ctx->v_buf[ctx->v_buf_in++], frame);
        // if(ret < 0){
        //     av_log(NULL, AV_LOG_ERROR, "video_decode error\n");
        // }
        vi_index = (ctx->v_buf_in++) % VIDEO_BUFFER_SIZE;
        av_log(NULL, AV_LOG_INFO, "consume %d video_pkt\n", ctx->v_buf_in);

        ret = avcodec_send_packet(ctx->vcodec, ctx->v_buf[vi_index]);
        if(ret < 0){
            av_strerror(ret, error_str, AV_ERROR_MAX_STRING_SIZE);
            av_log(NULL, AV_LOG_ERROR, "avcodec_send_packet error %s\n", error_str);
            return NULL;
        }
        while(avcodec_receive_frame(ctx->vcodec, frame) >= 0){
            av_log(NULL, AV_LOG_INFO, "[video %ld]:decode video_data info: \n \
                [frame->width]=  %d, \n \
                [frame->height]=  %d, \n \
                [frame->pixel_format]=  %s, \n \
                [frame->pts]=  %ld \n, \
                [frame->dts]=  %ld \n, \
                [frame->time_base]=  %f \n",
                video_frames+1,
                (frame)->width, 
                (frame)->height, 
                av_get_pix_fmt_name((AVPixelFormat)(frame)->format),
                (frame)->pts,
                (frame)->pkt_dts,
                av_q2d((frame)->time_base));
            
            // produce
            pthread_mutex_lock(&ctx->q.video_mutex);
            while((ctx->q.v_buf_index+1) % VIDEO_BUFFER_SIZE == ctx->q.v_buf_in){
                // pthread_mutex_unlock(&ctx->q.video_mutex);
                // while(ctx->q.v_buf_index != 0);
                pthread_cond_wait(&ctx->q.video_cond, &ctx->q.video_mutex);
            }
            vwq_index = (ctx->q.v_buf_index++) % VIDEO_BUFFER_SIZE;
            av_frame_ref(ctx->q.v_buf[vwq_index], frame);
            av_log(NULL, AV_LOG_INFO, "produce %d video_frame\n", ctx->q.v_buf_index);

            pthread_cond_signal(&ctx->q.video_cond);
            pthread_mutex_unlock(&ctx->q.video_mutex);

            video_frames++;
            av_frame_unref(frame);
        }

        pthread_cond_signal(&ctx->video_cond);
        pthread_mutex_unlock(&ctx->video_mutex);

        av_packet_unref(ctx->v_buf[vi_index]);
    }

__finished:
    av_frame_free(&frame);
    av_log(NULL, AV_LOG_INFO, "video_decode thread exit!\n");
    ctx->video_decode_flags = false;

    return NULL;
}

void *read_pkt(void *args)
{
    av_log(NULL, AV_LOG_INFO, "read_pkt thread  start!\n");
    AV_Context *ctx = (AV_Context *)args;
    int ret, aw_index, vw_index;
    AVPacket *pkt = av_packet_alloc();
    if(!pkt){
        av_log(NULL, AV_LOG_ERROR, "av_packet_alloc error\n");
        return NULL;
    }

    while((ret = av_read_frame(ctx->ifc, pkt)) >= 0){
        if(pkt->stream_index == ctx->audio_index){
            pthread_mutex_lock(&ctx->audio_mutex);
            while((ctx->a_buf_index + 1) % AUDIO_BUFFER_SIZE == ctx->a_buf_in){
                // pthread_mutex_unlock(&ctx->audio_mutex);
                // while(ctx->a_buf_index != 0);
                pthread_cond_wait(&ctx->audio_cond, &ctx->audio_mutex);
            }
            aw_index = (ctx->a_buf_index++) % AUDIO_BUFFER_SIZE;
            av_packet_ref(ctx->a_buf[aw_index], pkt);
            av_log(NULL, AV_LOG_INFO, "produce %d audio_pkt\n", ctx->a_buf_index);
    
            /* produce */
            pthread_cond_signal(&ctx->audio_cond);
            pthread_mutex_unlock(&ctx->audio_mutex);

            audio_pkts++;
        }else if(pkt->stream_index == ctx->video_index){
            pthread_mutex_lock(&ctx->video_mutex);
            while((ctx->v_buf_index + 1) % VIDEO_BUFFER_SIZE == ctx->v_buf_in){
                // pthread_mutex_unlock(&ctx->video_mutex);
                // while(ctx->v_buf_index != 0);
                pthread_cond_wait(&ctx->video_cond, &ctx->video_mutex);
            }

            /* produce */
            vw_index = (ctx->v_buf_index++) % VIDEO_BUFFER_SIZE;
            av_log(NULL, AV_LOG_INFO, "produce %d video_pkt\n", ctx->v_buf_index);
            av_packet_ref(ctx->v_buf[vw_index], pkt);

            // ctx.v_buf[ctx.v_buf_index] = ctx.video_pkt;
            pthread_cond_signal(&ctx->video_cond);
            pthread_mutex_unlock(&ctx->video_mutex);
            // ret = video_decode(ctx.vcodec, pkt, frame);
            // if(ret < 0){
            //     av_log(NULL, AV_LOG_ERROR, "video_decode failed!\n");
            //     break;
            // }
            video_pkts++;

        }
        av_packet_unref(pkt);
    }

    if(ret == AVERROR_EOF){
        av_log(NULL, AV_LOG_INFO, "av_read_frame: End of file.\n");
    }else{
        av_strerror(ret, error_str, sizeof(error_str));
        av_log(NULL, AV_LOG_ERROR, "av_read_frame error: %s\n", error_str);
    }
    
    av_log(NULL, AV_LOG_INFO, "decode pharse is over!\n");
    av_log(NULL, AV_LOG_INFO, "audio_pkts = %ld, video_pkts = %ld\n", audio_pkts, video_pkts);

    // 5. Flush decoder
    // audio_decode(ctx.acodec, NULL, frame);
    // video_decode(ctx.vcodec, NULL, frame);
    ctx->read_pkt_flags = false;

    av_packet_free(&pkt);
    return NULL;
}

void init()
{
    // FFMPEG INIT

    // SDL INIT

}

int open_containerFormat_get_av_index(AVFormatContext **ifc, const char *filename, int *audio_index, int *video_index)
{
    int ret = 0;
    ret = avformat_open_input(&(*ifc), filename, NULL, NULL);
    if(ret < 0){
        av_strerror(ret, error_str, AV_ERROR_MAX_STRING_SIZE);
        av_log(NULL, AV_LOG_ERROR, "avformat_open_input error! [%s] \n", error_str);
        return ret;
    }

    ret = avformat_find_stream_info(*ifc, NULL);
    if(ret < 0){
        av_strerror(ret, error_str, AV_ERROR_MAX_STRING_SIZE);
        av_log(NULL, AV_LOG_ERROR, "avformat_find_stream_info error! [%s] \n", error_str);
        return ret;
    }

    for(unsigned int i = 0; i < (*ifc)->nb_streams; i++){
        if((*ifc)->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO){
            *audio_index = i;
        }else if((*ifc)->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO){
            *video_index = i;
        }
    }

    return 0;
}

int open_avcode_context(AVFormatContext *ifc, const AVCodec **acode, AVCodecContext **acodec, int index)
{
    int ret = 0;
    *acode = avcodec_find_decoder(ifc->streams[index]->codecpar->codec_id);
    *acodec = avcodec_alloc_context3(*acode);
    if(!*acodec){
        av_log(NULL, AV_LOG_ERROR, "avcodec_alloc_context3 error! \n");
        return -1;
    }
    ret = avcodec_parameters_to_context(*acodec, ifc->streams[index]->codecpar);
    if(ret < 0){
        av_strerror(ret, error_str, AV_ERROR_MAX_STRING_SIZE);
        av_log(NULL, AV_LOG_ERROR, "avcodec_parameters_to_context error! [%s] \n", error_str);
        return -1;
    }
    ret = avcodec_open2(*acodec, *acode, NULL);
    if(ret < 0){
        av_strerror(ret, error_str, AV_ERROR_MAX_STRING_SIZE);
        av_log(NULL, AV_LOG_ERROR, "avcodec_open2 error! [%s] \n", error_str);
        return -1;
    }
    
    return 0;
}


void process_decoded_data(AVFrame *frame, int bytes_per_sample)
{
#ifdef __cplusplus
    int frame_size_perchannel = frame->nb_samples * bytes_per_sample;
    av_log(NULL, AV_LOG_INFO, "[audio: %ld]: frame_size_perchannel = %d \n", audio_frames+1, frame_size_perchannel);
    int buffer_size = frame->nb_samples * frame->ch_layout.nb_channels * bytes_per_sample;
    if(!frame->data[0]){
        av_log(NULL, AV_LOG_INFO, "[audio: %ld]: frame.data error \n", audio_frames+1);
    }
    for(int i = 0; i < frame->nb_samples; i++){
        for(int j = 0; j < frame->ch_layout.nb_channels; j++){
            const char *p = reinterpret_cast<const char *>(frame->data[j] + i * bytes_per_sample);
            of.write(p, bytes_per_sample);
        
            // #if 0    
            // s_vaudio.emplace_back(*(p));
            // s_vaudio.emplace_back(*(p+1));
            // s_vaudio.emplace_back(*(p+2));
            // s_vaudio.emplace_back(*(p+3));
            // #else
            memcpy(audio_buf + (((i * frame->ch_layout.nb_channels) + j) * bytes_per_sample),
                frame->data[j] + i * bytes_per_sample,
                bytes_per_sample);
            // #endif
            // pthread_cond_broadcast(&cond);
            // pthread_cond_wait(&cond, &mutex);
            // pthread_mutex_unlock(&mutex);

        }
    }
    SDL_PutAudioStreamData(audio_stream, audio_buf, buffer_size);
    SDL_Delay(1);
#else
    for(int i = 0; i < frame->nb_samples; i++){
        for(int j = 0; j < frame->ch_layout.nb_channels; j++){
            
            fwrite(frame->data[j] + i * bytes_per_sample, bytes_per_sample, 1, of);
            // av_log(NULL, AV_LOG_INFO, "read filesize = %ld \n", m-1);
        }
    }
#endif
}

int swresameple_audio(AVFrame *frame)
{
    max_dst_nb_samples = dst_nb_samples =
        av_rescale_rnd(frame->nb_samples, dst_rate, frame->sample_rate, AV_ROUND_UP);

    int ret = 0;

    // 分配输出缓冲区
    uint8_t **converted_data = NULL;
    int dst_linsize = 0;
    ret = av_samples_alloc_array_and_samples(&converted_data, &dst_linsize, 1, dst_nb_samples, AV_SAMPLE_FMT_S16, 0);
    if(ret < 0){
        av_log(NULL, AV_LOG_ERROR, "av_samples_alloc error\n");
        return -1;
    }
    av_log(NULL, AV_LOG_INFO, "dst_linsize = %d\n", dst_linsize);
    //
    dst_nb_samples = av_rescale_rnd(swr_get_delay(swr_ctx, frame->sample_rate) + frame->nb_samples,
                                        dst_rate, frame->sample_rate, AV_ROUND_UP);
    if (dst_nb_samples > max_dst_nb_samples) {
        av_freep(&converted_data[0]);
        ret = av_samples_alloc(converted_data, &dst_linsize, 1,
                                dst_nb_samples, dst_sample_fmt, 1);
        if (ret < 0)
            return -1;
        max_dst_nb_samples = dst_nb_samples;
    }
    
    // 执行转换
    int converted_samples = swr_convert(swr_ctx, converted_data, dst_nb_samples,
        (const uint8_t **)frame->data, frame->nb_samples);

    int converted_bytes = av_samples_get_buffer_size(&dst_linsize, 1, converted_samples, AV_SAMPLE_FMT_S16, 1);
    av_log(NULL, AV_LOG_INFO, "dst_linsize = %d\n", dst_linsize);

    // 送入 SDL 播放器
    SDL_PutAudioStreamData(audio_stream, converted_data[0], converted_bytes);
    
    // 清理
    av_freep(&converted_data);
    // swr_free(&swr_ctx);

    return 0;
}

int audio_decode(AVCodecContext *acodec, AVPacket *pkt, AVFrame *frame)
{
    int ret;
    ret = avcodec_send_packet(acodec, pkt);
    if(ret == AVERROR(EAGAIN)){
        av_log(NULL, AV_LOG_WARNING, "acodec buffer is fulled \n");
        return -1;
    }else if(ret == AVERROR(EINVAL)){
        av_log(NULL, AV_LOG_WARNING, "acodec is not opened \n");
        return -1;
    }else if(ret == AVERROR_EOF){
        av_log(NULL, AV_LOG_WARNING, "acodec buffer is flushed \n");
        return -1;
    }else if(ret < 0)
    {
        av_strerror(ret, error_str, AV_ERROR_MAX_STRING_SIZE);
        av_log(NULL, AV_LOG_ERROR, "avcodec_send_packet error %s\n", error_str);
        return -1;
    }

    while(avcodec_receive_frame(acodec, frame) >= 0){
        av_log(NULL, AV_LOG_INFO, "[audio: %ld]:decode audio_data info: \n \
            [frame->sample_rate]=  %d, \n \
            [frame->nb_samples]=  %d, \n \
            [frame->channels]=  %d, \n \
            [frame->format]=  %s \n", 
            audio_frames+1,
            (frame)->sample_rate, 
            (frame)->nb_samples, 
            (frame)->ch_layout.nb_channels, 
            av_get_sample_fmt_name((AVSampleFormat)(frame)->format));

        if (av_sample_fmt_is_planar((AVSampleFormat)(frame)->format)) {
            // 使用 data[0], data[1], ...
            av_log(NULL, AV_LOG_INFO, "[audio: %ld]: audio channel is planar \n", audio_frames+1);
        } else {
            // 使用 data[0] 作为 interleaved
            av_log(NULL, AV_LOG_INFO, "[audio: %ld]: audio channel is not planar \n", audio_frames+1);
        }
        int bytes_per_sample = av_get_bytes_per_sample((AVSampleFormat)acodec->sample_fmt);
        av_log(NULL, AV_LOG_INFO, "[audio: %ld]: bytes_per_sample = %d \n", audio_frames+1, bytes_per_sample);
        // pthread_mutex_lock(&mutex);
        #if 1
        process_decoded_data(frame, bytes_per_sample);
        av_log(NULL, AV_LOG_INFO, "[audio: %ld] this audio_frame is processed!\n\n", audio_frames+1);
        buffer_ready = true; 

        #else
        swresameple_audio(frame);
        #endif

        audio_frames++;
        av_frame_unref(frame);
    }
    return 0;
}

int video_decode(AVCodecContext *vcodec, AVPacket *pkt, AVFrame *frame)
{
    int ret;
    ret = avcodec_send_packet(vcodec, pkt);
    if(ret == AVERROR(EAGAIN)){
        av_log(NULL, AV_LOG_WARNING, "vcodec buffer is fulled \n");
        return 0;
    }else if(ret == AVERROR(EINVAL)){
        av_log(NULL, AV_LOG_WARNING, "vcodec is not opened \n");
        return -1;
    }else if(ret == AVERROR_EOF){
        av_log(NULL, AV_LOG_WARNING, "vcodec buffer is flushed \n");
        return -1;
    }else if(ret < 0)
    {
        av_strerror(ret, error_str, AV_ERROR_MAX_STRING_SIZE);
        av_log(NULL, AV_LOG_ERROR, "avcodec_send_packet error %s\n", error_str);
        return -1;
    }

    while(avcodec_receive_frame(vcodec, frame) >= 0){
        av_log(NULL, AV_LOG_INFO, "[video %ld]:decode video_data info: \n \
            [frame->width]=  %d, \n \
            [frame->height]=  %d, \n \
            [frame->pixel_format]=  %s, \n \
            [frame->pts]=  %ld \n, \
            [frame->dts]=  %ld \n, \
            [frame->time_base]=  %f \n",
            video_frames+1,
            (frame)->width, 
            (frame)->height, 
            av_get_pix_fmt_name((AVPixelFormat)(frame)->format),
            (frame)->pts,
            (frame)->pkt_dts,
            av_q2d((frame)->time_base));
        
        SDL_Rect r = {
            .x = 0, .y = 0,
            .w = frame->width,
            .h = frame->height
        };

        if(!SDL_UpdateYUVTexture(texture, &r, 
            frame->data[0], frame->linesize[0],
            frame->data[1], frame->linesize[1],
            frame->data[2], frame->linesize[2]
        )){
            SDL_Log("Failed to update texture: %s", SDL_GetError());
            return -1;
        }
        SDL_RenderClear(renderer);

        // SDL_FRect dst = {0, 0, 800, 600 };
        if(!SDL_RenderTexture(renderer, texture, NULL, NULL)){
            SDL_Log("SDL_RenderTexture Error\n");
            return -1;
        } // SDL3 的 API
        if(!SDL_RenderPresent(renderer)){
            SDL_Log("SDL_RenderTexture Error\n");
            return -1;
        }
        // SDL_Delay(4);
        usleep(10000);
        av_log(NULL, AV_LOG_INFO, "[video %ld]: this video_frame is processed!\n\n", video_frames+1);
        video_frames++;
        av_frame_unref(frame);
        // ctx.q.
    }
    return 0;
}

int main(int argc, char *argv[])
{
    // enviroment prepare
    AV_Context ctx = {0};
    int ret;
    ctx.audio_index = -1, ctx.video_index = -1;

    for(int i = 0; i < AUDIO_BUFFER_SIZE; i++){
        ctx.a_buf[i] = av_packet_alloc();
        if(!ctx.a_buf[i]){
            av_log(NULL, AV_LOG_WARNING, "av_packet_alloc error! \n");
        }
    }

    for(int i = 0; i < VIDEO_BUFFER_SIZE; i++){
        ctx.v_buf[i] = av_packet_alloc();
        if(!ctx.v_buf[i]){
            av_log(NULL, AV_LOG_WARNING, "av_packet_alloc error! \n");
        }
    }

    for(int i = 0; i < AUDIO_BUFFER_SIZE; i++){
        ctx.q.a_buf[i] = av_frame_alloc();
        if(!ctx.q.a_buf[i]){
            av_log(NULL, AV_LOG_WARNING, "av_frame_alloc error! \n");
        }
    }

    for(int i = 0; i < VIDEO_BUFFER_SIZE; i++){
        ctx.q.v_buf[i] = av_frame_alloc();
        if(!ctx.q.v_buf[i]){
            av_log(NULL, AV_LOG_WARNING, "av_frame_alloc error! \n");
        }
    }
    AVPacket *pkt = NULL;
    AVFrame *frame = NULL;

    // AVFormatContext *ifc = NULL;
    // AVCodecContext *acodec = NULL;
    // const AVCodec *acode = NULL;

    // AVCodecContext *vcodec = NULL;
    // const AVCodec *vcode = NULL;

    // // AVStream *stream = NULL;
    // AVPacket *pkt = NULL;
    // AVFrame *frame = NULL;

    //
    if(argc < 3){
        std::cerr << "main Args error !" << std::endl;
        return -1;
    }
    const char *filename = argv[1], *of_name = argv[2];

    pthread_mutex_init(&ctx.audio_mutex, NULL);
    pthread_mutex_init(&ctx.video_mutex, NULL);

    pthread_cond_init(&ctx.audio_cond, NULL);
    pthread_cond_init(&ctx.video_cond, NULL);

    pthread_mutex_init(&ctx.q.audio_mutex, NULL);
    pthread_mutex_init(&ctx.q.video_mutex, NULL);

    pthread_cond_init(&ctx.q.audio_cond, NULL);
    pthread_cond_init(&ctx.q.video_cond, NULL);

    // initiate SDL and av and prepare
    if(!SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO)){
        SDL_Log("SDL_Init failed! %s", SDL_GetError());
        return -1;
    };

    if(pthread_create(&vpid, NULL, video_process_frame_t, &ctx) < 0){
        av_log(NULL, AV_LOG_ERROR, "pthread_create error!\n");
        return -1;
    }

    if(pthread_create(&apid, NULL, audio_process_frame_t, &ctx) < 0){
        av_log(NULL, AV_LOG_ERROR, "pthread_create error!\n");
        return -1;
    }

    pthread_detach(vpid);
    pthread_detach(apid);
    //
    #ifdef __cplusplus
    of.open(of_name, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    #else
    FILE *of = fopen(of_name, "wb+");
    if(!of){
        return -1;
    }
    #endif
    // 
    ret = open_containerFormat_get_av_index(&ctx.ifc, filename, &ctx.audio_index, &ctx.video_index);
    if(ret < 0){
        av_log(NULL, AV_LOG_ERROR, "open_containerFormat_get_audiostream_index error! \n");
        goto __failed;
    }
    //
    ret = open_avcode_context(ctx.ifc, &ctx.acode, &ctx.acodec, ctx.audio_index);
    if(ret < 0){
        av_log(NULL, AV_LOG_ERROR, "open_acode_context audio_stream acodec error! \n");
        goto __failed;
    }

    ret = open_avcode_context(ctx.ifc, &ctx.vcode, &ctx.vcodec, ctx.video_index);
    if(ret < 0){
        av_log(NULL, AV_LOG_ERROR, "open_acode_context video_stream acodec error! \n");
        goto __failed;
    }

    //
    /* create resampler context */
    swr_ctx = swr_alloc();
    if (!swr_ctx) {
        av_log(NULL, AV_LOG_ERROR, "swr_ctx error! \n");
        goto __failed;
    }

    /* set options */
    
    av_opt_set_chlayout(swr_ctx, "in_chlayout",    &ctx.acodec->ch_layout, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate",       ctx.acodec->sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", ctx.acodec->sample_fmt, 0);

    av_opt_set_chlayout(swr_ctx, "out_chlayout",    &dst_ch_layout, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate",       ctx.acodec->sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", dst_sample_fmt, 0);

    /* initialize the resampling context */
    if ((ret = swr_init(swr_ctx)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "swr_init error! \n");
        goto __failed;
    }

    // open audioStream

    audio_buf = (uint8_t *)malloc(8192);
    audio_pos = audio_buf;
    
    #if 1
    audio_spec.channels = ctx.acodec->ch_layout.nb_channels;
    audio_spec.freq = ctx.acodec->sample_rate;
    audio_spec.format = SDL_AUDIO_F32LE;
    #else
    audio_spec.channels = dst_ch_layout.nb_channels;
    audio_spec.freq = acodec->sample_rate;
    audio_spec.format = SDL_AUDIO_S16LE;
    #endif

    audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio_spec, NULL, NULL);
    if(!audio_stream){
        SDL_Log("SDL_OpenAudioDeviceStream failed! %s", SDL_GetError());
        goto __failed;
    }

    SDL_ResumeAudioStreamDevice(audio_stream);

    // open windows
    if(!SDL_CreateWindowAndRenderer("av_play", WINDOW_WIDTH, WINDOW_HEIGHT, 0, &window, &renderer)){
        SDL_Log("SDL_CreateWindowAndRenderer error  %s", SDL_GetError());
        goto __failed;
    }

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, ctx.vcodec->width, ctx.vcodec->height);
    if(!texture){
        SDL_Log("SDL_CreateTexture error  %s", SDL_GetError());
        goto __failed;
    }

    // decode
    pkt = av_packet_alloc();
    if(!pkt){
        av_log(NULL, AV_LOG_WARNING, "av_packet_alloc error! \n");
    }

    frame = av_frame_alloc();
    if(!frame){
        av_log(NULL, AV_LOG_WARNING, "av_frame_alloc error! \n");
    }

    // ctx.audio_frame = av_frame_alloc();
    // audio_frame = ctx.audio_frame;
    // if(!audio_frame){
    //     av_log(NULL, AV_LOG_WARNING, "av_frame_alloc error! \n");
    // }

    // ctx.video_frame = av_frame_alloc();
    // video_frame = ctx.video_frame;
    // if(!video_frame){
    //     av_log(NULL, AV_LOG_WARNING, "av_frame_alloc error! \n");
    // }

    //decode

    if(pthread_create(&rpid, NULL, read_pkt, &ctx) < 0){
        av_log(NULL, AV_LOG_ERROR, "pthread_create error!\n");
        return -1;
    }
    pthread_detach(rpid);
    // while(av_read_frame(ctx.ifc, pkt) >= 0){
    //     if(pkt->stream_index == ctx.audio_index){
    //         pthread_mutex_lock(&ctx.audio_mutex);
    //         if(ctx.a_buf_index >= AUDIO_BUFFER_SIZE){
    //             pthread_mutex_unlock(&ctx.audio_mutex);
    //             while(ctx.a_buf_index != 0);
    //         }
    //         av_packet_ref(ctx.a_buf[ctx.a_buf_index++], pkt);
    //         // ctx.a_buf[ctx.a_buf_index++] = ctx.audio_pkt;

    //         // ret = audio_decode(ctx.acodec, pkt, frame);
    //         // if(ret < 0){
    //         //     av_log(NULL, AV_LOG_ERROR, "audio_decode failed!\n");
    //         //     break;
    //         // }
    //         /* produce */
    //         pthread_cond_signal(&ctx.audio_cond);
    //         pthread_mutex_unlock(&ctx.audio_mutex);

    //     }else if(pkt->stream_index == ctx.video_index){
    //         pthread_mutex_lock(&ctx.video_mutex);
    //         if(ctx.v_buf_index >= VIDEO_BUFFER_SIZE){
    //             pthread_mutex_unlock(&ctx.video_mutex);
    //             while(ctx.v_buf_index != 0);
    //         }

    //         /* produce */
    //         av_packet_ref(ctx.v_buf[ctx.v_buf_index++], pkt);

    //         // ctx.v_buf[ctx.v_buf_index] = ctx.video_pkt;

    //         pthread_cond_signal(&ctx.video_cond);
    //         pthread_mutex_unlock(&ctx.video_mutex);
    //         // ret = video_decode(ctx.vcodec, pkt, frame);
    //         // if(ret < 0){
    //         //     av_log(NULL, AV_LOG_ERROR, "video_decode failed!\n");
    //         //     break;
    //         // }
    //     }
    //     av_packet_unref(pkt);
    // }
    // av_log(NULL, AV_LOG_INFO, "decode pharse is over!\n");
    // // 5. Flush decoder
    // // audio_decode(ctx.acodec, NULL, frame);
    // // video_decode(ctx.vcodec, NULL, frame);
    // ctx.decode_flags = false;
    
    int viq_index;
    while(running){
        pthread_mutex_lock(&ctx.q.video_mutex);
        while(ctx.q.v_buf_in == ctx.q.v_buf_index){
            // if(ctx.q.v_buf_index == VIDEO_BUFFER_SIZE){
            //     ctx.q.v_buf_index = 0;
            //     ctx.q.v_buf_in = 0;
            // }
            if(ctx.video_decode_flags == false){
                pthread_mutex_unlock(&ctx.q.video_mutex);
                goto __finished;
            }
            pthread_cond_wait(&ctx.q.video_cond, &ctx.q.video_mutex);
        }
        /* consume */
        viq_index = (ctx.q.v_buf_in++) % VIDEO_BUFFER_SIZE;
        av_log(NULL, AV_LOG_INFO, "consume %d video_frame\n", ctx.q.v_buf_in);

        AVFrame *frame = ctx.q.v_buf[viq_index];
        SDL_Rect r = {
            .x = 0, .y = 0,
            .w = frame->width,
            .h = frame->height
        };

        if(!SDL_UpdateYUVTexture(texture, &r, 
            frame->data[0], frame->linesize[0],
            frame->data[1], frame->linesize[1],
            frame->data[2], frame->linesize[2]
        )){
            SDL_Log("Failed to update texture: %s", SDL_GetError());
            return -1;
        }
        SDL_RenderClear(renderer);

        // SDL_FRect dst = {0, 0, 800, 600 };
        if(!SDL_RenderTexture(renderer, texture, NULL, NULL)){
            SDL_Log("SDL_RenderTexture Error\n");
            return -1;
        } // SDL3 的 API
        if(!SDL_RenderPresent(renderer)){
            SDL_Log("SDL_RenderTexture Error\n");
            return -1;
        }
        // SDL_Delay(4);
        usleep(10000);
        
        pthread_cond_signal(&ctx.q.video_cond);
        pthread_mutex_unlock(&ctx.q.video_mutex);
        av_frame_unref(ctx.q.v_buf[viq_index]);

    }


__finished:
    av_log(NULL, AV_LOG_INFO, "video frame is fullly alread processed!\n");
    av_log(NULL, AV_LOG_INFO, "audio_pkts = %ld, video_pkts = %ld\n", audio_pkts, video_pkts);
    av_log(NULL, AV_LOG_INFO, "audio_frames = %ld, video_frames = %ld\n", audio_frames, video_frames);

    av_log(NULL, AV_LOG_INFO, "a_buf_index = %d a_buf_in = %d v_buf_index = %d v_buf_in = %d\n", 
        ctx.a_buf_index, ctx.a_buf_in, ctx.v_buf_index, ctx.v_buf_in);
    av_log(NULL, AV_LOG_INFO, "q.v_buf_index = %d q.v_buf_in = %d\n", ctx.q.v_buf_index, ctx.q.v_buf_in);

    SDL_Event event;
    while(running){
        while(SDL_PollEvent(&event)){
            switch(event.type){
                case SDL_EVENT_QUIT:
                    running = false;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if(event.key.key == SDLK_ESCAPE){
                        running = false;
                    }
                    break;
                default:
                    break;
            }
        }

        // SDL_Log("event.type = %d", event.type);
    }
__failed:
    #ifdef __cplusplus
    #else
    fclose(of);
    #endif
    free(audio_buf);
    avformat_free_context(ctx.ifc);
    av_packet_free(&pkt);
    av_frame_free(&frame);
    for(int i = 0; i < AUDIO_BUFFER_SIZE; i++){
        av_packet_free(&ctx.a_buf[i]);
    }
    for(int i = 0; i < VIDEO_BUFFER_SIZE; i++){
        av_packet_free(&ctx.v_buf[i]);
    }

    for(int i = 0; i < AUDIO_BUFFER_SIZE; i++){
        av_frame_free(&ctx.q.a_buf[i]);
    }
    for(int i = 0; i < VIDEO_BUFFER_SIZE; i++){
        av_frame_free(&ctx.q.v_buf[i]);
    }
    avcodec_free_context(&ctx.acodec);
    avcodec_free_context(&ctx.vcodec);

    
    SDL_DestroyWindow(window);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyAudioStream(audio_stream);
    SDL_Quit();

    return 0;
}