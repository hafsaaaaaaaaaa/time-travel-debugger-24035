ttdb: src/*.cpp
	g++ -std=c++17 -Wall -g -Iinclude -I. main.cpp execute.cpp serialize.cpp src/*.cpp -o ttdb

clean:
	rm -f ttdb resolve.bin session.tdbg
