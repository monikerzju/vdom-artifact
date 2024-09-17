all: lib install

lib:
	arm-linux-gnueabihf-gcc -g -fPIC -c libvkeys.c -O3 -Wno-implicit-function-declaration -fno-stack-protector
	arm-linux-gnueabihf-gcc -shared libvkeys.o -o libvkeys.so

install:
	sudo cp libvkeys.so /usr/lib
	sudo cp libvkeys.h /usr/include

clean:
	rm libvkeys.o libvkeys.so
