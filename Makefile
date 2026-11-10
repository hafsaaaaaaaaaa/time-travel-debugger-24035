CXX = g++
CXXFLAGS = -std=c++17 -Wall -Iinclude -I.
PHASE1 = execute.cpp serialize.cpp src/validate.cpp src/resolve.cpp src/timeline.cpp
HEADERS = $(wildcard include/*.h) $(wildcard *.h)

all: ttdb server load_balancer client

ttdb: main.cpp $(PHASE1) $(HEADERS)
	$(CXX) $(CXXFLAGS) main.cpp $(PHASE1) -o ttdb

server: src/server.cpp $(PHASE1) $(HEADERS)
	$(CXX) $(CXXFLAGS) src/server.cpp $(PHASE1) -o server

load_balancer: src/load_balancer.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) src/load_balancer.cpp -o load_balancer -pthread

client: src/client.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) src/client.cpp -o client

clean:
	rm -f ttdb server load_balancer client resolve.bin session.tdbg upload.bin ttdb_last.tdbg ttdb_last.tdbg.tmp

.PHONY: all clean
