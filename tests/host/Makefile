COMPONENT := ../../components/wisafe2
CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined
SOURCES := main.cpp test_framing.cpp test_messages.cpp test_scheduler.cpp \
	$(COMPONENT)/framing.cpp $(COMPONENT)/messages.cpp $(COMPONENT)/scheduler.cpp $(COMPONENT)/frame_analyzer.cpp

.PHONY: test clean

test: host_tests
	./host_tests

host_tests: $(SOURCES) $(wildcard *.h) $(wildcard $(COMPONENT)/*.h)
	$(CXX) $(CXXFLAGS) -I$(COMPONENT) -o $@ $(SOURCES)

clean:
	rm -f host_tests
