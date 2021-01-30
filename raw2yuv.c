/*
 * This is intended as a very simple example of using MAML to translate
 * a raw, bayered image to YUV.
 */
#include <sys/types.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>

#include "interface/mmal/mmal.h"
#include "interface/mmal/util/mmal_util.h"
#include "interface/mmal/util/mmal_util_params.h"

#include "interface/vcsm/user-vcsm.h"

#define UNUSED(x) do { x = x; } while (0);

//#define DEBUG_FUNCS

#define IMAGE_WIDTH 1920
#define IMAGE_HEIGHT 1080
#define INPUT_ENCODING MMAL_ENCODING_BAYER_SRGGB12P
#define OUTPUT_ENCODING MMAL_ENCODING_I420
#define ZERO_COPY MMAL_TRUE


#define FAKE_TIMESTAMP 37

static const char *input_file;
static const char *output_file;
static MMAL_COMPONENT_T *isp;
static MMAL_PORT_T *iport, *oport;
static MMAL_POOL_T *ipool, *opool;
static volatile int complete_flag;
static uint64_t start_ts, end_ts;

/* get timestamp in nanoseconds */
static uint64_t
timestamp(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

#if defined(DEBUG_FUNCS)
static char *
format_type_name(MMAL_ES_TYPE_T format_type)
{
    switch (format_type) {
        case MMAL_ES_TYPE_UNKNOWN: return "unknown";
        case MMAL_ES_TYPE_CONTROL: return "control";
        case MMAL_ES_TYPE_AUDIO: return "audio";
        case MMAL_ES_TYPE_VIDEO: return "video";
        case MMAL_ES_TYPE_SUBPICTURE: return "subpicture";
        default: return "undefined";
    }
}

static void
fourcc_to_str(uint32_t fourcc, char *s)
{
    s[0] = fourcc & 0xff;
    s[1] = (fourcc >> 8) & 0xff;
    s[2] = (fourcc >> 16) & 0xff;
    s[3] = (fourcc >> 24) & 0xff;
    s[4] = 0;
}

static void
debug_dump_video(MMAL_VIDEO_FORMAT_T *v) {
    char color_space[5];
    fourcc_to_str(v->color_space, color_space);
    printf("VIDEO FORMAT: w=%d h=%d color_space=%s crop.x=%d, crop.y=%d, crop.w=%d crop.h=%d pixel aspect ratio=%d/%d frame_rate=%d/%d\n", v->width, v->height, color_space, v->crop.x, v->crop.y, v->crop.width, v->crop.height, v->par.num, v->par.den, v->frame_rate.num, v->frame_rate.den);
}

static void
debug_dump_format(MMAL_ES_FORMAT_T *format)
{
    char encoding[5];
    fourcc_to_str(format->encoding, encoding);
    printf("FORMAT: type=%s encoding=%s encoding_variant=%d bitrate=%d flags=%x extradata_size=%d extradata=%p\n",
        format_type_name(format->type), encoding, format->encoding_variant, format->bitrate, format->flags, format->extradata_size, format->extradata);
    switch (format->type) {
        case MMAL_ES_TYPE_VIDEO:
            debug_dump_video(&format->es->video);
            break;
        case MMAL_ES_TYPE_UNKNOWN:
        case MMAL_ES_TYPE_CONTROL:
        case MMAL_ES_TYPE_AUDIO:
        case MMAL_ES_TYPE_SUBPICTURE:
            break;
    }
}

static char *
port_type_name(MMAL_PORT_TYPE_T port_type)
{
    switch (port_type) {
        case MMAL_PORT_TYPE_UNKNOWN: return "unknown";
        case MMAL_PORT_TYPE_CONTROL: return "control";
        case MMAL_PORT_TYPE_INPUT: return "input";
        case MMAL_PORT_TYPE_OUTPUT: return "output";
        case MMAL_PORT_TYPE_CLOCK: return "clock";
        case MMAL_PORT_TYPE_INVALID: return "invalid";
        default: return "undefined";
    }
}

static void
debug_dump_port(MMAL_PORT_T *port)
{
    printf("PORT: name=%s index=%d index_all=%d is_enabled=%d type=%s\n", port->name, port->index, port->index_all, port->is_enabled, port_type_name(port->type));
    printf("     buffer: num_min=%d size_min=%d alignment_min=%d num_recommended=%d size_recommended=%d num=%d size=%d\n",
            port->buffer_num_min, port->buffer_size_min, port->buffer_alignment_min, port->buffer_num_recommended, port->buffer_size_recommended, port->buffer_num, port->buffer_size);
    debug_dump_format(port->format);
}
#endif

static void
o_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
    end_ts = timestamp();
    UNUSED(port);

	printf("OUTPUT: Buffer %p from isp, filled %d, timestamp %llu, flags %04X: duration: %.3fms\n",
        buffer, buffer->length, buffer->pts, buffer->flags, (end_ts - start_ts) / 1000000.0);
    int fd = open(output_file, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
    size_t s = write(fd, buffer->data, buffer->length);
    if (s != buffer->length) {
        printf("error writing output data\n");
        exit(1);
    }
    complete_flag = 1;
}

static void
i_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
    /* do nothing; the input buffer is free. if this was a real processing
     * pipeline we could release the buffer back to the pool */
    UNUSED(port);
    UNUSED(buffer);
}

