CXXFLAGS = -std=c++17 -Wall -pedantic -O3
LIBS = -ldl -lssl -lcrypto

bf: main.cpp
	g++ $(CXXFLAGS) $^ $(LIBS) -o $@ 

test: bloomfilter_test.cpp
	g++ $(CXXFLAGS) $^ $(LIBS) -o $@

clean:
	rm -rf bf