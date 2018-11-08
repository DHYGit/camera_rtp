#ifndef _CONSUMERTHREADTOOL_H__
#define _CONSUMERTHREADTOOL_H__
#include "Thread.h"
#include <Argus/Argus.h>
#include <NvVideoEncoder.h>
#include <EGLStream/EGLStream.h>

using namespace Argus;
using namespace EGLStream;

namespace ArgusSamples{

    class ConsumerThread : public Thread
    {
        public:
            explicit ConsumerThread(OutputStream *stream);
            ~ConsumerThread();
            bool isInError()
            {
                return m_gotError;
    	    }
        private:
            virtual bool threadInitialize();
            virtual bool threadExecute();
            virtual bool threadShutdown();
            bool createVideoEncoder();
            void abort();
            static bool encoderCapturePlaneDqCallback(
                            struct v4l2_buffer *v4l2_buf,
            		    NvBuffer *buffer,
                            NvBuffer *shared_buffer,
                            void *arg);
            OutputStream *m_stream;
            UniqueObj<FrameConsumer> m_consumer;
            NvVideoEncoder *m_VideoEncoder;
            std::ofstream *m_outputFile;
            bool m_gotError;
    };
}
bool execute(int num);


#endif
