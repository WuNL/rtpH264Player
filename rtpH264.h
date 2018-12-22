//
// Created by wunl on 18-12-21.
//

#ifndef RTPH264_RTPH264_H
#define RTPH264_RTPH264_H

#include "ffHeaders.h"

typedef void (*RtpH264_OnPicture) (unsigned char *data, int lineSize, int width, int height);

void RtpH264_Init ();

void RtpH264_Deinit ();

void RtpH264_Run (int sfd, RtpH264_OnPicture onPicture);

void RtpH264_Stop ();


#endif //RTPH264_RTPH264_H
