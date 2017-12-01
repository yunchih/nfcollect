
SRC = $(wildcard *.c)
OBJECT = $(SRC:.c=.o)
TARGET = nfcollect
CC = gcc
CFLAGS = -g -Wall -Wextra -DDEBUG
LDFLAGS = -lnetfilter_log -lpthread

$(TARGET): $(OBJECT)
	$(CC) -o $(TARGET) $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJECT)
