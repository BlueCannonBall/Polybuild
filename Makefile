CXX := g++
CXXFLAGS := -Wall -std=c++17 -O3
LDFLAGS := -lpthread
obj/./main.o: ./main.cpp ./toml.hpp
