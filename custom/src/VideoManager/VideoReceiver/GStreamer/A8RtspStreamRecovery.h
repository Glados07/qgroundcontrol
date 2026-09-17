/****************************************************************************
 * Receiver-local A8 RTSP progress observation and bounded session recovery.
 ****************************************************************************/

#pragma once

class GimbalControlSettings;
class VideoReceiver;

class A8RtspStreamRecovery
{
public:
    static void install(VideoReceiver *receiver, void *sink, GimbalControlSettings *settings);
};
