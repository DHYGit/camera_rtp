#include "ConsumerThreadTool.h"
#include "ProControl.h"
#include "pro_rtp.h"
#include <EGLStream/NV/ImageNativeBuffer.h>
#include <fstream>
#include <queue>
#include <arpa/inet.h>

using namespace ArgusSamples;

static const int    array_n[8][8] = {
    { 1, 1, 0, 0, 0, 0, 1, 1 },
    { 1, 1, 1, 0, 0, 0, 1, 1 },
    { 1, 1, 1, 1, 0, 0, 1, 1 },
    { 1, 1, 1, 1, 1, 0, 1, 1 },
    { 1, 1, 0, 1, 1, 1, 1, 1 },
    { 1, 1, 0, 0, 1, 1, 1, 1 },
    { 1, 1, 0, 0, 0, 1, 1, 1 },
    { 1, 1, 0, 0, 0, 0, 1, 1 }
};
// Constant configuration.
static const int    MAX_ENCODER_FRAMES = 5;
static const int    Y_INDEX            = 0;
static const int    START_POS          = 32;
static const int    FONT_SIZE          = 64;
static const int    SHIFT_BITS         = 3;

// Configurations which can be overrided by cmdline
int          CAPTURE_TIME = 1; // In seconds.
bool         VERBOSE_ENABLE = false;
bool         DO_CPU_PROCESS = false;

bool DO_STAT = false;

