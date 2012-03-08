CXX = g++
CPPFLAGS = -Wall -Wextra -pedantic-errors -O2 
LIBRARIES = -O2

objects = $(addprefix obj/,hittitesiggen_vectors.o)
binary = $(addprefix bin/,nyquist.bin)

project: $(binary)
	echo $(binary)

all: $(objects)

$(binary): $(objects)
	$(CXX) $(LIBRARIES) $(objects) -o $@


obj/%.o: src/%.cpp
	$(CXX) $< -c $(CPPFLAGS) -Iheader -o $@

$(objects): | obj

$(binary): | bin

obj:
	mkdir $@

bin:
	mkdir $@

.PHONY: clean test

clean:
	-rm $(objects) $(binaries)
	-rmdir bin obj

test: $(binary)
	$(binary)
	diff data_output/*

