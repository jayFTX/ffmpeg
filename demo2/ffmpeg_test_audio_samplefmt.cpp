#include <iostream>
#include <fstream>
#include <string>
#include <vector>

#ifdef __cplusplus
extern "C"{
#endif
#include <SDL3/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>

#ifdef __cplusplus
}
#endif

//
SwrContext *swr_ctx = NULL;
int dst_rate = 44100;
AVChannelLayout dst_ch_layout = AV_CHANNEL_LAYOUT_MONO;
enum AVSampleFormat dst_sample_fmt = AV_SAMPLE_FMT_S16;
int dst_nb_samples = 0;


void init()
{
    // FFMPEG INIT

    // SDL INIT

}

int main(int argc, char *argv[])
{
    AVChannelLayout src_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    int src_rate = 44100, dst_rate = 44100;
    uint8_t **src_data = NULL, **dst_data = NULL;
    int src_nb_channels = 0, dst_nb_channels = 0;
    int src_linesize, dst_linesize;
    int src_nb_samples = 1024;
    enum AVSampleFormat src_sample_fmt = AV_SAMPLE_FMT_FLTP;
    char buf[64];
    double t;
    int ret;


    /* create resampler context */
    swr_ctx = swr_alloc();
    if (!swr_ctx) {
        fprintf(stderr, "Could not allocate resampler context\n");
        ret = AVERROR(ENOMEM);
        return -1;
    }
    /* set options */
    av_opt_set_chlayout(swr_ctx, "in_chlayout",    &src_ch_layout, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate",       src_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", src_sample_fmt, 0);

    av_opt_set_chlayout(swr_ctx, "out_chlayout",    &dst_ch_layout, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate",       dst_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", dst_sample_fmt, 0);

    /* initialize the resampling context */
    if ((ret = swr_init(swr_ctx)) < 0) {
        fprintf(stderr, "Failed to initialize the resampling context\n");
        return -1;
    }

    src_nb_channels = src_ch_layout.nb_channels;
    ret = av_samples_alloc_array_and_samples(&src_data, &src_linesize, src_nb_channels,
                                             src_nb_samples, src_sample_fmt, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate source samples\n");
        return -1;
    }
    dst_nb_samples = av_rescale_rnd(src_nb_samples, dst_rate, src_rate, AV_ROUND_UP);
    /* buffer is going to be directly written to a rawaudio file, no alignment */
    dst_nb_channels = dst_ch_layout.nb_channels;
    ret = av_samples_alloc_array_and_samples(&dst_data, &dst_linesize, dst_nb_channels,
                                             dst_nb_samples, dst_sample_fmt, 0);


    return 0;
}