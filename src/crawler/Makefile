CXX := g++
OPENSSL_PREFIX := /opt/homebrew/opt/openssl@3

CXXFLAGS := -std=c++17 -Wall -Wextra -g -O2 \
            -I../parser \
            -I$(OPENSSL_PREFIX)/include

LDFLAGS := -L$(OPENSSL_PREFIX)/lib
LIBS := -lssl -lcrypto

TARGET := basic
TEST_TARGET := url_dedup_test

SRCS := basic.cpp \
        url_dedup.cpp \
        ../parser/HtmlParser.cpp \
        ../utils/SSL/LinuxSSL_Crawler.cpp \
        ../parser/HtmlTags.cpp

OBJS := $(SRCS:.cpp=.o)
TEST_SRCS := url_dedup_test.cpp url_dedup.cpp
TEST_OBJS := $(TEST_SRCS:.cpp=.o)

.PHONY: all clean test

all: $(TARGET) $(TEST_TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TEST_TARGET): $(TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -f $(OBJS) $(TEST_OBJS) $(TARGET) $(TEST_TARGET)
