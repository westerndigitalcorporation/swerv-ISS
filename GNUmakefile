OFLAGS := -O3
IFLAGS := -I/usr/local/include -I/home/jrahmeh/local/include
LFLAGS := /usr/local/lib/libboost_program_options.a

# Command to compile .cpp files.
CPPC := $(CXX) -std=gnu++14 $(OFLAGS) $(IFLAGS)

%.o:  %.cpp
	$(CPPC) -Wall -c -o $@ $^

# Command to compile .c files.
%.o:  %.c
	$(CC) -Wall -c -o $@ $^


RISCV := IntRegs.o CsRegs.o instforms.o Memory.o Core.o InstInfo.o

whisper: whisper.o linenoise.o librvcore.a
	$(CPPC) -o $@ $^ $(LFLAGS) /usr/local/lib/libboost_system.a -lpthread

gen16codes: gen16codes.o librvcore.a
	$(CPPC) -o $@ $^ $(LFLAGS)

trace-compare: trace-compare.o
	$(CPPC) -o $@ $^ $(LFLAGS)

adjust-spike-log: adjust-spike-log.o
	$(CPPC) -o $@ $^ $(LFLAGS)

librvcore.a: $(RISCV)
	ar r $@ $^

all: whisper gen16codes trace-compare adjust-spike-log

RELEASE_DIR := /home/jrahmeh/bin

release: all
	cp whisper $(RELEASE_DIR)/visper
	cp whisper gen16codes trace-compare adjust-spike-log $(RELEASE_DIR)

clean:
	$(RM) whisper gen16codes trace-compare $(RISCV) librvcore.a \
	whisper.o linenoise.o \
	gen16codes.o \
	trace-compare.o \
	adjust-spike-log.o

.PHONY: all clean release

