.PHONY: all clean

all:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build --parallel

clean:
	cmake -E remove_directory build
	cmake -E remove -f ../bin/sdacs_ei_runner
