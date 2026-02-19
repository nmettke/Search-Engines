CXX := g++
OPENSSL_PREFIX := /opt/homebrew/opt/openssl@3

CXXFLAGS := -std=c++17 -Wall -Wextra -g -O2 \
            -I../parser \
            -I$(OPENSSL_PREFIX)/include

LDFLAGS := -L$(OPENSSL_PREFIX)/lib
LIBS := -lssl -lcrypto

TARGET := basic

SRCS := basic.cpp \
        ../parser/HtmlParser.cpp \
        ../utils/SSL/LinuxSSL_Crawler.cpp \
        ../parser/HtmlTags.cpp

OBJS := $(SRCS:.cpp=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
