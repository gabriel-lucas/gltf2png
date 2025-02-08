CXX = clang++-15
CXXFLAGS = -g -O0 -std=gnu++17 -pthread \
           -I/usr/include/filament-1.9 \
           -I/usr/include/filament-1.9/backend \
           -I/usr/include/stb \
           -I/usr/include/utils \
           -I/usr/include/camutils \
           -I/usr/include/math \
           -Wno-deprecated-declarations

LDFLAGS = -L/usr/lib/x86_64-linux-gnu \
          -lfilament \
          -lfilament_backend \
          -lfilament_gltfio_core \
          -lfilament_gltfio \
          -lfilament_utils \
          -lfilament_filabridge \
          -lvulkan \
          -lglfw \
          -lassimp \
          -ldraco \
          -lspirv-cross-c-shared \
          -ldl -lpthread

gltf2png: gltf2png.o
	$(CXX) gltf2png.o $(LDFLAGS) -o $@

gltf2png.o: gltf2png.cpp
	$(CXX) $(CXXFLAGS) -c $<

clean:
	rm -f gltf2png gltf2png.o

.PHONY: clean
