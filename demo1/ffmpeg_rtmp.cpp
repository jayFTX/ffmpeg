#include <iostream>
#include <string>

#ifdef __cplusplus
extern "C"{
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/time.h>

#ifdef __cplusplus
}
#endif

using namespace std;

int main(int argc, char *argv[])
{
    if(argc < 2){
        cout << "args errors" << endl;
        return -1;
    }
    const char *filename, *url;
    AVFormatContext *ifmct = NULL, *ofmct = NULL;
    const AVOutputFormat *ofmt = NULL;
    AVCodecContext *cc = NULL;
    const AVCodec *codec = NULL;
    AVFrame *frame = NULL;
    AVPacket *pkt = NULL;
    int ret;
    char error_str[AV_ERROR_MAX_STRING_SIZE];
    int video_index = -1, frame_index = 0;

    avformat_network_init();
    filename = argv[1];
    url = argv[2];
    // 1.open input container
    ret = avformat_open_input(&ifmct, filename, NULL, NULL);
    if(ret < 0){
        fprintf(stdout, "avformat_open_input error \n");
        return -1;
    }

    ret = avformat_find_stream_info(ifmct, NULL);
    if(ret < 0){
        fprintf(stdout, "avformat_find_stream_info error\n");
        return -1;
    }

    ret = av_find_best_stream(ifmct, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if(ret < 0){
        fprintf(stdout, "avformat_find_stream_info error\n");
        return -1;
    }
    video_index = ret;

    av_dump_format(ifmct, 0, filename, 0);

    // 2 open out container
    ret = avformat_alloc_output_context2(&ofmct, NULL, "flv", url);
    if(ret < 0){
        fprintf(stdout, "avformat_alloc_output_context2 error \n");
        return -1;
    }

    ofmt = ofmct->oformat;

    for(int i = 0; i < ifmct->nb_streams; i++){
        // create new stream
        AVStream *outStream = NULL;
        outStream = avformat_new_stream(ofmct, NULL);
        if(!outStream){
            fprintf(stdout, "avformat_new_stream error \n");
            return -1;
        }

        ret = avcodec_parameters_copy(outStream->codecpar, ifmct->streams[i]->codecpar);
        if(ret < 0){
            fprintf(stdout, "avcodec_parameters_copy error \n");
            return -1;
        }
        // outStream->codecpar->codec_tag = 0;
    }
    av_dump_format(ofmct, 0, url, 1);


    // rtmp
    if (!(ofmt->flags & AVFMT_NOFILE)) {
		ret = avio_open(&ofmct->pb, url, AVIO_FLAG_WRITE);
		if (ret < 0) {
            av_strerror(ret, error_str, sizeof(error_str));
            av_log(NULL, AV_LOG_ERROR, "Can't open filename: %d %s\n", ret, error_str);
            return -1;
		}
	}

    ret = avformat_write_header(ofmct, NULL);
    if(ret < 0){
        fprintf(stdout, "avformat_write_header error \n");
        return -1;
    }
    cout << "avformat_write_header succeed!" << endl;
    pkt = av_packet_alloc();
    if(!pkt){
        fprintf(stdout, "av_packet_alloc error \n");
        return -1;
    }
    int64_t start_time= av_gettime();
    while(true){
        AVStream *in_stream, *out_stream;
        ret = av_read_frame(ifmct, pkt);
        if(ret < 0){
            // fprintf(stdout, "av_read_frame error \n");
            break;
        }
        if(pkt->pts == AV_NOPTS_VALUE){
			//Write PTS
			AVRational time_base1 = ifmct->streams[video_index]->time_base;
			//Duration between 2 frames (us)
			int64_t calc_duration = (double)AV_TIME_BASE / av_q2d(ifmct->streams[video_index]->r_frame_rate);
			//Parameters
			pkt->pts = (double)(frame_index*calc_duration) / (double)(av_q2d(time_base1) * AV_TIME_BASE);
			pkt->dts = pkt->pts;
			pkt->duration = (double)calc_duration / (double)(av_q2d(time_base1) * AV_TIME_BASE);
		}
		//Important:Delay
		if(pkt->stream_index == video_index){
			AVRational time_base = ifmct->streams[video_index]->time_base;
			AVRational time_base_q = {1, AV_TIME_BASE};
			int64_t pts_time = av_rescale_q(pkt->dts, time_base, time_base_q);
			int64_t now_time = av_gettime() - start_time;
			if (pts_time > now_time)
				av_usleep(pts_time - now_time);

		}
        in_stream  = ifmct->streams[pkt->stream_index];
		out_stream = ofmct->streams[pkt->stream_index];

        pkt->pts = av_rescale_q_rnd(pkt->pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
		pkt->dts = av_rescale_q_rnd(pkt->dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
		pkt->duration = av_rescale_q(pkt->duration, in_stream->time_base, out_stream->time_base);
		pkt->pos = -1;
		//Print to Screen
		if(pkt->stream_index == video_index){
			printf("Send %8d video frames to output URL\n", frame_index);
			frame_index++;
		}

        ret = av_interleaved_write_frame(ofmct, pkt);
        if(ret < 0){
            fprintf(stdout, "av_interleaved_write_frame error \n");
            break;
        }
        av_packet_unref(pkt);
    }

    av_write_trailer(ofmct);
    
    
    avformat_free_context(ifmct);
    avformat_free_context(ofmct);
    // avcodec_free_context(&cc);


    av_packet_free(&pkt);
    av_frame_free(&frame);
    avformat_network_deinit();
    
    return 0;
}