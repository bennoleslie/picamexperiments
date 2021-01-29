#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <linux/videodev2.h>

/* MIPI CSI camera is always on /dev/video0 */
#define VIDEO_DEVICE "/dev/video0"
/* The camera we are testing has a fixed image size and depth */
#define IMAGE_WIDTH 1920
#define IMAGE_HEIGHT 1080
#define PIX_FMT V4L2_PIX_FMT_SRGGB12P
/* Size of the images in bytes (12 bits per pixel */
#define IMAGE_SIZE (IMAGE_WIDTH * IMAGE_HEIGHT) + (IMAGE_WIDTH * IMAGE_HEIGHT) / 2
/* Number of buffers allocated into the kernel */
#define BUFFER_COUNT 5
/* Number of frames to capture in the application */
#define CAPTURE_COUNT 300

/* Gain; ideally we have zero gain to avoid noise */
#define GAIN 0
/* Exposure time; this is maximum, which is what we need to capture enough light */
#define EXPOSURE 1121
/* We can have the sensor itself flip horizontal or vertical */
#define HFLIP 0
#define VFLIP 0

/* Defines to change what 'work' is done for each frame captured */
//#define WRITE_TO_FILE
//#define WRITE_TO_NULL
#define WORK

/* file descriptor for the camera */
static int cam_fd = -1;
/* pointers to the mmap buffer of each input buffer */
static uint8_t* video_buffer_ptr[BUFFER_COUNT];
/* most recent sequence number - used to track dropped packets */
static uint32_t last_seq = -1;

/* open the camera - updates the global cam_fd */
static void
cam_open(void)
{
    cam_fd = open(VIDEO_DEVICE, O_RDWR);
    if (cam_fd < 0) {
        printf("Error opening %s (%s)\n", VIDEO_DEVICE, strerror(errno));
        exit(0);
    }
}

/* set the gain/exposure and flip parameters. these are basically
 * the only controls on the actual sensor itself */
static void
sensor_set_parameters(void)
{
	int r;
    struct v4l2_control ctl;

    /* Set the gain */
    ctl.id = V4L2_CID_GAIN;
    ctl.value = GAIN;
    r = ioctl(cam_fd, VIDIOC_S_CTRL, &ctl);
    if (r < 0) {
        printf("error setting gain %s\n", strerror(errno));
        exit(0);
    }

    /* Set the exposure */
    ctl.id = V4L2_CID_EXPOSURE;
    ctl.value = EXPOSURE;
    r = ioctl(cam_fd, VIDIOC_S_CTRL, &ctl);
    if (r < 0) {
        printf("error setting gain %s\n", strerror(errno));
        exit(0);
    }

    /* hflip */
    ctl.id = V4L2_CID_HFLIP;
    ctl.value = HFLIP;
    r = ioctl(cam_fd, VIDIOC_S_CTRL, &ctl);
    if (r < 0) {
        printf("error setting gain %s\n", strerror(errno));
        exit(0);
    }

    /* vlip */
    ctl.id = V4L2_CID_VFLIP;
    ctl.value = VFLIP;
    r = ioctl(cam_fd, VIDIOC_S_CTRL, &ctl);
    if (r < 0) {
        printf("error setting gain %s\n", strerror(errno));
        exit(0);
    }
}

/* setup the format we want to capture */
static void
setup_format(void)
{
    int r;
    struct v4l2_format format;
    memset(&format, 0, sizeof format);
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	format.fmt.pix.pixelformat = PIX_FMT;
    format.fmt.pix.width = IMAGE_WIDTH;
    format.fmt.pix.height = IMAGE_HEIGHT;
    r = ioctl(cam_fd, VIDIOC_TRY_FMT, &format);
    if (r != 0) {
        printf("ioctl(VIDIOC_TRY_FMT) failed %d(%s)", errno, strerror(errno));
        exit(1);
    }

    /* Set the capture parameters */
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    r = ioctl(cam_fd, VIDIOC_S_FMT, &format);
    if (r != 0) {
        printf("ioctl(VIDIOC_S_FMT) failed %d(%s)", errno, strerror(errno));
        exit(1);
    }

}

