CFLAGS := -I/opt/vc/include -W -Wall -Wextra -g  -Werror
LIBS := -L/opt/vc/lib -lrt -lbcm_host -lvcos -lmmal_core -lmmal_util -lmmal_vc_client -lvcsm

PROGS := v4lcap

all: $(PROGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

V4LCAP_OBJS := v4lcap.o
v4lcap: $(V4LCAP_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f *.o
	rm -f $(PROGS)

