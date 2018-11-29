CXXFLAGS := -g -Wall -lm

ifdef C
CXX:=cc
CXXFLAGS += -DCCOMPILER
else
CXX := g++
CXXFLAGS += -std=c++0x
endif

all: cachesim

cachesim: cachesim.o cachesim_driver.o
	$(CXX) -o cachesim cachesim.o cachesim_driver.o

clean:
	rm -f cachesim *.o
