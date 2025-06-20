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
#include <libavutil/time.h>

#ifdef __cplusplus
}
#endif

#define WINDOW_WIDTH 432
#define WINDOW_HEIGHT 768
#define MAX_AUDIO_QUEUE_SIZE 10000

static char error_str[AV_ERROR_MAX_STRING_SIZE];
static std::fstream of;
static pthread_t vpid;
static pthread_t apid;
static pthread_t rpid;
static pthread_t ap_pid;
static pthread_t e_pid;


//
static SDL_AudioStream *audio_stream = NULL;
static SDL_Texture *texture = NULL;
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;

static SDL_AudioSpec audio_spec;
static bool running = true;
static bool ap_running = true;
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
    int a_buf_front; // 队头
    int a_buf_rear; // 队尾
    int v_buf_front;
    int v_buf_rear;

    pthread_mutex_t audio_mutex;
    pthread_mutex_t video_mutex;
    pthread_cond_t audio_cond;
    pthread_cond_t video_cond;
}FrameQueue;

typedef struct PacketQueue{
    AVPacket *a_buf[AUDIO_BUFFER_SIZE];
    AVPacket *v_buf[VIDEO_BUFFER_SIZE];
    int a_buf_front;
    int a_buf_rear;
    int v_buf_front;
    int v_buf_rear;

    pthread_mutex_t audio_mutex;
    pthread_mutex_t video_mutex;
    pthread_cond_t audio_cond;
    pthread_cond_t video_cond;
}PacketQueue;

