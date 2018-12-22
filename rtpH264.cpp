//
// Created by wunl on 18-12-21.
//
#include "rtpH264.h"
#include "rtpdataheader.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/uio.h>


static AVCodecContext *context = NULL;
static AVCodecParserContext *parser = NULL;
static AVPacket *pkt = NULL;
static AVFrame *frame = NULL;

FILE *f;

static void pgm_save (unsigned char *buf, int wrap, int xsize, int ysize,
                      char *filename)
{
    FILE *f;
    int i;

    f = fopen(filename, "w");
    fprintf(f, "P5\n%d %d\n%d\n", xsize, ysize, 255);
    for (i = 0; i < ysize; i ++)
        fwrite(buf + i * wrap, 1, xsize, f);
    fclose(f);
}

static void decode (AVCodecContext *dec_ctx, AVFrame *frame, AVPacket *pkt,
                    const char *filename)
{
    char buf[1024];
    int ret;

    ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0)
    {
        fprintf(stderr, "Error sending a packet for decoding\n");
        exit(1);
    }

    while (ret >= 0)
    {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0)
        {
            fprintf(stderr, "Error during decoding\n");
            exit(1);
        }

        printf("saving frame %3d\n", dec_ctx->frame_number);
        fflush(stdout);

//        for (int i = 0; i < 1080; i ++)
//        {
//            fwrite(frame->data[0] + frame->linesize[0] * i, 1, 1920, f);
//        }
//        for (int i = 0; i < 1080 / 2; i ++)
//        {
//            fwrite(frame->data[1] + frame->linesize[1] * i, 1, 1920 / 2, f);
//        }
//        for (int i = 0; i < 1080 / 2; i ++)
//        {
//            fwrite(frame->data[2] + frame->linesize[2] * i, 1, 1920 / 2, f);
//        }

    }
}


void RtpH264_Init ()
{

    /* register all the codecs */
    avcodec_register_all();

    pkt = av_packet_alloc();
    if (! pkt)
        exit(1);


    AVCodec *codec = avcodec_find_decoder_by_name("h264");
    if (! codec)
    {
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }

    parser = av_parser_init(codec->id);
    if (! parser)
    {
        fprintf(stderr, "parser not found\n");
        exit(1);
    }

    context = avcodec_alloc_context3(codec);

    /* open it */
    if (avcodec_open2(context, codec, NULL) < 0)
    {
        fprintf(stderr, "could not open codec\n");
        exit(EXIT_FAILURE);
    }

    char *filename = const_cast<char *>("/home/wunl/Videos/test.yuv");
    f = fopen(filename, "w");
    if (! f)
    {
        fprintf(stderr, "Could not open %s\n", filename);
        exit(1);
    }

    frame = av_frame_alloc();
    if (! frame)
    {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }
}

void RtpH264_Deinit ()
{
    // Mp4mux_Close();

    avcodec_close(context);
    av_free(context);

    av_parser_close(parser);
    av_frame_free(&frame);
    av_packet_free(&pkt);
}

static int bStop = 0;

void RtpH264_Stop ()
{
    bStop = 1;
}

