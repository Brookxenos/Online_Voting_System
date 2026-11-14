# OpenVote — Makefile
# Build:   make
# Run:     make run        (port 8080)
# Test:    make test       (integration smoke tests; requires bash + curl)
# Clean:   make clean

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pthread
LDFLAGS  ?= -pthread

# System shared libraries are linked by exact filename because only the
# runtime packages are installed in this environment (no -dev headers).
#   libsodium.so.23  — Argon2id password hashing + CSPRNG + SHA-256
#   libsqlite3.so.0  — transactional SQL storage
SODIUM   := /usr/lib/x86_64-linux-gnu/libsodium.so.23
SQLITE   := /usr/lib/x86_64-linux-gnu/libsqlite3.so.0

SRCDIR   := src
WEBDIR   := web
DATADIR  := data
TARGET   := voting_server

SOURCES := $(wildcard $(SRCDIR)/*.cpp)
HEADERS := $(wildcard $(SRCDIR)/*.h)
OBJECTS := $(SOURCES:.cpp=.o)

.PHONY: all run run-port test clean data

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJECTS) $(LDFLAGS) -Wl,-rpath,/usr/lib/x86_64-linux-gnu $(SODIUM) $(SQLITE)

$(SRCDIR)/%.o: $(SRCDIR)/%.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: all
	@mkdir -p $(DATADIR)
	./$(TARGET) 8080 $(WEBDIR) $(DATADIR)

# Run on a custom port: make run-port PORT=9000
run-port: all
	@mkdir -p $(DATADIR)
	./$(TARGET) $(PORT) $(WEBDIR) $(DATADIR)

test: all
	@bash tests/smoke.sh

data:
	@mkdir -p $(DATADIR)

clean:
	rm -f $(OBJECTS) $(TARGET)
