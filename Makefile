OBJS=raspi2png.o dma.o mailbox.o pcm.o pwm.o rpihw.o ws2811.o
BIN=raspi2png

CFLAGS+=-Wall -g -O3
LDFLAGS+=-L/opt/vc/lib/ -lbcm_host -lm

INCLUDES+=-I/opt/vc/include/ -I/opt/vc/include/interface/vcos/pthreads -I/opt/vc/include/interface/vmcs_host/linux

all: $(BIN)

install: $(BIN)
	install -d -m 755 $(DESTDIR)/usr/bin/
	install -m 755 $(BIN) $(DESTDIR)/usr/bin/raspi2png

%.o: %.c
	@rm -f $@
	$(CC) $(CFLAGS) $(INCLUDES) -g -c $< -o $@ -Wno-deprecated-declarations

$(BIN): $(OBJS)
	$(CC) -o $@ -Wl,--whole-archive $(OBJS) $(LDFLAGS) -Wl,--no-whole-archive -rdynamic

clean:
	@rm -f $(OBJS)
	@rm -f $(BIN)
