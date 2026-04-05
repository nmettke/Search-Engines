CXX = g++ --std=c++17
VAL = valgrind --error-exitcode=42

all:  Robots test valgrind

Robots:  RobotsTxt.h Utf8.h pause.cpp Robots.cpp RobotsTxt.cpp Utf8.cpp
	$(CXX) Robots.cpp RobotsTxt.cpp Utf8.cpp pause.cpp -o Robots

valgrind:  Robots SampleRobots.txt Queries.txt
	$(VAL) ./Robots SampleRobots.txt < Queries.txt > /dev/null

test: Robots SampleRobots.txt Queries.txt ExpectedResults.txt
	./Robots SampleRobots.txt < Queries.txt > TestResults.txt
	diff ExpectedResults.txt TestResults.txt

clean:
	rm -f Robots TestResults.txt
