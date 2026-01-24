# Reflex Robotics - Makefile

CC = gcc
CFLAGS = -O3 -Wall -Wextra -pthread
LDFLAGS = -lm -lpthread

SRC_DIR = src
BUILD_DIR = build

TARGETS = control_loop latency_benchmark

.PHONY: all clean run

all: $(BUILD_DIR) $(TARGETS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

control_loop: $(SRC_DIR)/control_loop.c $(SRC_DIR)/reflex.h
	$(CC) $(CFLAGS) -I$(SRC_DIR) $< -o $(BUILD_DIR)/$@ $(LDFLAGS)

latency_benchmark: $(SRC_DIR)/latency_benchmark.c $(SRC_DIR)/reflex.h
	$(CC) $(CFLAGS) -I$(SRC_DIR) $< -o $(BUILD_DIR)/$@ $(LDFLAGS)

run: control_loop
	./$(BUILD_DIR)/control_loop

benchmark: latency_benchmark
	./$(BUILD_DIR)/latency_benchmark

clean:
	rm -rf $(BUILD_DIR) *.csv

# Thor-specific: pin to performance cores
run-thor: control_loop
	taskset -c 0-13 ./$(BUILD_DIR)/control_loop

benchmark-thor: latency_benchmark
	taskset -c 0-13 ./$(BUILD_DIR)/latency_benchmark
