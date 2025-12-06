CXX = g++
CXXFLAGS = -O2 -Wall
LDFLAGS = -lpcre2-8

all: repl repflags

repl: repl.cpp
	$(CXX) $(CXXFLAGS) -o repl repl.cpp $(LDFLAGS)

repflags: repflags.cpp
	$(CXX) $(CXXFLAGS) -o repflags repflags.cpp $(LDFLAGS)

clean:
	rm -f repl repflags

.PHONY: all clean
