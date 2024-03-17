all: peer tracker
peer: peer.cpp
	g++ -std=c++17 -g -Wall -o peer peer.cpp -pthread
tracker: tracker.cpp
	g++ -std=c++17 -g -Wall -o tracker tracker.cpp -pthread
clean:
	$(RM) peer
	$(RM) tracker
