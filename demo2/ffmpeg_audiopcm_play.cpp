#include <iostream>
#include <fstream>
#include <string>
#include <vector>

#ifdef __cplusplus
extern "C"{
#endif
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

void *video_process_frame_t(void *args)
{
    pthread_mutex_lock(&mutex);
    while(!buffer_ready){
        pthread_cond_wait(&cond, &mutex);
    }

    /* consume */


    pthread_mutex_unlock(&mutex);
}

void *audio_process_frame_t(void *args)
{

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
    av_log(NULL, AV_LOG_INFO, "frame_size_perchannel = %d \n", frame_size_perchannel);
    int buffer_size = frame->nb_samples * frame->ch_layout.nb_channels * bytes_per_sample;

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
        av_log(NULL, AV_LOG_WARNING, "codec buffer is fulled \n");
        return -1;
    }else if(ret == AVERROR(EINVAL)){
        av_log(NULL, AV_LOG_WARNING, "codec is not opened \n");
        return -1;
    }else if(ret == AVERROR_EOF){
        av_log(NULL, AV_LOG_WARNING, "codec buffer is flushed \n");
        return -1;
    }else if(ret < 0)
    {
        av_strerror(ret, error_str, AV_ERROR_MAX_STRING_SIZE);
        av_log(NULL, AV_LOG_ERROR, "avcodec_send_packet error %s\n", error_str);
        return -1;
    }

    while(avcodec_receive_frame(acodec, frame) >= 0){
        av_log(NULL, AV_LOG_INFO, "[%ld]:decode audio_data info: \n \
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
            av_log(NULL, AV_LOG_INFO, "[%ld]: audio channel is planar \n", audio_frames+1);
        } else {
            // 使用 data[0] 作为 interleaved
            av_log(NULL, AV_LOG_INFO, "[%ld]: audio channel is not planar \n", audio_frames+1);
        }
        int bytes_per_sample = av_get_bytes_per_sample((AVSampleFormat)acodec->sample_fmt);
        av_log(NULL, AV_LOG_INFO, "bytes_per_sample = %d \n", bytes_per_sample);
        // pthread_mutex_lock(&mutex);
        #if 1
        process_decoded_data(frame, bytes_per_sample);
        av_log(NULL, AV_LOG_INFO, "[%ld] frame is processed!\n", audio_frames+1);
        buffer_ready = true; 
        // pthread_cond_signal(&cond);  // 通知音频线程可以播放了
        // pthread_mutex_unlock(&mutex);
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
        av_log(NULL, AV_LOG_WARNING, "codec buffer is fulled \n");
        return 0;
    }else if(ret == AVERROR(EINVAL)){
        av_log(NULL, AV_LOG_WARNING, "codec is not opened \n");
        return -1;
    }else if(ret == AVERROR_EOF){
        av_log(NULL, AV_LOG_WARNING, "codec buffer is flushed \n");
        return -1;
    }else if(ret < 0)
    {
        av_strerror(ret, error_str, AV_ERROR_MAX_STRING_SIZE);
        av_log(NULL, AV_LOG_ERROR, "avcodec_send_packet error %s\n", error_str);
        return -1;
    }

    while(avcodec_receive_frame(vcodec, frame) >= 0){
        av_log(NULL, AV_LOG_INFO, "[%ld]:decode video_data info: \n \
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
            return -1;
        } // SDL3 的 API
        SDL_RenderPresent(renderer);
        // SDL_Delay(40);
        video_frames++;
        av_frame_unref(frame);
    }
    return 0;
}

int main(int argc, char *argv[])
{
    // enviroment prepare
    int ret, audio_index = -1, video_index = -1;
    AVFormatContext *ifc = NULL;
    AVCodecContext *acodec = NULL;
    const AVCodec *acode = NULL;

    AVCodecContext *vcodec = NULL;
    const AVCodec *vcode = NULL;

    // AVStream *stream = NULL;
    AVPacket *pkt = NULL;
    AVFrame *frame = NULL;

    //

    //
    if(argc < 3){
        std::cerr << "main Args error !" << std::endl;
        return -1;
    }
    const char *filename = argv[1], *of_name = argv[2];

    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);

    // initiate SDL and av and prepare
    if(!SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO)){
        SDL_Log("SDL_Init failed! %s", SDL_GetError());
        return -1;
    };

    if(!pthread_create(&vpid, NULL, video_process_frame_t, NULL)){
        av_log(NULL, AV_LOG_ERROR, "pthread_create error!\n");
        return -1;
    }

    if(!pthread_create(&apid, NULL, audio_process_frame_t, NULL)){
        av_log(NULL, AV_LOG_ERROR, "pthread_create error!\n");
        return -1;
    }

    #ifdef __cplusplus
    of.open(of_name, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    #else
    FILE *of = fopen(of_name, "wb+");
    if(!of){
        return -1;
    }
    #endif
    // 
    ret = open_containerFormat_get_av_index(&ifc, filename, &audio_index, &video_index);
    if(ret < 0){
        av_log(NULL, AV_LOG_ERROR, "open_containerFormat_get_audiostream_index error! \n");
        goto __failed;
    }
    //
    ret = open_avcode_context(ifc, &acode, &acodec, audio_index);
    if(ret < 0){
        av_log(NULL, AV_LOG_ERROR, "open_acode_context audio_stream acodec error! \n");
        goto __failed;
    }

    ret = open_avcode_context(ifc, &vcode, &vcodec, video_index);
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
    
    av_opt_set_chlayout(swr_ctx, "in_chlayout",    &acodec->ch_layout, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate",       acodec->sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", acodec->sample_fmt, 0);

    av_opt_set_chlayout(swr_ctx, "out_chlayout",    &dst_ch_layout, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate",       acodec->sample_rate, 0);
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
    audio_spec.channels = acodec->ch_layout.nb_channels;
    audio_spec.freq = acodec->sample_rate;
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
    if(!SDL_CreateWindowAndRenderer("audio_play", WINDOW_WIDTH, WINDOW_HEIGHT, 0, &window, &renderer)){
        SDL_Log("SDL_CreateWindowAndRenderer error  %s", SDL_GetError());
        goto __failed;
    }

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, vcodec->width, vcodec->height);
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

    while(av_read_frame(ifc, pkt) >= 0){
        if(pkt->stream_index == audio_index){
            pthread_mutex_lock(&mutex);
            
            ret = audio_decode(acodec, pkt, frame);
            if(ret < 0){
                av_log(NULL, AV_LOG_ERROR, "audio_decode failed!\n");
                break;
            }
            /* produce */
            pthread_cond_signal(&cond);
            pthread_mutex_unlock(&mutex);

        }else if(pkt->stream_index == video_index){
            ret = video_decode(vcodec, pkt, frame);
            if(ret < 0){
                av_log(NULL, AV_LOG_ERROR, "video_decode failed!\n");
                break;
            }
        }
        av_packet_unref(pkt);
    }
    // 5. Flush decoder
    audio_decode(acodec, NULL, frame);
    video_decode(vcodec, NULL, frame);

    
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
    avformat_free_context(ifc);
    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&acodec);
    avcodec_free_context(&vcodec);

    
    SDL_DestroyWindow(window);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyAudioStream(audio_stream);
    SDL_Quit();

    return 0;
}