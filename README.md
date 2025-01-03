Dog was written by Folkert van Heusden.
It is based on the chess program Micah with different evaluation and different threading (lazy smp).
Licensed under the MIT license.


Clone the git repo with the "--recursive" flag!


To build the program and upload it to a wemos32 mini:

	cd app
	pio run -t upload
	pio run -t uploadfs

The ESP32 version can be used with xboard as well.
Adapt app/wrapper.sh to let it use the correct port in case the ESP32 is not connected to /dev/ttyUSB0.
Then run: `xboard -fUCI -fcp app/wrapper.sh` (this requires 'socat' to be installed).

The program also has a integrated text-interface. For that just run it and enter "tui".

To build it for Linux (requires at least gcc/g++ 14 or clang/clang++ 14):

	cd app/src/linux-windows
	mkdir build
	cd build
	cmake ..
	make

To build it for windows (using mingw-w64):

	cd app/src/linux-windows
	mkdir buildwindows
	cd buildwindows
	cmake -DCMAKE_TOOLCHAIN_FILE=../mingw64.cmake ..
	make

The Linux/windows versions contain a Dog in ansi-art visible when you run it with '-h'.

On windows this only works if you enable ansi-code processing in the registry (and restart cmd.exe):

    reg add HKEY_CURRENT_USER\Console /v VirtualTerminalLevel /t REG_DWORD /d 0x00000001 /f


The device can have (optional) LEDs connected:
* a green led on pin 27 - blinks while thinking
* a blue led on pin 25  - blinks while pondering
* a red led on pin 22   - blinks in an error situation

You can also can connect a TTL to UART converter to pin 16 (RX) and pin 17 (TX).

* internal led is blinking during startup
