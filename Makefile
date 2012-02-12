# Settings
SHELL = /bin/sh
CXX = g++
CXXFLAGS =
MAKEDEPEND.cpp = $(CXX) -MM
CPPS = git-encwrapper.cpp
OBJS = ${CPPS:.cpp=.o}
DEPS = ${CPPS:.cpp=.d}
TARGET = git-encwrapper
LIBGIT = ./libgit/libgit.a
LIBS = -liconv $(LIBGIT)


# Build
.PHONY: all
all: $(TARGET)

$(TARGET): $(OBJS) $(LIBGIT)
	$(CXX) $(LDFLAGS) $(OBJS) -o $@ $(LIBS)

$(LIBGIT): $(wildcard ./libgit/*.h) $(wildcard ./libgit/*.c)
	cd ./libgit && $(MAKE)

%.d: %.cpp
	$(MAKEDEPEND.cpp) $< > $@

-include $(DEPS)


# Clean
.PHONY: clean
clean:
	-rm $(OBJS) $(TARGET) $(DEPS)
	cd ./libgit && $(MAKE) clean

# Release
.PHONY: release
release:
	make clean
	make "CFLAGS=-O3 -Wall"
	strip $(TARGET)
