all: lib install

lib:
	gcc -g -fPIC -c libvkeys.c -O3 -Wno-implicit-function-declaration -fno-stack-protector
	gcc -shared libvkeys.o -o libvkeys.so
	gcc -o libvkeyss.o -fPIC -c libvkeys.c -DVKEY_SAFE -O3 -Wno-implicit-function-declaration -fno-stack-protector
	gcc -shared libvkeyss.o -o libvkeyss.so

template:
	gcc -o sbtemp.o -fPIC -c sbtemp.c -DVKEY_SAFE -O3 -Wno-implicit-function-declaration -fno-stack-protector
	gcc -shared sbtemp.o -o sbtemp.so
	objdump -d sbtemp.o

install:
	sudo cp libvkeys.so /usr/lib
	sudo cp libvkeyss.so /usr/lib
	sudo cp libvkeys.h /usr/include

clean:
	rm libvkeys.o libvkeys.so libvkeyss.o libvkeyss.so
