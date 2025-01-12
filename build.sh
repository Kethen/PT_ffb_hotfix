set -xe
CPPC=i686-w64-mingw32-c++
$CPPC -g -fPIC -c main.cpp -std=c++20 -o main.o -O0
$CPPC -g -fPIC -DREVERSED -c main.cpp -std=c++20 -o main_reversed.o -O0
$CPPC -g -shared -o project_torque_ffb_hotpatch.asi main.o -lntdll -lpsapi -Wl,-Bstatic -lpthread -static-libgcc -static-libstdc++
$CPPC -g -shared -o project_torque_ffb_hotpatch_reversed.asi main_reversed.o -lntdll -lpsapi -Wl,-Bstatic -lpthread -static-libgcc -static-libstdc++
