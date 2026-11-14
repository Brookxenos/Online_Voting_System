# Online Voting System — Makefile
# Build:   make
# Run:     make run
# Clean:   make clean

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pthread
LDFLAGS  ?= -pthread

SRCDIR   := src
WEBDIR   := web
DATADIR  := data
TARGET   := voting_server

SOURCES := $(wildcard $(SRCDIR)/*.cpp)
HEADERS := $(wildcard $(SRCDIR)/*.h)
OBJECTS := $(SOURCES:.cpp=.o)

.PHONY: all run clean test data

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJECTS) $(LDFLAGS)

$(SRCDIR)/%.o: $(SRCDIR)/%.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: all
	@mkdir -p $(DATADIR)
	./$(TARGET) 8080 $(WEBDIR) $(DATADIR)

# Run on a custom port: make run PORT=9000
run-port: all
	@mkdir -p $(DATADIR)
	./$(TARGET) $(PORT) $(WEBDIR) $(DATADIR)

data:
	@mkdir -p $(DATADIR)

clean:
	rm -f $(OBJECTS) $(TARGET)
