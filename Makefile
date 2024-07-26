# Makefile! Just a launcher for scripts...

export CMAKE_GENERATOR = Ninja
export CMAKE_COLOR_DIAGNOSTICS = ON

all: debug release

clean:
	rm -r build_cmake

clean_cache:
	# Clear CMake caches. May be necessary after upgrading Xcode.
	rm -rf build_cmake/*/CMake*


debug:
	mkdir -p build_cmake/debug/
	cd build_cmake/debug && cmake -DCMAKE_BUILD_TYPE=Debug ../..
	cd build_cmake/debug && cmake --build .

test: debug
	build_cmake/debug/CroutonTests -r consoleplus

release:
	mkdir -p build_cmake/release/
	cd build_cmake/release && cmake -DCMAKE_BUILD_TYPE=MinSizeRel ../..
	cd build_cmake/release && cmake --build . --target LibCrouton --target CroutonXcodeDependencies

xcode_deps: debug release
	mkdir -p build_cmake/debug/xcodedeps
	cp build_cmake/debug/vendor/libuv/libuv*.a build_cmake/debug/xcodedeps/
	cp build_cmake/debug/vendor/mbedtls/library/libmbed*.a build_cmake/debug/xcodedeps/
	mkdir -p build_cmake/release/xcodedeps
	cp build_cmake/release/vendor/libuv/libuv*.a build_cmake/release/xcodedeps/
	cp build_cmake/release/vendor/mbedtls/library/libmbed*.a build_cmake/release/xcodedeps/

blip:
	mkdir -p build_cmake/debug/
	cd build_cmake/debug && cmake -DCMAKE_BUILD_TYPE=Debug -DCROUTON_BUILD_BLIP=1 ../..
	cd build_cmake/debug && cmake --build . --target BLIP --target CroutonTests
	mkdir -p build_cmake/release/
	cd build_cmake/release && cmake -DCMAKE_BUILD_TYPE=MinSizeRel -DCROUTON_BUILD_BLIP=1 ../..
	cd build_cmake/release && cmake --build . --target BLIP

esp:
	cd tests/ESP32 && idf.py build

esptest:
	cd tests/ESP32 && idf.py build flash monitor

lint:
	missing_includes.rb --base include/crouton/util/Base.hh  --ignore cassert  include src
