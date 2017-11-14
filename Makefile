CXX=clang++ -std=c++17
SRCS=conference-call.cc server.cc client.cc
OBJS=$(subst .cc,.o,$(SRCS))
FLAGS=-pthread

all: cc

cc: $(OBJS)
	$(CXX) $(FLAGS) -o cc $(OBJS)

clean:
	$(RM) $(OBJS)

distclean: clean
	$(RM) cc
