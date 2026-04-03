CXX := g++
OPENSSL_PREFIX := /opt/homebrew/opt/openssl@3

CXXFLAGS := -std=c++17 -Wall -Wextra -g -O2 \
            -I../parser \
            -I$(OPENSSL_PREFIX)/include

LDFLAGS := -L$(OPENSSL_PREFIX)/lib
LIBS := -lssl -lcrypto

TARGET := basic

SRCS := basic.cpp \
        url_dedup.cpp \
        ../utils/SSL/LinuxSSL_Crawler.cpp \
        ../parser/HtmlTags.cpp \
        ../utils/Utf8.cpp \
        ../parser/HtmlParser.cpp

OBJS := $(SRCS:.cpp=.o)

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

test:
	@echo "Use the top-level CMake/CTest test targets for url_dedup tests."

clean:
	rm -f $(OBJS) $(TARGET)
