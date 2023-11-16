.PHONY: test clean

COMMON_DIR:=../common

CFLAGS=-I $(COMMON_DIR) -g -Wall
LDFLAGS=-lpthread
OBJS=csapp.o

all: proxy

proxy: proxy.o csapp.o cache.o
	$(CC) $(CFLAGS) proxy.o csapp.o cache.o -o proxy $(LDFLAGS)

$(OBJS): %.o : $(COMMON_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *~ *.o proxy core *.tar *.zip *.gzip *.bzip *.gz