void RtpH264_Run (int sfd, RtpH264_OnPicture onPicture)
{
#define INBUF_SIZE (1024 * 128)
    uint8_t inbuf[INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
    /* set end of buffer to 0 (this ensures that no overreading happens for damaged mpeg streams) */
    memset(inbuf + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    uint8_t *data;
    size_t data_size;
    int ret;

    AVFrame *picture = av_frame_alloc();
    AVPacket avpkt;
    av_init_packet(&avpkt);
    int frame_count = 0;

    unsigned short sequence = 0;
    unsigned int timestamp = 0;

    while (1)
    {
        //int ready = 0;
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sfd, &rfds);

        struct timeval timeout;  /*Timer for operation select*/
        timeout.tv_sec = 0;
        timeout.tv_usec = 10000; /*10 ms*/

        if (select(sfd + 1, &rfds, 0, 0, &timeout) <= 0)
        {
            if (bStop)
                break;
            else
                continue;
        }

        if (FD_ISSET(sfd, &rfds) <= 0)
            continue;

        rtp_hdr_t rtp;
        unsigned char buf[64];
        memset(buf, 0, 64);
        int r = recv(sfd, buf, sizeof(rtp_hdr_t) + 8,
                     MSG_PEEK);  /*  Peek data from socket, so that we could determin how to read data from socket*/
        if (r <= sizeof(rtp_hdr_t) || r == - 1)
        {
            recv(sfd, buf, sizeof(rtp_hdr_t) + 8, 0); /*Read invalid packet*/
            printf("Warning !!! Invalid packet\n");
            continue; /*Invalid packet ???*/
        }

        int ready = 0;

        /*  Handle H.264 RTP Header */
        /* +---------------+
        *  |0|1|2|3|4|5|6|7|
        *  +-+-+-+-+-+-+-+-+
        *  |F|NRI|  Type   |
        *  +---------------+
        *
        * F must be 0.
        */
        unsigned char nal_ref_idc, nal_unit_type;
        unsigned char *header = buf + sizeof(rtp_hdr_t);  /*  NAL Header  */
        nal_ref_idc = (header[0] & 0x60) >> 5;  /*  NRI */
//printf("nal_ref_idc = %d\n", nal_ref_idc);
        nal_unit_type = header[0] & 0x1f;       /*  Type  */
//printf("nal_unit_type = %d\n", nal_unit_type);
        switch (nal_unit_type)
        {
            case 0:
            case 30:
            case 31:
                /* undefined */
                break;
            case 25:
                /* STAP-B    Single-time aggregation packet     5.7.1 */
                /* 2 byte extra header for DON */
                /* fallthrough */
            case 24:
                /* STAP-A    Single-time aggregation packet     5.7.1 */
            {
//                unsigned char tmpbuf[1000];
//                memset(tmpbuf, 0, 1000);
//                r = static_cast<int>(recv(sfd, tmpbuf, sizeof(rtp_hdr_t) + 100, 0));
//                break;


                printf("nal_unit_type = %d\n", nal_unit_type);
                int offset = 0;
                int offset1 = 0;
                size_t nal1len = 0;
                size_t nal2len = 0;

                unsigned char tmpbuf[1000];
                memset(tmpbuf, 0, 1000);
                r = static_cast<int>(recv(sfd, tmpbuf, sizeof(rtp_hdr_t) + 100, 0));


                inbuf[0] = 0x00;
                inbuf[1] = 0x00;
                inbuf[2] = 0x00;
                inbuf[3] = 0x01;

                offset += 4;
                offset1 += sizeof(rtp_hdr_t) + 3;
                nal1len = tmpbuf[sizeof(rtp_hdr_t) + 2];

                memcpy(inbuf + offset, tmpbuf + offset1, nal1len);

                offset += nal1len;
                offset1 += nal1len;
                offset1 += 2;
                nal2len = tmpbuf[sizeof(rtp_hdr_t) + 3 + nal1len + 1];

                inbuf[offset + 0] = 0x00;
                inbuf[offset + 1] = 0x00;
                inbuf[offset + 2] = 0x00;
                inbuf[offset + 3] = 0x01;

                offset += 4;

                memcpy(inbuf + offset, tmpbuf + offset1, nal2len);

                avpkt.size = (r - sizeof(rtp_hdr_t) + 8 - 5);
                avpkt.data = inbuf;

                ready = 1;  /*We are done, go to decode*/
                break;
            }
            case 26:
                /* MTAP16    Multi-time aggregation packet      5.7.2 */
                /* fallthrough, not implemented */
            case 27:
                /* MTAP24    Multi-time aggregation packet      5.7.2 */
                break;
            case 28:
                /* FU-A      Fragmentation unit                 5.8 */
            case 29:
            {
                /* FU-B      Fragmentation unit                 5.8 */
                /* +---------------+
                * |0|1|2|3|4|5|6|7|
                * +-+-+-+-+-+-+-+-+
                * |S|E|R|  Type   |
                * +---------------+
                *
                * R is reserved and always 0
                */

                /*Really read packet*/
                struct iovec data[3];

                data[0].iov_base = &rtp;
                data[0].iov_len = sizeof(rtp_hdr_t);

                if (nal_unit_type == 28)
                {
                    /* strip off FU indicator and FU header bytes */
                    data[1].iov_base = buf;
                    data[1].iov_len = 2;
                } else if (nal_unit_type == 29)
                {
                    /* strip off FU indicator and FU header and DON bytes */
                    data[1].iov_base = buf;
                    data[1].iov_len = 4;
                }

                /* NAL unit starts here */
                if ((header[1] & 0x80) == 0x80)
                {
                    data[2].iov_base = &inbuf[5];
                    data[2].iov_len = INBUF_SIZE - 5;

                    r = readv(sfd, data, 3);
                    if (r <= (sizeof(rtp_hdr_t) + 2))
                    {
                        printf("Socket read fail !!!\n");
                        goto cleanup;
                    }

                    unsigned char fu_indicator = buf[0];
                    unsigned char fu_header = buf[1];

                    timestamp = ntohl(rtp.ts);
                    sequence = ntohs(rtp.seq);

                    inbuf[0] = 0x00;
                    inbuf[1] = 0x00;
                    inbuf[2] = 0x00;
                    inbuf[3] = 0x01;
                    inbuf[4] = (fu_indicator & 0xe0) | (fu_header & 0x1f);
                    if (nal_unit_type == 28)
                        avpkt.size = (r - sizeof(rtp_hdr_t) - 2 + 5);
                    else if (nal_unit_type == 29)
                        avpkt.size = (r - sizeof(rtp_hdr_t) - 4 + 5);
                    avpkt.data = inbuf;
//printf("nalu = %d\n", fu_header & 0x1f);

                    if ((fu_header & 0x1f) == 7)
                        avpkt.flags |= AV_PKT_FLAG_KEY;
//          avpkt.pts = frame_count++;
//printf("avpkt.pts = %d\n", avpkt.pts);
                    break;
                } else
                {
                    data[2].iov_base = inbuf + avpkt.size;
                    data[2].iov_len = INBUF_SIZE - avpkt.size;

                    r = readv(sfd, data, 3);

                    //unsigned char fu_indicator = buf[0];
                    unsigned char fu_header = buf[1];

                    if (r <= (sizeof(rtp_hdr_t) + 2) || r == - 1)
                    {
                        printf("Socket read fail !!!\n");
                        goto cleanup;
                    }

                    if (ntohl(rtp.ts) != timestamp)
                    {
                        printf("Miss match timestamp %d ( expect %d )\n", ntohl(rtp.ts), timestamp);
                    }

                    if (ntohs(rtp.seq) != ++ sequence)
                    {
                        printf("Wrong sequence number %u ( expect %u )\n", ntohs(rtp.seq), sequence);
                    }

                    if (nal_unit_type == 28)
                        avpkt.size += (r - sizeof(rtp_hdr_t) - 2);
                    else if (nal_unit_type == 29)
                        avpkt.size += (r - sizeof(rtp_hdr_t) - 4);
//printf("frame size : %d\n", avpkt.size);
                }

                /* NAL unit ends  */
                if ((header[1] & 0x40) == 0x40)
                {
                    ready = 1;  /*We are done, go to decode*/
                    break;
                }

                break;
            }
            default:
            {
                /* 1-23   NAL unit  Single NAL unit packet per H.264   5.6 */
                /* the entire payload is the output buffer */
                struct iovec data[2];

                data[0].iov_base = &rtp;
                data[0].iov_len = sizeof(rtp_hdr_t);

                inbuf[0] = 0x00;
                inbuf[1] = 0x00;
                inbuf[2] = 0x00;
                inbuf[3] = 0x01;

                data[1].iov_base = &inbuf[4];
                data[1].iov_len = INBUF_SIZE - 4;

                r = readv(sfd, data, 2);
                if (r <= sizeof(rtp_hdr_t) || r == - 1)
                {
                    printf("Socket read fail !!!\n");
                    goto cleanup;
                }

                avpkt.size = (r - sizeof(rtp_hdr_t) + 4);
                avpkt.data = inbuf;

                ready = 1;  /*We are done, go to decode*/

                break;
            }
        }

        int got_picture;

//if(ready==1)
//{
//    fwrite(avpkt.data, 1, avpkt.size, f);
//}

//        memset(inbuf + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);


        data_size = avpkt.size;
        data = inbuf;
        while (ready && data_size > 0)
        {
            ret = av_parser_parse2(parser, context, &pkt->data, &pkt->size,
                                   data, data_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            if (ret < 0)
            {
                fprintf(stderr, "Error while parsing\n");
                exit(1);
            }
            data += ret;
            data_size -= ret;

            if (pkt->size)
                decode(context, frame, pkt, "/home/wunl/Videos/out.yuv");
        }


/*        while (ready && avpkt.size > 0)
        {
            avpkt.pts = ++ frame_count;
//printf("frame size : %d\n", avpkt.size);
            int len = avcodec_decode_video2(context, picture, &got_picture, &avpkt);

            if (len < 0)
            {
                fprintf(stderr, "Error while decoding frame\n");
                av_init_packet(&avpkt);
                break;
            }

            if (got_picture)
            {
                printf("Decode length : %d\n", len);
                fflush(stdout);
                if (onPicture)
                    onPicture(picture->data[0], picture->linesize[0], context->width, context->height);

                av_init_packet(&avpkt);
                break;
            }
            avpkt.size -= len;
            avpkt.data += len;
        }*/

    }

    cleanup:
    av_free(picture);
    fclose(f);
    return;
}
