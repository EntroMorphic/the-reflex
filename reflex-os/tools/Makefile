# Makefile for reflex-os Linux tools
#
# These tools run on Linux hosts (Pi 4, Thor, etc.) not ESP32

CC = gcc
CFLAGS = -O2 -Wall -Wextra -I../include
LDFLAGS =

TOOLS = obsbot_test

.PHONY: all clean

all: $(TOOLS)

obsbot_test: obsbot_test.c ../include/reflex_obsbot.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TOOLS)

# Quick test
test: obsbot_test
	./obsbot_test --scan
