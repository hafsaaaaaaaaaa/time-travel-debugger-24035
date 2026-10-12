ttdb: src/*.cpp
	g++ -std=c++17 -Iinclude src/*.cpp -o ttdb

clean:
	rm -f ttdb resolve.bin session.tdbg