typedef struct AV_Context{
    AVFormatContext *ifc;
    int audio_index;
    int video_index;

    AVCodecContext *acodec;
    AVCodecContext *vcodec;
    const AVCodec *acode;
    const AVCodec *vcode;

    PacketQueue pkt_queue;
    FrameQueue frame_queue;

    bool read_pkt_flags = true;
    bool video_pkt_decode_flags = true;
    bool audio_pkt_decode_flags = true;

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
static double audio_clock = 0.0;
static double real_audio_clock = 0.0;
static double last_frame_pts = 0.0, last_delay = 0.0;

int video_decode(AVCodecContext *vcodec, AVPacket *pkt, AVFrame *frame);
int audio_decode(AVCodecContext *acodec, AVPacket *pkt, AVFrame *frame);
void process_decoded_data(AVFrame *frame, int bytes_per_sample);
int open_containerFormat_get_av_index(AVFormatContext **ifc, const char *filename, int *audio_index, int *video_index);
int open_avcode_context(AVFormatContext *ifc, const AVCodec **acode, AVCodecContext **acodec, int index);

// void audio_stream_cb(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)
// {
//     pthread_mutex_lock(&mutex);
//     // pthread_cond_wait(&cond, &mutex);
//     while(!buffer_ready){
//         pthread_cond_wait(&cond, &mutex);
//     }
//     long int available = a_index - a_in;
//     if (available <= 0) {
//         pthread_mutex_unlock(&mutex);
//         return;
//     }
//     int len = (additional_amount < available) ? additional_amount : available;

//     SDL_PutAudioStreamData(stream, audio_buf + a_in, len);
//     a_in += len;

//     if (a_in >= a_index) {
//         buffer_ready = false;
//         a_in = a_index = 0;  // 重置指针（如果没有使用环形缓冲）
//     }
//     // long int auido_len = a_index - a_in -1;
//     // while(additional_amount > 0 && auido_len > 0){
        
//     //     SDL_Log("additional_amount = %d \n audio_buf = %ld \n", additional_amount, auido_len);
//     //     int len = additional_amount > auido_len ? auido_len : additional_amount;
        
//     //     SDL_PutAudioStreamData(stream, audio_pos, len);
//     //     auido_len -= len;
//     //     additional_amount -= len;
//     //     a_in += len;
//     //     audio_pos = audio_buf + a_in;

//     // }

//     // buffer_ready = false;
//     pthread_mutex_unlock(&mutex);

// }
#endif

void *audio_process_frame_t(void *args)
{
    av_log(NULL, AV_LOG_INFO, "audio_decode thread start!\n");
    AV_Context *ctx = (AV_Context *)args;
    int ret, ai_index, aw_index;
    AVFrame *frame = av_frame_alloc();
    if(!frame){
        av_log(NULL, AV_LOG_ERROR, "av_frame_alloc error\n");
        return NULL;
    }
    while(ctx->audio_pkt_decode_flags){
        pthread_mutex_lock(&ctx->pkt_queue.audio_mutex);
        while(ctx->pkt_queue.a_buf_front == ctx->pkt_queue.a_buf_rear){

            if(ctx->read_pkt_flags == false){
                pthread_mutex_unlock(&ctx->pkt_queue.audio_mutex);
                goto __finished;
            }
            pthread_cond_wait(&ctx->pkt_queue.audio_cond, &ctx->pkt_queue.audio_mutex);
        }
        if(ctx->audio_pkt_decode_flags == false){
            pthread_cond_signal(&ctx->pkt_queue.audio_cond);
            pthread_mutex_unlock(&ctx->pkt_queue.audio_mutex);
            break;
        }
        /* consume */
        ai_index = ctx->pkt_queue.a_buf_front;
        ctx->pkt_queue.a_buf_front = (ctx->pkt_queue.a_buf_front + 1) % AUDIO_BUFFER_SIZE;

        ret = avcodec_send_packet(ctx->acodec, ctx->pkt_queue.a_buf[ai_index]);
        if(ret < 0){
            av_strerror(ret, error_str, AV_ERROR_MAX_STRING_SIZE);
            av_log(NULL, AV_LOG_ERROR, "avcodec_send_packet error %s\n", error_str);
            return NULL;
        }

        while(ctx->audio_pkt_decode_flags){
            ret = avcodec_receive_frame(ctx->acodec, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            else if (ret < 0)
                goto __finished;
            av_log(NULL, AV_LOG_INFO, "[audio: %ld]:decode audio_data info: \n \
                [frame->sample_rate]=  %d, \n \
                [frame->nb_samples]=  %d, \n \
                [frame->channels]=  %d, \n \
                [frame->format]=  %s \n \
                [frame->pts]=  %ld, \n \
                [frame->dts]=  %ld, \n \
                [frame->time_base.num]=  %d, \n \
                [frame->time_base.den]=  %d, \n",
                audio_frames+1,
                (frame)->sample_rate, 
                (frame)->nb_samples, 
                (frame)->ch_layout.nb_channels, 
                av_get_sample_fmt_name((AVSampleFormat)(frame)->format),
                frame->pts,
                frame->pkt_dts,
                ctx->ifc->streams[ctx->audio_index]->time_base.num,
                ctx->ifc->streams[ctx->audio_index]->time_base.den);

            if (av_sample_fmt_is_planar((AVSampleFormat)(frame)->format)) {
                // 使用 data[0], data[1], ...
                av_log(NULL, AV_LOG_INFO, "[audio: %ld]: audio channel is planar \n", audio_frames+1);
            } else {
                // 使用 data[0] 作为 interleaved
                av_log(NULL, AV_LOG_INFO, "[audio: %ld]: audio channel is not planar \n", audio_frames+1);
            }
            int bytes_per_sample = av_get_bytes_per_sample((AVSampleFormat)ctx->acodec->sample_fmt);
            av_log(NULL, AV_LOG_INFO, "[audio: %ld]: bytes_per_sample = %d \n", audio_frames+1, bytes_per_sample);

            // produce
            pthread_mutex_lock(&ctx->frame_queue.audio_mutex);
            while((ctx->frame_queue.a_buf_rear + 1) % AUDIO_BUFFER_SIZE == ctx->frame_queue.a_buf_front){
                if(ap_running == false){
                    pthread_cond_signal(&ctx->frame_queue.audio_cond);
                    pthread_mutex_unlock(&ctx->frame_queue.audio_mutex);
                    break;
                }
                pthread_cond_wait(&ctx->frame_queue.audio_cond, &ctx->frame_queue.audio_mutex);
            }
            if(ap_running == false){
                pthread_cond_signal(&ctx->frame_queue.audio_cond);
                pthread_mutex_unlock(&ctx->frame_queue.audio_mutex);
                break;
            }
            aw_index = ctx->frame_queue.a_buf_rear;
            ctx->frame_queue.a_buf_rear = (ctx->frame_queue.a_buf_rear + 1) % AUDIO_BUFFER_SIZE;

            av_frame_ref(ctx->frame_queue.a_buf[aw_index], frame);
            // av_log(NULL, AV_LOG_INFO, "produce %d audio_frame\n", ctx->frame_queue.a_buf_rear);

            pthread_cond_signal(&ctx->frame_queue.audio_cond);
            pthread_mutex_unlock(&ctx->frame_queue.audio_mutex);
            audio_frames++;
            av_frame_unref(frame);
        }

        pthread_cond_signal(&ctx->pkt_queue.audio_cond);
        pthread_mutex_unlock(&ctx->pkt_queue.audio_mutex);

        av_packet_unref(ctx->pkt_queue.a_buf[ai_index]);
    }
__finished:
    ctx->audio_pkt_decode_flags = false;
    // 线程退出时一定要通知对方，不然对方wait(cond)一直阻塞，然后pthread_join(对方线程)就会死锁。
    pthread_cond_signal(&ctx->pkt_queue.audio_cond);
    pthread_cond_signal(&ctx->frame_queue.audio_cond);

    av_frame_free(&frame);
    av_log(NULL, AV_LOG_INFO, "audio_pkt_decode thread exit!\n");

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
    while(ctx->video_pkt_decode_flags){
        pthread_mutex_lock(&ctx->pkt_queue.video_mutex);
        while(ctx->pkt_queue.v_buf_front == ctx->pkt_queue.v_buf_rear){
            if(ctx->read_pkt_flags == false){ 
                pthread_mutex_unlock(&ctx->pkt_queue.video_mutex);
                goto __finished;
            }
            pthread_cond_wait(&ctx->pkt_queue.video_cond, &ctx->pkt_queue.video_mutex);
        }
        if(ctx->video_pkt_decode_flags == false){
            pthread_cond_signal(&ctx->pkt_queue.video_cond);
            pthread_mutex_unlock(&ctx->pkt_queue.video_mutex);
            break;
        }
        /* consume */
        // ret = video_decode(ctx->vcodec, ctx->v_buf[ctx->v_buf_in++], frame);
        // if(ret < 0){
        //     av_log(NULL, AV_LOG_ERROR, "video_decode error\n");
        // }
        vi_index = ctx->pkt_queue.v_buf_front;
        ctx->pkt_queue.v_buf_front = (ctx->pkt_queue.v_buf_front + 1) % VIDEO_BUFFER_SIZE;

        // av_log(NULL, AV_LOG_INFO, "consume %d video_pkt\n", ctx->pkt_queue.v_buf_front);

        ret = avcodec_send_packet(ctx->vcodec, ctx->pkt_queue.v_buf[vi_index]);
        if(ret < 0){
            av_strerror(ret, error_str, AV_ERROR_MAX_STRING_SIZE);
            av_log(NULL, AV_LOG_ERROR, "avcodec_send_packet error %s\n", error_str);
            return NULL;
        }
        while(ctx->video_pkt_decode_flags){
            ret = avcodec_receive_frame(ctx->vcodec, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            else if (ret < 0)
                goto __finished;
            av_log(NULL, AV_LOG_INFO, "[video %ld]:decode video_data info: \n \
                [frame->width]=  %d, \n \
                [frame->height]=  %d, \n \
                [frame->pixel_format]=  %s, \n \
                [frame->pts]=  %ld \n, \
                [frame->dts]=  %ld \n, \
                [frame->time_base.num]=  %d \n \
                [frame->time_base.den]=  %d \n",
                video_frames+1,
                (frame)->width, 
                (frame)->height, 
                av_get_pix_fmt_name((AVPixelFormat)(frame)->format),
                (frame)->pts,
                (frame)->pkt_dts,
                ctx->ifc->streams[ctx->video_index]->time_base.num,
                ctx->ifc->streams[ctx->video_index]->time_base.den);
            
            // produce
            pthread_mutex_lock(&ctx->frame_queue.video_mutex);
            while((ctx->frame_queue.v_buf_rear+1) % VIDEO_BUFFER_SIZE == ctx->frame_queue.v_buf_front){
                if(running == false){ // 对于生产者来说， 消费者已阻塞， 生产者也要结束， 不然死锁。
                    pthread_cond_signal(&ctx->frame_queue.video_cond);
                    pthread_mutex_unlock(&ctx->frame_queue.video_mutex);
                    break;
                }
                pthread_cond_wait(&ctx->frame_queue.video_cond, &ctx->frame_queue.video_mutex);
            }
            if(running == false){
                pthread_cond_signal(&ctx->frame_queue.video_cond);
                pthread_mutex_unlock(&ctx->frame_queue.video_mutex);
                break;
            }
            vwq_index = ctx->frame_queue.v_buf_rear;
            ctx->frame_queue.v_buf_rear = (ctx->frame_queue.v_buf_rear + 1) % VIDEO_BUFFER_SIZE;

            av_frame_ref(ctx->frame_queue.v_buf[vwq_index], frame);
            av_log(NULL, AV_LOG_INFO, "produce %d video_frame\n", ctx->frame_queue.v_buf_rear);

            pthread_cond_signal(&ctx->frame_queue.video_cond);
            pthread_mutex_unlock(&ctx->frame_queue.video_mutex);

            video_frames++;
            av_frame_unref(frame);
        }

        pthread_cond_signal(&ctx->pkt_queue.video_cond);
        pthread_mutex_unlock(&ctx->pkt_queue.video_mutex);

        av_packet_unref(ctx->pkt_queue.v_buf[vi_index]);
    }

__finished:
    ctx->video_pkt_decode_flags = false;
    // 线程退出时一定要通知对方，不然对方wait(cond)一直阻塞，然后pthread_join(对方线程)就会死锁。
    pthread_cond_signal(&ctx->pkt_queue.video_cond);
    pthread_cond_signal(&ctx->frame_queue.video_cond);

    av_frame_free(&frame);
    av_log(NULL, AV_LOG_INFO, "video_pkt_decode thread exit!\n");

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
            pthread_mutex_lock(&ctx->pkt_queue.audio_mutex);
            while((ctx->pkt_queue.a_buf_rear + 1) % AUDIO_BUFFER_SIZE == ctx->pkt_queue.a_buf_front){
                if(ctx->read_pkt_flags == false){ // 通知退出时
                    pthread_cond_signal(&ctx->pkt_queue.audio_cond);
                    pthread_mutex_unlock(&ctx->pkt_queue.audio_mutex);
                    break;
                }
                pthread_cond_wait(&ctx->pkt_queue.audio_cond, &ctx->pkt_queue.audio_mutex);
            }
            if(ctx->read_pkt_flags == false){ // 通知退出时
                pthread_cond_signal(&ctx->pkt_queue.audio_cond);
                pthread_mutex_unlock(&ctx->pkt_queue.audio_mutex);
                break;
            }
            aw_index = ctx->pkt_queue.a_buf_rear;
            ctx->pkt_queue.a_buf_rear = (ctx->pkt_queue.a_buf_rear + 1) % AUDIO_BUFFER_SIZE;

            av_packet_ref(ctx->pkt_queue.a_buf[aw_index], pkt);
            // av_log(NULL, AV_LOG_INFO, "produce %d audio_pkt\n", ctx->pkt_queue.a_buf_rear);
    
            /* produce */
            pthread_cond_signal(&ctx->pkt_queue.audio_cond);
            pthread_mutex_unlock(&ctx->pkt_queue.audio_mutex);

            audio_pkts++;
        }else if(pkt->stream_index == ctx->video_index){
            pthread_mutex_lock(&ctx->pkt_queue.video_mutex);
            while((ctx->pkt_queue.v_buf_rear + 1) % VIDEO_BUFFER_SIZE == ctx->pkt_queue.v_buf_front){
                if(ctx->read_pkt_flags == false){ // 通知退出时
                    pthread_cond_signal(&ctx->pkt_queue.video_cond);
                    pthread_mutex_unlock(&ctx->pkt_queue.video_mutex);
                    break;
                }
                pthread_cond_wait(&ctx->pkt_queue.video_cond, &ctx->pkt_queue.video_mutex);
            }
            if(ctx->read_pkt_flags == false){ // 通知退出时
                pthread_cond_signal(&ctx->pkt_queue.video_cond);
                pthread_mutex_unlock(&ctx->pkt_queue.video_mutex);
                break;
            }
            /* produce */
            vw_index = ctx->pkt_queue.v_buf_rear;
            ctx->pkt_queue.v_buf_rear = (ctx->pkt_queue.v_buf_rear + 1) % VIDEO_BUFFER_SIZE;

            // av_log(NULL, AV_LOG_INFO, "produce %d video_pkt\n", ctx->pkt_queue.v_buf_rear);
            av_packet_ref(ctx->pkt_queue.v_buf[vw_index], pkt);

            // ctx.v_buf[ctx.v_buf_index] = ctx.video_pkt;
            pthread_cond_signal(&ctx->pkt_queue.video_cond);
            pthread_mutex_unlock(&ctx->pkt_queue.video_mutex);

            video_pkts++;

        }
        av_packet_unref(pkt);
    }

    if(ret == AVERROR_EOF){
        av_log(NULL, AV_LOG_INFO, "av_read_frame: End of file.\n");
    }else if(ret < 0){
        av_strerror(ret, error_str, sizeof(error_str));
        av_log(NULL, AV_LOG_ERROR, "av_read_frame error: %s\n", error_str);
    }

    av_log(NULL, AV_LOG_INFO, "read_pkt thread exit!\n");
    av_log(NULL, AV_LOG_INFO, "audio_pkts = %ld, video_pkts = %ld\n", audio_pkts, video_pkts);

    ctx->read_pkt_flags = false;
    // 线程退出时一定要通知对方，不然对方wait(cond)一直阻塞，然后pthread_join(对方线程)就会死锁。
    pthread_cond_signal(&ctx->pkt_queue.audio_cond);
    pthread_cond_signal(&ctx->pkt_queue.video_cond);

    av_packet_free(&pkt);
    return NULL;
}

void *audio_play(void *args)
{
    av_log(NULL, AV_LOG_INFO, "audio_play thread  start!\n");
    AV_Context *ctx = (AV_Context *)args;
    int ret, ai_index;
    AVFrame *frame = NULL;
    while(ap_running){
        pthread_mutex_lock(&ctx->frame_queue.audio_mutex);

        while(ctx->frame_queue.a_buf_front == ctx->frame_queue.a_buf_rear){
            if(ctx->audio_pkt_decode_flags == false){
                pthread_mutex_unlock(&ctx->frame_queue.audio_mutex);
                goto __finished;
            }
            pthread_cond_wait(&ctx->frame_queue.audio_cond, &ctx->frame_queue.audio_mutex);
        }
        if(ap_running == false){
            pthread_cond_signal(&ctx->frame_queue.audio_cond);
            pthread_mutex_unlock(&ctx->frame_queue.audio_mutex);
            break;
        }

        ai_index = ctx->frame_queue.a_buf_front;
        ctx->frame_queue.a_buf_front = (ctx->frame_queue.a_buf_front + 1) % AUDIO_BUFFER_SIZE;

        av_log(NULL, AV_LOG_INFO, "consume %d audio_frame\n", ctx->frame_queue.a_buf_front);
        frame = ctx->frame_queue.a_buf[ai_index];

        double duration = (double)frame->nb_samples / frame->sample_rate;
        av_log(NULL, AV_LOG_INFO, "this audio_frame duration =  %.4f\n", duration);

        audio_clock = frame->pts * av_q2d(ctx->ifc->streams[ctx->audio_index]->time_base);
        av_log(NULL, AV_LOG_INFO, "this audio_frame pts is =  %.4f\n", audio_clock);
        if (audio_clock <= 0) {
            // 等待音频先跑起来再播放视频
            usleep(10000);
            pthread_cond_signal(&ctx->frame_queue.audio_cond);
            pthread_mutex_unlock(&ctx->frame_queue.audio_mutex);
            av_frame_unref(ctx->frame_queue.a_buf[ai_index]);
            continue;
        }

        int bytes_per_sample = av_get_bytes_per_sample((AVSampleFormat)ctx->acodec->sample_fmt);
        int queued_bytes = SDL_GetAudioStreamQueued(audio_stream);
        av_log(NULL, AV_LOG_INFO, "this queued_bytes  is =  %d\n", queued_bytes);
        while (SDL_GetAudioStreamQueued(audio_stream) > MAX_AUDIO_QUEUE_SIZE) {
            SDL_Delay(5); // 等待缓冲消耗
        }       

        double bytes_per_sec = frame->sample_rate * frame->ch_layout.nb_channels * bytes_per_sample;
        double buffered_time = (double)queued_bytes / bytes_per_sec;
        real_audio_clock = audio_clock - buffered_time; 
        av_log(NULL, AV_LOG_INFO, "audio_frame pts = %.4f \n", audio_clock);
        av_log(NULL, AV_LOG_INFO, "real_audio_clock %.4f \n", real_audio_clock);

        process_decoded_data(frame, bytes_per_sample);

        pthread_cond_signal(&ctx->frame_queue.audio_cond);
        pthread_mutex_unlock(&ctx->frame_queue.audio_mutex);
        av_frame_unref(ctx->frame_queue.a_buf[ai_index]);
    }
__finished:
    // av_frame_free(&frame);
    // pthread_cond_signal(&ctx->frame_queue.audio_cond);

    av_log(NULL, AV_LOG_INFO, "audio_play thread exit!\n");
    return NULL;
}

// void *event_process_t(void *args)
// {
//     av_log(NULL, AV_LOG_INFO, "event_process_t thread  start!\n");
//     AV_Context *ctx = (AV_Context *)args;
//     SDL_Event event;
//     while(running){
//         while(SDL_PollEvent(&event)){
//             switch(event.type){
//                 case SDL_EVENT_QUIT:
//                     running = false;
//                     break;
//                 case SDL_EVENT_KEY_DOWN:
//                     if(event.key.key == SDLK_ESCAPE){
//                         running = false;
//                     }
//                     break;
//                 default:
//                     break;
//             }
//         }

//         // SDL_Log("event.type = %d", event.type);
//     }
//     av_log(NULL, AV_LOG_INFO, "event_process_t thread  exit!\n");
//     return NULL;
// }

int init(AV_Context *ctx, const char *of_name)
{
    int ret;
    // FFMPEG INIT
    for(int i = 0; i < AUDIO_BUFFER_SIZE; i++){
        ctx->pkt_queue.a_buf[i] = av_packet_alloc();
        if(!ctx->pkt_queue.a_buf[i]){
            av_log(NULL, AV_LOG_WARNING, "av_packet_alloc error! \n");
            return -1;
        }
        ctx->pkt_queue.v_buf[i] = av_packet_alloc();
        if(!ctx->pkt_queue.v_buf[i]){
            av_log(NULL, AV_LOG_WARNING, "av_packet_alloc error! \n");
            return -1;
        }
    }

    for(int i = 0; i < AUDIO_BUFFER_SIZE; i++){
        ctx->frame_queue.a_buf[i] = av_frame_alloc();
        if(!ctx->pkt_queue.a_buf[i]){
            av_log(NULL, AV_LOG_WARNING, "av_frame_alloc error! \n");
            return -1;
        }
        ctx->frame_queue.v_buf[i] = av_frame_alloc();
        if(!ctx->frame_queue.v_buf[i]){
            av_log(NULL, AV_LOG_WARNING, "av_frame_alloc error! \n");
            return -1;
        }
    }

    pthread_mutex_init(&ctx->pkt_queue.audio_mutex, NULL);
    pthread_mutex_init(&ctx->pkt_queue.video_mutex, NULL);

    pthread_cond_init(&ctx->pkt_queue.audio_cond, NULL);
    pthread_cond_init(&ctx->pkt_queue.video_cond, NULL);

    pthread_mutex_init(&ctx->frame_queue.audio_mutex, NULL);
    pthread_mutex_init(&ctx->frame_queue.video_mutex, NULL);

    pthread_cond_init(&ctx->frame_queue.audio_cond, NULL);
    pthread_cond_init(&ctx->frame_queue.video_cond, NULL);
    // SDL INIT
    // initiate SDL and av and prepare
    if(!SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO)){
        SDL_Log("SDL_Init failed! %s", SDL_GetError());
        return -1;
    };
    //
    #ifdef __cplusplus
    of.open(of_name, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    #else
    FILE *of = fopen(of_name, "wb+");
    if(!of){
        return -1;
    }
    #endif

    return 0;
}

int Demux_swr_start(AV_Context *ctx, const char *filename)
{
    //
    int ret;
    ret = open_containerFormat_get_av_index(&ctx->ifc, filename, &ctx->audio_index, &ctx->video_index);
    if(ret < 0){
        av_log(NULL, AV_LOG_ERROR, "open_containerFormat_get_audiostream_index error! \n");
        return -1;
    }
    //
    ret = open_avcode_context(ctx->ifc, &ctx->acode, &ctx->acodec, ctx->audio_index);
    if(ret < 0){
        av_log(NULL, AV_LOG_ERROR, "open_acode_context audio_stream acodec error! \n");
        return -1;
    }

    ret = open_avcode_context(ctx->ifc, &ctx->vcode, &ctx->vcodec, ctx->video_index);
    if(ret < 0){
        av_log(NULL, AV_LOG_ERROR, "open_acode_context video_stream acodec error! \n");
        return -1;
    }

    /* create resampler context */
    swr_ctx = swr_alloc();
    if (!swr_ctx) {
        av_log(NULL, AV_LOG_ERROR, "swr_ctx error! \n");
        return -1;
    }

    /* set options */
    
    av_opt_set_chlayout(swr_ctx, "in_chlayout",    &ctx->acodec->ch_layout, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate",       ctx->acodec->sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", ctx->acodec->sample_fmt, 0);

    av_opt_set_chlayout(swr_ctx, "out_chlayout",    &dst_ch_layout, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate",       ctx->acodec->sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", dst_sample_fmt, 0);

    /* initialize the resampling context */
    if ((ret = swr_init(swr_ctx)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "swr_init error! \n");
        return -1;
    }

    return 0;
}

int sdl_prepare_init(AV_Context *ctx)
{
    #if 1
    audio_spec.channels = ctx->acodec->ch_layout.nb_channels;
    audio_spec.freq = ctx->acodec->sample_rate;
    if(ctx->acodec->sample_fmt == AV_SAMPLE_FMT_FLTP){
        audio_spec.format = SDL_AUDIO_F32LE;
        av_log(NULL, AV_LOG_INFO, "SDL_AUDIO_F32LE");
    }else if(ctx->acodec->sample_fmt == AV_SAMPLE_FMT_S16){
        audio_spec.format = SDL_AUDIO_S16LE;
        av_log(NULL, AV_LOG_INFO, "SDL_AUDIO_S16LE");
    }else if(ctx->acodec->sample_fmt == AV_SAMPLE_FMT_S32){
        audio_spec.format = SDL_AUDIO_S32LE;
        av_log(NULL, AV_LOG_INFO, "SDL_AUDIO_S32LE");
    }else if(ctx->acodec->sample_fmt == AV_SAMPLE_FMT_S16P){
        audio_spec.format = SDL_AUDIO_S16LE;
        av_log(NULL, AV_LOG_INFO, "SDL_AUDIO_S16LE");
    }else if(ctx->acodec->sample_fmt == AV_SAMPLE_FMT_S32P){
        audio_spec.format = SDL_AUDIO_S32LE;
        av_log(NULL, AV_LOG_INFO, "SDL_AUDIO_S32LE");
    }
    #else
    audio_spec.channels = dst_ch_layout.nb_channels;
    audio_spec.freq = ctx.acodec->sample_rate;
    audio_spec.format = SDL_AUDIO_S16LE;
    #endif

    audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio_spec, NULL, NULL);
    if(!audio_stream){
        SDL_Log("SDL_OpenAudioDeviceStream failed! %s", SDL_GetError());
        return -1;
    }

    // play audio
    SDL_ResumeAudioStreamDevice(audio_stream);

    // create windows
    if(!SDL_CreateWindowAndRenderer("av_play", WINDOW_WIDTH, WINDOW_HEIGHT, 0, &window, &renderer)){
        SDL_Log("SDL_CreateWindowAndRenderer error  %s", SDL_GetError());
        return -1;
    }

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, ctx->vcodec->width, ctx->vcodec->height);
    if(!texture){
        SDL_Log("SDL_CreateTexture error  %s", SDL_GetError());
        return -1;
    }

    return 0;
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
    if (av_sample_fmt_is_planar((AVSampleFormat)(frame)->format)) {
        for(int i = 0; i < frame->nb_samples; i++){
            for(int j = 0; j < frame->ch_layout.nb_channels; j++){
                // const char *p = reinterpret_cast<const char *>(frame->data[j] + i * bytes_per_sample);
                // of.write(p, bytes_per_sample);
            
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
    }else
    {
        memcpy(audio_buf ,frame->data[0], buffer_size);
    }
    SDL_PutAudioStreamData(audio_stream, audio_buf, buffer_size);
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
        // buffer_ready = true; 

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
        // ctx->q.
    }
    return 0;
}

