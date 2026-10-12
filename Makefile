ttdb: main.cpp src/*.cpp
	g++ -std=c++17 -Iinclude main.cpp src/*.cpp -o ttdb
clean:
	rm -f ttdb resolve.bin session.tdbg
