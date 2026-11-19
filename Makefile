# ElectraVote — Makefile
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

SOURCES := $(shell find $(SRCDIR) -path '$(SRCDIR)/legacy_impl' -prune -o -name '*.cpp' -print)
HEADERS := $(shell find $(SRCDIR) -path '$(SRCDIR)/legacy_impl' -prune -o -name '*.h' -print)
OBJECTS := $(SOURCES:.cpp=.o)

.PHONY: all run run-port test test-auth clean data

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

test-auth: all
	$(CXX) $(CXXFLAGS) -Isrc -o /tmp/electravote-auth-service-test tests/auth_service.cpp src/db.cpp src/store.cpp src/crypto.cpp src/services/auth_service.cpp $(LDFLAGS) -Wl,-rpath,/usr/lib/x86_64-linux-gnu $(SODIUM) $(SQLITE)
	@/tmp/electravote-auth-service-test
	@rm -f /tmp/electravote-auth-service-test /tmp/electravote-auth-service-test.db /tmp/electravote-auth-service-test.db-shm /tmp/electravote-auth-service-test.db-wal

test: all test-auth
	@bash tests/smoke.sh

data:
	@mkdir -p $(DATADIR)

clean:
	rm -f $(OBJECTS) $(TARGET)
