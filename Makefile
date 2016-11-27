CXX=g++
CXXFLAGS=-g -pedantic -Wall -Wextra -O2 -std=c++11
LDFLAGS=-lsimlib -lm
SOURCES=posta.cpp
EXECUTABLE=posta

all:$(EXECUTABLE)

$(EXECUTABLE): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@ $(LDFLAGS)

clean:
	rm $(EXECUTABLE)