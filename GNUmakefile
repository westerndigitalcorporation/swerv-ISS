RELEASE_DIR := /home/joseph.rahmeh/bin

# We use the ELFIO library and boost 1.67.
ELFIO_INC := /home/joseph.rahmeh/local/include
BOOST_INC := /wdc/apps/utilities/boost-1.67/include

# These boost libraries are compiled with g++ -std=gnu++14
BOOST_LIB_DIR := /wdc/apps/utilities/boost-1.67/lib

OFLAGS := -O3

IFLAGS := -I$(BOOST_INC) -I $(ELFIO_INC)

BOOST_OPTS := $(BOOST_LIB_DIR)/libboost_program_options.a
BOOST_SYS := $(BOOST_LIB_DIR)/libboost_system.a

# Command to compile .cpp files.
CPPC := $(CXX) -std=c++17 $(OFLAGS) $(IFLAGS)

# Make a .o from a .cpp
%.o:  %.cpp
	$(CPPC) -pedantic -Wall -c -o $@ $<

# Make .o from a .c
%.o:  %.c
	$(CC) -Wall -c -o $@ $<

whisper: whisper.o linenoise.o librvcore.a
	$(CPPC) -o $@ $^ $(BOOST_OPTS) $(BOOST_SYS) -lpthread

RISCV := IntRegs.o CsRegs.o instforms.o Memory.o Core.o InstInfo.o Triggers.o

librvcore.a: $(RISCV)
	ar r $@ $^

release: whisper
	cp $^ $(RELEASE_DIR)

clean:
	$(RM) whisper $(RISCV) librvcore.a whisper.o linenoise.o

extraclean: clean
	$(RM) *.d

help:
	@echo possible targets: release clean extraclean

.PHONY: clean release help

# Rule for generating dependency files
%.d: %.cpp
	@set -e; rm -f $@; \
	 $(CPPC) -M $< > $@.$$$$; \
	 sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	 rm -f $@.$$$$

%.d: %.c
	@set -e; rm -f $@; \
	 $(CC) -M $< > $@.$$$$; \
	 sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	 rm -f $@.$$$$

CPP_SOURCES := $(RISCV:.o=.cpp) whisper.cpp
C_SOURCES := linenoise.c

include $(CPP_SOURCES:.cpp=.d) $(C_SOURCES:.c=.d)
