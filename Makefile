FILAMENT_LIBS = \
    -lfilabridge -lfilaflat -lbackend \
    -lbluegl -lbluevk -lfilament \
    -lgltfio_core -lgltfio -luberzlib -lktxreader \
    -lshaders -lgeometry -lutils -limage -luberarchive \
    -lfilameshio -lmeshoptimizer -ldracodec \
    -lzstd -lcivetweb -lvkshaders -lsmol-v \
    -libl -lmatdbg -lm -lc++ -lc++abi -lstb

CC = clang++-19

CXXFLAGS = \
	   -Iinclude/ \
	    -I/usr/include/stb/ \
	   -std=c++17 \
	   -stdlib=libc++ -c -fno-builtin

LDFLAGS = -Llib/x86_64/ -ldl

gltf2png: gltf2png.o
	$(CC) gltf2png.o -Wl,--start-group $(FILAMENT_LIBS) -Wl,--end-group $(LDFLAGS) -lpthread -o gltf2png

gltf2png.o: gltf2png.cpp
	$(CC) $(CXXFLAGS) gltf2png.cpp

clean:
	rm -f gltf2png gltf2png.o

.PHONY: clean