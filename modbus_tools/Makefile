CC = gcc
CFLAGS = -Wall -g
LDFLAGS = -lmodbus

%.o:	%.c
	$(CC) $(CFLAGS) -c -I. -I/usr/local/include/modbus $< -o $@

.PHONY: all
all: clean mbg mflasher addon

.PHONY: clean
clean:
	rm -f *.o

mbg: bg.o ../../transport/modbus/modbus_proxy.o libmodbus_backend.o
	$(CC) -o modbus_bg $^ $(LDFLAGS)


mflasher: flash.o ../../transport/modbus/modbus_proxy.o libmodbus_backend.o
	$(CC) -o modbus_flash $^ $(LDFLAGS)

addon: addon.o
	$(CC) -o modbus_addon $^ $(LDFLAGS)