static std::string  OUTPUT_FILENAME ("output.h264");
// Debug print macros.
#define PRODUCER_PRINT(...) printf("PRODUCER: " __VA_ARGS__)
#define CONSUMER_PRINT(...) printf("CONSUMER: " __VA_ARGS__)
#define CHECK_ERROR(expr) \
    do { \
        if ((expr) < 0) { \
            abort(); \
            ORIGINATE_ERROR(#expr " failed"); \
        } \
    } while (0);

int capture_count = 0;
int fps = 0;

extern void LOG(bool flag, std::string str);
extern std::queue <MediaDataStruct> *video_buf_queue;
extern pthread_mutex_t video_buf_queue_lock;
ConsumerThread::ConsumerThread(OutputStream* stream) :
        m_stream(stream),
        m_VideoEncoder(NULL),
        m_outputFile(NULL),
        m_gotError(false)
{
}

ConsumerThread::~ConsumerThread()
{
    if (m_VideoEncoder)
    {
        if (DO_STAT)
             m_VideoEncoder->printProfilingStats(std::cout);
        delete m_VideoEncoder;
    }

    if (m_outputFile)
        delete m_outputFile;
}

bool ConsumerThread::threadInitialize()
{
    std::string function = __FUNCTION__;
    // Create the FrameConsumer.
    m_consumer = UniqueObj<FrameConsumer>(FrameConsumer::create(m_stream));
    if (!m_consumer){
        LOG(false, function + " Failed to create FrameConsumer");
    }else{
        LOG(true, function + " create FrameConsumer success");
    }
    // Create Video Encoder
    if (!createVideoEncoder()){
        LOG(false, function + " Failed to create video m_VideoEncoderoder");
    }else{
        LOG(true, function + " create video encoder success");
    }
#if DEBUG
    // Create output file
    m_outputFile = new std::ofstream(OUTPUT_FILENAME.c_str());
    if (!m_outputFile){
        LOG(false, function + " Failed to open output file.");
    }else{
        LOG(true, function + " open output file success");
    }
#endif
    // Stream on
    int e = m_VideoEncoder->output_plane.setStreamStatus(true);
    if (e < 0){
        LOG(false, function + " Failed to stream on output plane");
    }else{
        LOG(true, function + " stream on output plane success");
    }
    e = m_VideoEncoder->capture_plane.setStreamStatus(true);
    if (e < 0){
        LOG(false, function + " Failed to stream on capture plane");
    }else{
        LOG(true, function + " stream on capture plane success");
    }
    // Set video encoder callback
    m_VideoEncoder->capture_plane.setDQThreadCallback(encoderCapturePlaneDqCallback);

    // startDQThread starts a thread internally which calls the
    // encoderCapturePlaneDqCallback whenever a buffer is dequeued
    // on the plane
    m_VideoEncoder->capture_plane.startDQThread(this);
    // Enqueue all the empty capture plane buffers
    for (uint32_t i = 0; i < m_VideoEncoder->capture_plane.getNumBuffers(); i++)
    {
        struct v4l2_buffer v4l2_buf;
        struct v4l2_plane planes[MAX_PLANES];

        memset(&v4l2_buf, 0, sizeof(v4l2_buf));
        memset(planes, 0, MAX_PLANES * sizeof(struct v4l2_plane));

        v4l2_buf.index = i;
        v4l2_buf.m.planes = planes;

        CHECK_ERROR(m_VideoEncoder->capture_plane.qBuffer(v4l2_buf, NULL));
    }
    return true;
}

bool ConsumerThread::threadExecute()
{
    std::string function = __FUNCTION__;
    IStream *iStream = interface_cast<IStream>(m_stream);
    IFrameConsumer *iFrameConsumer = interface_cast<IFrameConsumer>(m_consumer);

    // Wait until the producer has connected to the stream.
    CONSUMER_PRINT("Waiting until producer is connected...\n");
    if (iStream->waitUntilConnected() != STATUS_OK){
        LOG(false, function + " Stream failed to connect.");
    }else{
        LOG(true, function + " Stream connect success");
    }
    CONSUMER_PRINT("Producer has connected; continuing.\n");

    int bufferIndex;

    bufferIndex = 0;
    // Keep acquire frames and queue into encoder
    while (!m_gotError)
    {
        NvBuffer *buffer;
        int fd = -1;
        capture_count++;
        struct v4l2_buffer v4l2_buf;
        struct v4l2_plane planes[MAX_PLANES];

        memset(&v4l2_buf, 0, sizeof(v4l2_buf));
        memset(planes, 0, MAX_PLANES * sizeof(struct v4l2_plane));

        v4l2_buf.m.planes = planes;

        // Check if we need dqBuffer first
        if (bufferIndex < MAX_ENCODER_FRAMES &&
            m_VideoEncoder->output_plane.getNumQueuedBuffers() <
            m_VideoEncoder->output_plane.getNumBuffers())
        {
            // The queue is not full, no need to dqBuffer
            // Prepare buffer index for the following qBuffer
            v4l2_buf.index = bufferIndex++;
        }
        else
        {
            // Output plane full or max outstanding number reached
            //printf("Output plane full or max outstanding number reached\n");
            CHECK_ERROR(m_VideoEncoder->output_plane.dqBuffer(v4l2_buf, &buffer,
                                                              NULL, 10));
            // Release the frame.
            fd = v4l2_buf.m.planes[0].m.fd;
            NvBufferDestroy(fd);
            if (VERBOSE_ENABLE)
                CONSUMER_PRINT("Released frame. %d\n", fd);
        }
        // Acquire a frame.
        UniqueObj<Frame> frame(iFrameConsumer->acquireFrame());
        IFrame *iFrame = interface_cast<IFrame>(frame);
        if (!iFrame)
        {
            // Send EOS
            v4l2_buf.m.planes[0].m.fd = fd;
            v4l2_buf.m.planes[0].bytesused = 0;
            CHECK_ERROR(m_VideoEncoder->output_plane.qBuffer(v4l2_buf, NULL));
            printf("send EOS\n");
            break;
        }

        // Get the IImageNativeBuffer extension interface and create the fd.
        NV::IImageNativeBuffer *iNativeBuffer = interface_cast<NV::IImageNativeBuffer>(iFrame->getImage());
        if (!iNativeBuffer){
            LOG(false, function + " IImageNativeBuffer not supported by Image.");
        }
        fd = iNativeBuffer->createNvBuffer(Argus::Size2D<uint32_t>(DST_WIDTH, DST_HEIGHT),
                                           NvBufferColorFormat_YUV420,
                                           (DO_CPU_PROCESS)?NvBufferLayout_Pitch:NvBufferLayout_BlockLinear);
        if (VERBOSE_ENABLE)
            CONSUMER_PRINT("Acquired Frame. %d\n", fd);
        if (DO_CPU_PROCESS) {
            NvBufferParams par;
            NvBufferGetParams (fd, &par);
            void *ptr_y;
            uint8_t *ptr_cur;
            int i, j, a, b;
            NvBufferMemMap(fd, Y_INDEX, NvBufferMem_Write, &ptr_y);
            NvBufferMemSyncForCpu(fd, Y_INDEX, &ptr_y);
            ptr_cur = (uint8_t *)ptr_y + par.pitch[Y_INDEX]*START_POS + START_POS;

            // overwrite some pixels to put an 'N' on each Y plane
            // scan array_n to decide which pixel should be overwritten
            for (i=0; i < FONT_SIZE; i++) {
                for (j=0; j < FONT_SIZE; j++) {
                    a = i>>SHIFT_BITS;
                    b = j>>SHIFT_BITS;
                    if (array_n[a][b])
                        (*ptr_cur) = 0xff; // white color
                    ptr_cur++;
                }
                ptr_cur = (uint8_t *)ptr_y + par.pitch[Y_INDEX]*(START_POS + i)  + START_POS;
            }
            NvBufferMemSyncForDevice (fd, Y_INDEX, &ptr_y);
            NvBufferMemUnMap(fd, Y_INDEX, &ptr_y);
        }

        // Push the frame into V4L2.
        v4l2_buf.m.planes[0].m.fd = fd;
        v4l2_buf.m.planes[0].bytesused = 1; // byteused must be non-zero
        CHECK_ERROR(m_VideoEncoder->output_plane.qBuffer(v4l2_buf, NULL));
    }
    // Wait till capture plane DQ Thread finishes
    // i.e. all the capture plane buffers are dequeued
    m_VideoEncoder->capture_plane.waitForDQThread(2000);
    CONSUMER_PRINT("Done.\n");
    requestShutdown();
    return true;
}

bool ConsumerThread::threadShutdown()
{
    return true;
}

bool ConsumerThread::createVideoEncoder()
{
    std::string function = __FUNCTION__;
    int ret = 0;

    m_VideoEncoder = NvVideoEncoder::createVideoEncoder("enc0");
    if (!m_VideoEncoder){
        LOG(false, function + " Could not create m_VideoEncoderoder");
        return false;
    }else{ 
        LOG(true, function + " create m_VideoEncoder success");
    }
    if (DO_STAT)
        m_VideoEncoder->enableProfiling();

    ret = m_VideoEncoder->setCapturePlaneFormat(ENCODER_PIXFMT, DST_WIDTH,
                                    DST_HEIGHT, 2 * 1024 * 1024);
    if (ret < 0){
        LOG(false, function + " could not set capture plane format");
        return false;
    }else{
        LOG(true, function + " set capture plane format success");
    }
    ret = m_VideoEncoder->setOutputPlaneFormat(V4L2_PIX_FMT_YUV420M, DST_WIDTH,
                                    DST_HEIGHT);
    if (ret < 0){
        LOG(false, function + " could not set output plane format");
        return false;
    }else{
        LOG(true, function + " set output plane format success");
    }
    
    m_VideoEncoder->setInsertVuiEnabled(true);
    ret = m_VideoEncoder->setFrameRate(DEFAULT_FPS, 1);
    if (ret < 0){
        LOG(false, function + " Could not set m_VideoEncoderoder framerate");
        return false;
    }else{
        LOG(true, function + " set m_VideoEncoder framerate success");
    }
    ret = m_VideoEncoder->setIFrameInterval(DEFAULT_FPS);
    if (ret < 0){
       LOG(false, " Could not set I-frame interval");
       return false;
    }else{
       LOG(true, function + " set I-frame interval success");
    }
    m_VideoEncoder->setIDRInterval(DEFAULT_FPS);
    m_VideoEncoder->setInsertSpsPpsAtIdrEnabled(true);
 
    ret = m_VideoEncoder->setBitrate(2 * 1024 * 1024);
    if (ret < 0){
        LOG(false, function + " could not set bitrate");
        return false;
    }else{
        LOG(true, function + " set bitrate success");
    }
    if (ENCODER_PIXFMT == V4L2_PIX_FMT_H264)
    {
        ret = m_VideoEncoder->setProfile(V4L2_MPEG_VIDEO_H264_PROFILE_HIGH);
        LOG(true, function + " encode h264");
    }
    else
    {
        ret = m_VideoEncoder->setProfile(V4L2_MPEG_VIDEO_H265_PROFILE_MAIN);
        LOG(true, function + " encode h265");
    }
    if (ret < 0){
        LOG(false, function + " Could not set m_VideoEncoderoder profile");
        return false;
    }else{
        LOG(true, function + " set m_VideoEncoder profile");
    }
    if (ENCODER_PIXFMT == V4L2_PIX_FMT_H264)
    {
        ret = m_VideoEncoder->setLevel(V4L2_MPEG_VIDEO_H264_LEVEL_5_0);
        if (ret < 0){
            LOG(false, function + "Could not set m_VideoEncoderoder level");
            return false;
        }else{
            LOG(true, function + " set m_VideoEncoder level success");
        }
    }

    ret = m_VideoEncoder->setRateControlMode(V4L2_MPEG_VIDEO_BITRATE_MODE_CBR);
    if (ret < 0){
        LOG(false, function + " Could not set rate control mode");
        return false;
    }else{
        LOG(true, function + " set rate control mode success");
    }
    //ret = m_VideoEncoder->setHWPresetType(V4L2_ENC_HW_PRESET_ULTRAFAST);
    //if (ret < 0)
    //    ORIGINATE_ERROR("Could not set m_VideoEncoderoder HW Preset");

    // Query, Export and Map the output plane buffers so that we can read
    // raw data into the buffers
    ret = m_VideoEncoder->output_plane.setupPlane(V4L2_MEMORY_DMABUF, 10, true, false);
    if (ret < 0){
        LOG(false, function + " Could not setup output plane");
        return false;
    }else{
        LOG(true, function + " setup output plane success");
    }
    // Query, Export and Map the output plane buffers so that we can write
    // m_VideoEncoderoded data from the buffers
    ret = m_VideoEncoder->capture_plane.setupPlane(V4L2_MEMORY_MMAP, 10, true, false);
    if (ret < 0){
        LOG(false, function + " Could not setup capture plane");
        return false;
    }else{
        LOG(true, function + " setup capture plane success");
    }
    printf("create video encoder return true\n");
    return true;
}

void ConsumerThread::abort()
{
    m_VideoEncoder->abort();
    m_gotError = true;
}

extern bool push_flag;
char *video_data = (char *)malloc(BUFFLENGTH);

bool ConsumerThread::encoderCapturePlaneDqCallback(struct v4l2_buffer *v4l2_buf,
                                                   NvBuffer * buffer,
                                                   NvBuffer * shared_buffer,
                                                   void *arg)
{
    std::string function = __FUNCTION__;
    ConsumerThread *thiz = (ConsumerThread*)arg;
    if (!v4l2_buf)
    {
        thiz->abort();
        LOG(false, function + " Failed to dequeue buffer from encoder capture plane");
        return false;
    }
#if DEBUG
    thiz->m_outputFile->write((char *) buffer->planes[0].data,
                              buffer->planes[0].bytesused);
#endif
    if(push_flag){
	fps++;
        MediaDataStruct media_data;
        media_data.len = buffer->planes[0].bytesused;
        media_data.index = VIDEOINDEX;
        media_data.buff = (unsigned char *)malloc(buffer->planes[0].bytesused);
        memset(media_data.buff, 0, buffer->planes[0].bytesused);
        memcpy(media_data.buff, buffer->planes[0].data, buffer->planes[0].bytesused);
        gettimeofday(&media_data.tv, NULL);
        pthread_mutex_lock(&video_buf_queue_lock);
        video_buf_queue->push(media_data);
        pthread_mutex_unlock(&video_buf_queue_lock);
    }
    if (thiz->m_VideoEncoder->capture_plane.qBuffer(*v4l2_buf, NULL) < 0)
    {
        thiz->abort();
        LOG(false, function + " Failed to enqueue buffer to encoder capture plane");
        return false;
    }
    // GOT EOS from m_VideoEncoderoder. Stop dqthread.
    if (buffer->planes[0].bytesused == 0)
    {
        CONSUMER_PRINT("Got EOS, exiting...\n");
        return false;
    }
    return true;
}

bool execute(int num){
    std::string function = __FUNCTION__;
  // Create the CameraProvider object and get the core interface.
    UniqueObj<CameraProvider> cameraProvider = UniqueObj<CameraProvider>(CameraProvider::create());
    ICameraProvider *iCameraProvider = interface_cast<ICameraProvider>(cameraProvider);
    if (!iCameraProvider){
        LOG(false, function + " Failed to create CameraProvider");
        return false;
    }else{
        LOG(true, function + " create CameraProvider success");
    }
    // Get the camera devices.
    std::vector<CameraDevice*> cameraDevices;
    iCameraProvider->getCameraDevices(&cameraDevices);
    if (cameraDevices.size() == 0){
        LOG(false, function + " No cameras available");
        return false;
    }else{
        std::string str = "Get ";
        std::stringstream stream;
        int num = cameraDevices.size();
        stream << num;
        str += stream.str();
        if (cameraDevices.size() > 1)
        {
            str += " cameras";
        }
        else
        {
            str += " camera";
        }
        LOG(true, function + " " + str);
    }
    // Create the capture session using the first device and get the core interface.
    UniqueObj<CaptureSession> captureSession(
            iCameraProvider->createCaptureSession(cameraDevices[num]));
    ICaptureSession *iCaptureSession = interface_cast<ICaptureSession>(captureSession);
    if (!iCaptureSession){
        LOG(false, function + " Failed to get ICaptureSession interface");
        return false;
    }else{
        LOG(true, function + " get ICaptureSession interface success");
    }
    // Create the OutputStream.
    PRODUCER_PRINT("Creating output stream\n");
    UniqueObj<OutputStreamSettings> streamSettings(iCaptureSession->createOutputStreamSettings());
    IOutputStreamSettings *iStreamSettings = interface_cast<IOutputStreamSettings>(streamSettings);
    if (!iStreamSettings){
        LOG(false, function + " Failed to get IOutputStreamSettings interface");
    }else{
        LOG(true, function + " get IOutputStreamSettings interface success");
    }
    iStreamSettings->setPixelFormat(PIXEL_FMT_YCbCr_420_888);
    iStreamSettings->setResolution(Argus::Size2D<uint32_t>(DST_WIDTH, DST_HEIGHT));
    UniqueObj<OutputStream> outputStream(iCaptureSession->createOutputStream(streamSettings.get()));

    // Launch the FrameConsumer thread to consume frames from the OutputStream.
    PRODUCER_PRINT("Launching consumer thread\n");
    ConsumerThread frameConsumerThread(outputStream.get());
    PROPAGATE_ERROR(frameConsumerThread.initialize());
    // Wait until the consumer is connected to the stream.
    PROPAGATE_ERROR(frameConsumerThread.waitRunning());

    // Create capture request and enable output stream.
    UniqueObj<Request> request(iCaptureSession->createRequest());
    IRequest *iRequest = interface_cast<IRequest>(request);
    if (!iRequest){
        LOG(false, function + "Failed to create Request");
        return false;
    }else{
        LOG(true, function + " create Request success");
    }
    iRequest->enableOutputStream(outputStream.get());

    ISourceSettings *iSourceSettings = interface_cast<ISourceSettings>(iRequest->getSourceSettings());
    if (!iSourceSettings){
        LOG(false, function + " Failed to get ISourceSettings interface");
        return false;
    }else{
        LOG(true, function + " get ISourceSettings interface");
    }
    iSourceSettings->setFrameDurationRange(Range<uint64_t>(1e9/DEFAULT_FPS));
    // Submit capture requests.
    PRODUCER_PRINT("Starting repeat capture requests.\n");
    if (iCaptureSession->repeat(request.get()) != STATUS_OK){
        LOG(false, function + " Failed to start repeat capture request");
        return false;
    }else{
        LOG(true, function + " start repeat capture request");
    }
    // Wait for CAPTURE_TIME seconds.
    //for (int i = 0; i < CAPTURE_TIME && !frameConsumerThread.isInError(); i++)
    //for (int i = 0; i < 10; i++)
    while(1){    
        sleep(1);
    }
    // Stop the repeating request and wait for idle.
    iCaptureSession->stopRepeat();
    iCaptureSession->waitForIdle();
    // Destroy the output stream to end the consumer thread.
    outputStream.reset();
    // Wait for the consumer thread to complete.
    PROPAGATE_ERROR(frameConsumerThread.shutdown());
    PRODUCER_PRINT("Done -- exiting.\n");
    return true;
}

