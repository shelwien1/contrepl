CXX = g++
CXXFLAGS = -O2 -Wall
LDFLAGS = -lpcre2-8

# Platform-specific settings for DLL loading
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
  REPL2_LDFLAGS = $(LDFLAGS) -ldl
  DLL_FLAGS = -shared -fPIC
endif
ifeq ($(UNAME_S),Darwin)
  REPL2_LDFLAGS = $(LDFLAGS)
  DLL_FLAGS = -shared -fPIC
endif
ifdef WINDIR
  REPL2_LDFLAGS = $(LDFLAGS)
  DLL_FLAGS = -shared
endif

all: repl2 repl2chk default.dll

repl2: repl2.cpp
	$(CXX) $(CXXFLAGS) -o repl2 repl2.cpp $(REPL2_LDFLAGS)

repl2chk: repl2chk.cpp
	$(CXX) $(CXXFLAGS) -o repl2chk repl2chk.cpp $(LDFLAGS)

default.dll: default_dll.cpp
	$(CXX) $(CXXFLAGS) $(DLL_FLAGS) -o default.dll default_dll.cpp

clean:
	rm -f repl2 repl2chk default.dll

.PHONY: all clean
