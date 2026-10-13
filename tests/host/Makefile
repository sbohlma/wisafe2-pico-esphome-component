COMPONENT := ../../components/wisafe2
CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined
SOURCES := main.cpp test_framing.cpp test_messages.cpp test_radio.cpp test_host_identity.cpp test_scheduler.cpp \
	test_registry.cpp test_discovery.cpp test_node_view.cpp \
	$(COMPONENT)/framing.cpp $(COMPONENT)/messages.cpp $(COMPONENT)/catalog.cpp $(COMPONENT)/scheduler.cpp \
	$(COMPONENT)/frame_analyzer.cpp $(COMPONENT)/duplicate_filter.cpp $(COMPONENT)/host_identity.cpp \
	$(COMPONENT)/registry.cpp $(COMPONENT)/discovery.cpp $(COMPONENT)/node_view.cpp

.PHONY: test clean

test: host_tests
	./host_tests

host_tests: $(SOURCES) $(wildcard *.h) $(wildcard $(COMPONENT)/*.h)
	$(CXX) $(CXXFLAGS) -I$(COMPONENT) -o $@ $(SOURCES)

clean:
	rm -f host_tests
