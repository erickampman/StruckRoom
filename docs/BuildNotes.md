CONFIGURE

cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

BUILD (the .clap lands at build/StruckRoom.clap; no files outside the build tree are touched)

cmake --build build -j

CLEAN 

cmake --build build -j --clean-first


Config Dump build:

    cd ~/dev/dev-clap/StruckRoom
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DSTRUCKROOM_PRESET_DUMP=ON
cmake --build build -j"$(nproc)"
