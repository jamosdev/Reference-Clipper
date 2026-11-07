.PHONY: build run

build:
	cmake --preset dev
	cmake --build --preset dev

run: build
	ASAN_OPTIONS=detect_leaks=0 ./build/reference-clipper