int main(int argc, char *argv[])
{
    // enviroment prepare
    AV_Context ctx = {0};
    int ret;
    ctx.audio_index = -1, ctx.video_index = -1;
    AVPacket *pkt = NULL;
    AVFrame *frame = NULL;
    //
    if(argc < 3){
        std::cerr << "main Args error !" << std::endl;
        return -1;
    }
    const char *filename = argv[1], *of_name = argv[2];

    ret = init(&ctx, of_name);
    if(ret < 0){
        av_log(NULL, AV_LOG_ERROR, "init error!\n");
        goto __failed;
    }

    ret = Demux_swr_start(&ctx, filename);
    if(ret < 0){
        av_log(NULL, AV_LOG_ERROR, "Demux_swr_start error!\n");
        goto __failed;
    }
    // open audioStream
    audio_buf = (uint8_t *)malloc(8192);
    audio_pos = audio_buf;

    sdl_prepare_init(&ctx);
    if(ret < 0){
        av_log(NULL, AV_LOG_ERROR, "sdl_prepare_init error!\n");
        goto __failed;
    }

    SDL_Delay(1000);

    // decode
    pkt = av_packet_alloc();
    if(!pkt){
        av_log(NULL, AV_LOG_WARNING, "av_packet_alloc error! \n");
    }

    frame = av_frame_alloc();
    if(!frame){
        av_log(NULL, AV_LOG_WARNING, "av_frame_alloc error! \n");
    }

    if(pthread_create(&rpid, NULL, read_pkt, &ctx) < 0){
        av_log(NULL, AV_LOG_ERROR, "pthread_create error!\n");
        return -1;
    }
    if(pthread_create(&ap_pid, NULL, audio_play, &ctx) < 0){
        av_log(NULL, AV_LOG_ERROR, "pthread_create error!\n");
        return -1;
    }
    if(pthread_create(&vpid, NULL, video_process_frame_t, &ctx) < 0){
        av_log(NULL, AV_LOG_ERROR, "pthread_create error!\n");
        return -1;
    }
    if(pthread_create(&apid, NULL, audio_process_frame_t, &ctx) < 0){
        av_log(NULL, AV_LOG_ERROR, "pthread_create error!\n");
        return -1;
    }

    // if(pthread_create(&e_pid, NULL, event_process_t, &ctx) < 0){
    //     av_log(NULL, AV_LOG_ERROR, "pthread_create error!\n");
    //     return -1;
    // }
    // pthread_detach(rpid);
    // pthread_detach(ap_pid);
    // pthread_detach(vpid);
    // pthread_detach(apid);
    
    // SDL 渲染只能在主线程, 而音频推流随意在哪个线程
    int viq_index;
    SDL_Event event;
    while(running){
        while(SDL_PollEvent(&event)){
            switch(event.type){
                case SDL_EVENT_QUIT:
                    running = false;
                    ap_running = false;
                    ctx.read_pkt_flags = false;
                    ctx.audio_pkt_decode_flags = false;
                    ctx.video_pkt_decode_flags = false;

                    break;
                case SDL_EVENT_KEY_DOWN:
                    if(event.key.key == SDLK_ESCAPE){
                        running = false;
                        ap_running = false;
                        ctx.read_pkt_flags = false; 
                        ctx.audio_pkt_decode_flags = false;
                        ctx.video_pkt_decode_flags = false;
                    }
                    break;
                default:
                    break;
            }
        }
        if(running == false){
            pthread_cond_signal(&ctx.pkt_queue.video_cond);
            break;
        }
        pthread_mutex_lock(&ctx.frame_queue.video_mutex);
        // 当前环形队列为空， 则应该等待生产者生产video_frame 到video_queue
        while(ctx.frame_queue.v_buf_front == ctx.frame_queue.v_buf_rear){
            if(ctx.video_pkt_decode_flags == false){
                pthread_mutex_unlock(&ctx.frame_queue.video_mutex);
                goto __finished;
            }
            pthread_cond_wait(&ctx.frame_queue.video_cond, &ctx.frame_queue.video_mutex);
        }
        /* consume */
        viq_index = ctx.frame_queue.v_buf_front;
        ctx.frame_queue.v_buf_front = (ctx.frame_queue.v_buf_front + 1) % VIDEO_BUFFER_SIZE;

        av_log(NULL, AV_LOG_INFO, "consume %d video_frame\n", ctx.frame_queue.v_buf_front);

        AVFrame *frame = ctx.frame_queue.v_buf[viq_index];
        double pts = frame->pts * av_q2d(ctx.ifc->streams[ctx.video_index]->time_base);
        double delay = pts - last_frame_pts;
        // double delay = pts - real_audio_clock;
        if(delay <= 0 || delay > 1.0) delay = last_delay;
        last_delay = delay;
        last_frame_pts = pts;

        double diff = pts - audio_clock;
        double sync_thresh = (delay > 0.01) ? 0.01 : delay;
        if(fabs(diff) < 10.0){
            if (diff <= -sync_thresh)
                delay = delay / 2.0;
            else if (diff >= sync_thresh)
                delay = delay * 2.0;
        }
        SDL_Delay(delay * 1000);
        av_log(NULL, AV_LOG_INFO, "video_frame pts = %.4f \n", pts);
        av_log(NULL, AV_LOG_INFO, "real_audio_clock %.4f \n", real_audio_clock);
        av_log(NULL, AV_LOG_INFO, "delay %.4f \n", delay);

        // if (delay > 0) {
        //     // 视频比音频快，等待 delay 秒
        //     av_usleep(delay * 1e6);
        // } else {
        //     // // 视频太慢，可能要丢帧
        //     // if (fabs(delay) > 0.02) {
        //     //     // 丢帧逻辑（可选）
        //     //     pthread_cond_signal(&ctx.frame_queue.video_cond);
        //     //     pthread_mutex_unlock(&ctx.frame_queue.video_mutex);
        //     //     av_frame_unref(ctx.frame_queue.v_buf[viq_index]);
        //     //     continue;
        //     // }
        // }
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
        
        pthread_cond_signal(&ctx.frame_queue.video_cond);
        pthread_mutex_unlock(&ctx.frame_queue.video_mutex);
        av_frame_unref(ctx.frame_queue.v_buf[viq_index]);

    }

__finished:
    av_log(NULL, AV_LOG_INFO, "video frame is fullly alread processed!\n");
    av_log(NULL, AV_LOG_INFO, "audio_pkts = %ld, video_pkts = %ld\n", audio_pkts, video_pkts);
    av_log(NULL, AV_LOG_INFO, "pkt_queue_a_rear = %d pkt_queue_a_front = %d pkt_queue_v_buf_rear = %d pkt_queue_v_buf_front = %d\n", 
        ctx.pkt_queue.a_buf_rear, ctx.pkt_queue.a_buf_front, ctx.pkt_queue.v_buf_rear, ctx.pkt_queue.v_buf_front);

    av_log(NULL, AV_LOG_INFO, "audio_frames = %ld, video_frames = %ld\n", audio_frames, video_frames);
    av_log(NULL, AV_LOG_INFO, "frame_queue_a_rear = %d frame_queue_a_front = %d frame_queue_v_buf_rear = %d frame_queue_v_buf_front = %d\n", 
        ctx.frame_queue.a_buf_rear, ctx.frame_queue.a_buf_front, ctx.frame_queue.v_buf_rear, ctx.frame_queue.v_buf_front);

    // pthread_join(e_pid, NULL);
    pthread_join(rpid, NULL);
    pthread_join(ap_pid, NULL);
    pthread_join(vpid, NULL);
    pthread_join(apid, NULL);
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
        av_packet_free(&ctx.pkt_queue.a_buf[i]);
        av_packet_free(&ctx.pkt_queue.v_buf[i]);
    }

    for(int i = 0; i < AUDIO_BUFFER_SIZE; i++){
        av_frame_free(&ctx.frame_queue.a_buf[i]);
        av_frame_free(&ctx.frame_queue.v_buf[i]);

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