/* configure the ISP input port */
static void
setup_input_port(void)
{
  	MMAL_STATUS_T status;
    iport = isp->input[0];

    /* Set the format correctly */
    iport->format->encoding = INPUT_ENCODING;
    /* Note: The width at least needs to be a multiple of 32(maybe 16?) - but we know
     * our width/height is OK, so don't mess around here.
     *
     * The cropped width/height can be used for the *actual* size.
     */
    iport->format->es->video.width = IMAGE_WIDTH;
    iport->format->es->video.height = IMAGE_HEIGHT;
    iport->format->es->video.crop.width = IMAGE_WIDTH;
    iport->format->es->video.crop.height = IMAGE_HEIGHT;
    iport->buffer_num = 1;

    status = mmal_port_format_commit(iport);
    if (status != MMAL_SUCCESS) {
        printf("mmal_port_format_commit failed\n");
        exit(1);
    }

    /* allocate a pool with just a single buffer, that's all that
     * is needed for this app */
    mmal_port_parameter_set_boolean(iport, MMAL_PARAMETER_ZERO_COPY, ZERO_COPY);
    ipool = mmal_port_pool_create(iport, 1, iport->buffer_size);
    if (ipool == NULL) {
        printf("mmal_port_pool_create failed\n");
        exit(1);
    }

    status = mmal_port_enable(iport, i_callback);
    if (status != MMAL_SUCCESS) {
        printf("mmal_port_enable(iport, ...) failed\n");
        exit(1);
    }
}

/* configure the ISP output port (or at least the primary output port, there are
 * other ports for preview images and similar things!
 */
static void
setup_output_port(void)
{
  	MMAL_STATUS_T status;
    MMAL_BUFFER_HEADER_T *buf_hdr;

    oport = isp->output[0];

    /* Rather than setting up from scratch copy the input port format
     * across.
     */
    mmal_format_copy(oport->format, iport->format);
    oport->format->encoding = OUTPUT_ENCODING;
    oport->buffer_num = 1;
    status = mmal_port_format_commit(oport);

    if (status != MMAL_SUCCESS) {
        printf("mmal_port_format_commit(oport) failed\n");
        exit(1);
    }

    mmal_port_parameter_set_boolean(oport, MMAL_PARAMETER_ZERO_COPY, ZERO_COPY);
    opool = mmal_port_pool_create(oport, 1, oport->buffer_size);
    if (opool == NULL) {
        printf("mmal_port_pool_create failed\n");
        exit(1);
    }

    status = mmal_port_enable(oport, o_callback);
    if (status != MMAL_SUCCESS) {
        printf("mmal_port_enable(oport) failed\n");
        exit(1);
    }

    buf_hdr = mmal_queue_get(opool->queue);
    if (buf_hdr == NULL) {
        printf("mmal_queue_get(opool->queue) failed\n");
        exit(1);
    }

    /* Send the output buffer to the port.. as this is the output port
     * and the input doesn't have any buffers yet, nothing happens */
    mmal_port_send_buffer(oport, buf_hdr);
}

int
main(int argc, char **argv)
{
  	MMAL_STATUS_T status;
    MMAL_BUFFER_HEADER_T *buf_hdr;

    if (argc != 3) {
        printf("usage: raw2yuv input output\n");
        exit(1);
    }

    input_file = argv[1];
    output_file = argv[2];

    /* Create the ISP MMAL component. We make things simple and store in a global */
    status = mmal_component_create("vc.ril.isp", &isp);
    if (status != MMAL_SUCCESS) {
        printf("Failed to create vc.ril.isp component\n");
        exit(1);
    }

    setup_input_port();
    setup_output_port();

    buf_hdr = mmal_queue_get(ipool->queue);
    int fd = open(input_file, O_RDONLY);
    size_t s = read(fd, buf_hdr->data, buf_hdr->alloc_size);
    if (s != buf_hdr->alloc_size) {
        printf("unable to read all data from file\n");
        exit(1);
    }
    buf_hdr->length = buf_hdr->alloc_size;
    buf_hdr->flags = MMAL_BUFFER_HEADER_FLAG_FRAME_END;
    buf_hdr->pts = FAKE_TIMESTAMP;

    /* push the buffer to the port so that it will be processed */
    start_ts = timestamp();
    mmal_port_send_buffer(iport, buf_hdr);
    while(!complete_flag) {
        usleep(1000);
   }

    return 0;
}