#include <iostream>

#ifdef __cpluplus
extern "C"{
#endif
#include <unistd.h>
#include <stdio.h>
#include <librtmp/rtmp.h>

#ifdef __cpluplus
}
#endif

// 大小端字节序转换
#define HTON16(x) ( (x >> 8 & 0x00FF) | (x << 8 & 0xFF00) )
#define HTON24(x) ( (x >> 16 & 0x0000FF) | (x & 0x00FF00) | (x << 16 & 0xFF0000) )
#define HTON32(x) ( (x >> 24 & 0x000000FF) | (x >> 8 & 0x0000FF00) | (x << 8 & 0x00FF0000) | (x << 24 & 0xFF000000) )
#define HTONTIME(x) ( (x >> 16 & 0x000000FF) | (x & 0x0000FF00) | (x << 16 & 0x00FF0000) | (x & 0xFF000000) )

int main(int argc, char *argv[])
{   
    int ret;
    if(argc != 2){
        fprintf(stderr, "main argc error!\n");
        return -1;
    }
    const char *filename = argv[1];
    FILE *f = fopen(filename, "rb");
    if(!f){
        fprintf(stderr, "fopen failed\n");
        return -1;
    }
    RTMP *pRTMP = RTMP_Alloc();
    if(!pRTMP){
        fprintf(stderr, "RTMP_Alloc failed\n");
        return -1;
    }

    RTMP_Init(pRTMP);
    // 设置推流地址
    pRTMP->Link.timeout = 10;
    RTMP_SetupURL(pRTMP, (char*)"rtmp://127.0.0.1:8890/live/a");
    // 开启推流标志
    RTMP_EnableWrite(pRTMP);
    // 连接服务器
    bool b = RTMP_Connect(pRTMP, NULL);
    if (!b){
        fprintf(stderr, "RTMP_Connect failed\n");
        return -1;
    }

    // 连接流地址
    b = RTMP_ConnectStream(pRTMP, 0);
    if (!b){
        fprintf(stderr, "RTMP_ConnectStream failed\n");
        return -1;
    }

    // 跳过FLV文件头的13个字节
    fseek(f, 9, SEEK_SET);
    fseek(f, 4, SEEK_CUR);

    // 初使化RTMP报文
    RTMPPacket packet;
    RTMPPacket_Reset(&packet);
    packet.m_body = NULL;
    packet.m_chunk = NULL;
    packet.m_nInfoField2 = pRTMP->m_stream_id;
    uint32_t starttime = RTMP_GetTime();

    while(true){
        uint8_t tag_type = 0;
        ret = fread(&tag_type, 1, sizeof(uint8_t), f);
        if(ret != sizeof(uint8_t))
            break;
        uint32_t tag_len = 0;
        ret = fread(&tag_len, 1, 3, f);
        if(ret != 3)
            break;
        tag_len = HTON24(tag_len);

        uint32_t timestamp = 0;
        ret = fread(&timestamp, 1, 4, f);
        if(ret != 4)
            break;
        timestamp = HTONTIME(timestamp);

        uint32_t streamid = 0;
        ret = fread(&streamid, 1, 3, f);
        if(ret != 3)
            break;
        streamid = HTON24(streamid);
        
        /**/
        RTMPPacket_Alloc(&packet, tag_len);
        if (fread(packet.m_body, 1, tag_len, f) != tag_len)
            break;
         // 组织报文并发送
        packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
        packet.m_packetType = tag_type;
        packet.m_hasAbsTimestamp = 0;
        packet.m_nChannel = 6;
        packet.m_nTimeStamp = timestamp;
        packet.m_nBodySize = tag_len;

        if (!RTMP_SendPacket(pRTMP, &packet, 0)){
            fprintf(stderr, "RTMP_SendPacket failed\n");
            break;
        }
        printf("send type:[%d] timestamp:[%d] datasize:[%d] \n", tag_type, timestamp, tag_len);

        // 跳过PreTag
        uint32_t pretagsize = 0;
        ret = fread(&pretagsize, 1, 4, f);
        if(ret != 4)
            break;
        pretagsize = HTON32(pretagsize);
        // 延时，避免发送太快
        uint32_t timeago = (RTMP_GetTime() - starttime);
        if (timestamp > 1000 && timeago < timestamp - 1000){
            printf("sleep...\n");
            usleep(100000);
        }
        RTMPPacket_Free(&packet);
    }
    // 关闭连接，释放RTMP上下文
    RTMP_Close(pRTMP);
    RTMP_Free(pRTMP);

    fclose(f);
    return 0;
}