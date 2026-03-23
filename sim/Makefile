CC      = gcc
CFLAGS  = -O3 -march=native -mavx2 -mpopcnt -std=c11 -Wall -Wextra
LDFLAGS = -lm
TARGET  = test14c

$(TARGET): test14c.c
	$(CC) $(CFLAGS) -o $(TARGET) test14c.c $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: run clean