/* allocate buffers */
static void
setup_buffers(void)
{
    int r;
    struct v4l2_requestbuffers req;
    struct v4l2_buffer buffer;

    /* Request buffers */
    req.count = BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    r = ioctl(cam_fd, VIDIOC_REQBUFS, &req);
    if (r != 0) {
        printf("ioctl(VIDIOC_REQBUFS) failed %d(%s)", errno, strerror(errno));
        exit(1);
    }

    if (req.count < BUFFER_COUNT)
    {
        printf("request buffer failed");
        exit(1);
    }

    memset(&buffer, 0, sizeof(buffer));
    buffer.type = req.type;
    buffer.memory = V4L2_MEMORY_MMAP;
    for (unsigned i = 0; i < req.count; i++)
    {
        /* retrieve the buffer */
        buffer.index = i;
        r = ioctl(cam_fd, VIDIOC_QUERYBUF, &buffer);
        if (r != 0) {
            printf("ioctl(VIDIOC_QUERYBUF) failed %d(%s)", errno, strerror(errno));
            exit(1);
        }

        /* map the buffer into our address space so we can access the data */
        video_buffer_ptr[i] = mmap(NULL, buffer.length, PROT_READ|PROT_WRITE, MAP_SHARED, cam_fd, buffer.m.offset);
        if (video_buffer_ptr[i] == MAP_FAILED) {
            printf("mmap() failed %d(%s)", errno, strerror(errno));
            exit(1);
        }

        /* enqueue the buffer so that when we start it will be used */
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;
        r = ioctl(cam_fd, VIDIOC_QBUF, &buffer);
        if (r != 0) {
            printf("ioctl(VIDIOC_QBUF) failed %d(%s)", errno, strerror(errno));
            exit(1);
        }
    }
}

/* turn on the stream; will start received buffers */
static void
start(void)
{
    int buffer_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int r = ioctl(cam_fd, VIDIOC_STREAMON, &buffer_type);
    if (r != 0)
    {
        printf("ioctl(VIDIOC_STREAMON) failed %d(%s)", errno, strerror(errno));
        exit(1);
    }
}

/* get a buffer from the driver -- this will block waiting for buffer to be available */
static int
get_buffer(void)
{
    int r;
    struct v4l2_buffer buffer;
    uint32_t seq, dropped;

    memset(&buffer, 0, sizeof buffer);
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = BUFFER_COUNT;

    r = ioctl(cam_fd, VIDIOC_DQBUF, &buffer);
    if (r != 0) {
        printf("ioctl(VIDIOC_DQBUF) failed %d(%s)", errno, strerror(errno));
        exit(1);
    }

    if (buffer.index >= BUFFER_COUNT) {
        printf("invalid buffer index: %d", buffer.index);
        exit(1);
    }

    seq = buffer.sequence;
    dropped = seq - last_seq - 1;
    if (dropped > 0) {
        printf("Dropped frames: %d\n", dropped);
    }
    last_seq = seq;

    return buffer.index;
}

/* release the buffer back to the driver by enqueuing it */
static void
release_buffer(int idx) {
    int r;
    struct v4l2_buffer buffer;

    memset(&buffer, 0, sizeof buffer);
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = idx;

    r = ioctl(cam_fd, VIDIOC_QBUF, &buffer);
    if (r != 0)
    {
        printf("ioctl(VIDIOC_QBUF) failed %d(%s)", errno, strerror(errno));
        exit(1);
    }
}

#if defined(WORK)
static uint64_t
work(uint8_t *buf) {
    uint64_t s;
    for (unsigned i = 0; i < IMAGE_SIZE; i+=8) {
        s += buf[i];
    }
    return s;
}
#endif

int
main(void)
{
    cam_open();
    sensor_set_parameters();
    setup_format();
    setup_buffers();
    start();

    for (unsigned i = 0; i < CAPTURE_COUNT; i++) {
#if defined(WRITE_TO_FILE)
        int fd;
        char filename[32];
#endif
        int buf_idx = get_buffer();
#if defined(WORK)
        work(video_buffer_ptr[buf_idx]);
#endif
#if defined(WRITE_TO_FILE)
#if defined(WRITE_TO_NULL)
        sprintf(filename, "/dev/null");
#else
        sprintf(filename, "./%05d.raw", i);
#endif
        fd = open(filename, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
        if (fd < 0) {
           printf("open() failed: %d(%s)", errno, strerror(errno));
           exit(1);
        }

        write(fd, video_buffer_ptr[buf_idx], IMAGE_SIZE);
        close(fd);
#endif
        release_buffer(buf_idx);
    }

    return 0;
}
