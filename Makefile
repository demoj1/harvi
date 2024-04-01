OUT_NAME=harvi

CXX=clang++
STD=-std=c++2b
WARNINGS=-Wno-unused-command-line-argument -Wall -Wextra -Wpedantic
SANITIZERS=-fsanitize=address -fsanitize=undefined -fno-sanitize-recover=all -fsanitize=float-divide-by-zero -fsanitize=float-cast-overflow -fno-sanitize=null -fno-sanitize=alignment
LIBS=-lm -ldl -lpthread $(shell pkgconf --cflags --libs raylib)
CXXFLAGS=$(STD) $(WARNINGS) -O3 $(LIBS)
CMD=$(CXX) $(CXXFLAGS)

OBJ_PREFIX=objs
OBJECTS=$(OBJ_PREFIX)/main.o

OUT_DIR=./output
OUT_POSTFIX=release
OUT=$(OUT_DIR)/$(OUT_NAME)-$(OUT_POSTFIX)

all: exe

run: exe
	$(OUT)

install: exe
	cp $(OUT) ~/.local/bin/$(OUT_NAME)

debug: CXXFLAGS=$(STD) $(WARNINGS) -g $(SANITIZERS) -DDEBUG $(LIBS)
debug: OUT_POSTFIX=debug
debug: cleanup exe
	rm -rf objs/*
	GDK_SCALE=2 $(OUT) test2.har

cleanup:
	rm -rf ./$(OBJ_PREFIX)/*

exe: $(OBJECTS) Makefile
	$(CMD) $(OBJECTS) -o $(OUT)

objs/%.o: src/%.cpp
	$(CMD) $< -c -o $@
