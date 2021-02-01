CFLAGS := -I/opt/vc/include -W -Wall -Wextra -g  -Werror
LIBS := -L/opt/vc/lib -lrt -lbcm_host -lvcos -lmmal_core -lmmal_util -lmmal_vc_client -lvcsm

PROGS := v4lcap raw2yuv yuv2h264

all: $(PROGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

V4LCAP_OBJS := v4lcap.o
v4lcap: $(V4LCAP_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

RAW2YUV_OBJS := raw2yuv.o
raw2yuv: $(RAW2YUV_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

YUV2H264_OBJS := yuv2h264.o
yuv2h264: $(YUV2H264_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f *.o
	rm -f $(PROGS)

