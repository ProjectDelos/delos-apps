CFLAGS=-O3 -g -Wall -Wextra -std=c++11 -Wno-sign-compare 
CC=g++

DELOS_LIB=$(DELOS_RUST_LOC)/examples/c_linking/target/release $(DELOS_RUST_LOC)/src/*
DELOS_INC=$(DELOS_RUST_LOC)/examples/c_linking
LIB_DIRS=./libs/lib/ $(DELOS_LIB)
LIBPATH=$(foreach d, $(LIB_DIRS), -L$d)
LIBS=-pthread -lc_link -ldl -lrt
INC_DIRS=include $(DELOS_APPS_LOC)/common $(DELOS_RUST_LOC)/examples/c_linking
INCLUDE=$(foreach d, $(INC_DIRS), -I$d)
SRC=src
SOURCES:=$(wildcard $(SRC)/*.cc $(SRC)/*.c)
OBJECTS=$(patsubst $(SRC)/%.cc,build/%.o,$(SOURCES))

DEPSDIR:=.deps
DEPCFLAGS=-MD -MF $(DEPSDIR)/$*.d -MP

all:build/c_proxy

-include $(wildcard $(DEPSDIR)/*.d)

build/c_proxy:$(OBJECTS)
	@$(CC) $(CFLAGS) $(INCLUDE) $^ -o $@ $(LIBPATH) $(LIBS)

build/%.o: src/%.cc $(DEPSDIR)/stamp Makefile
	@mkdir -p build
	@echo + cc $<
	@$(CXX) $(CFLAGS) $(DEPCFLAGS) $(INCLUDE) -c -o $@ $<

$(DEPSDIR)/stamp:
	@mkdir -p $(DEPSDIR)
	@touch $@

.PHONY: clean

clean:
	rm -rf build $(DEPSDIR) 
