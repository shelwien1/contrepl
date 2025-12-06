CXX = g++
CXXFLAGS = -O2 -Wall
LDFLAGS = -lpcre2-8

all: repl

repl: repl.cpp
	$(CXX) $(CXXFLAGS) -o repl repl.cpp $(LDFLAGS)

clean:
	rm -f repl

.PHONY: all clean
