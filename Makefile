CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Iinclude
SRC = $(wildcard src/*.cpp)
OBJ = $(SRC:src/%.cpp=build/%.o)
TARGET = ttdb

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(OBJ) -o $@

build/%.o: src/%.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf build $(TARGET) resolve.bin session.tdbg

.PHONY: all clean
