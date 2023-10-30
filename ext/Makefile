all:
	@echo "Pick a target to compile"
	exit 1

dynamorio: dynamorio-configure
	+make -C dynamorio/build

dynamorio-configure: dynamorio-clone
	test -f dynamorio/build/CMakeCache.txt || (cd dynamorio/build && (cmake .. -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) -DBUILD_DOCS=OFF) || git clean -xdf)

dynamorio-clone:
	test -f dynamorio/CMakeLists.txt || git clone https://github.com/DynamoRIO/dynamorio dynamorio
	mkdir -p dynamorio/build

wldbg: wldbg-configure
	+make -C wldbg

wldbg-configure: wldbg-clone
	cd wldbg && ./autogen.sh

wldbg-clone:
	test -f wldbg/configure.ac || git clone https://github.com/mchalupa/wldbg wldbg

