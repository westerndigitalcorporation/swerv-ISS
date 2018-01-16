OFLAGS := -O3
IFLAGS := -I/usr/local/include
LFLAGS := /usr/local/lib/libboost_program_options.a

# Command to compile .cpp files.
CPPC := $(CXX) -std=gnu++14 $(OFLAGS) $(IFLAGS)

%.o:  %.cpp
	$(CPPC) -c -o $@ $^

# Command to compile .c files.
%.o:  %.c
	$(CC) -c -o $@ $^


RISCV := CsRegs.o Inst.o Memory.o Core.o

whisper: whisper.o linenoise.o $(RISCV)
	$(CPPC) -o $@ $^ $(LFLAGS)

gen16codes: gen16codes.o $(RISCV)
	$(CPPC) -o $@ $^ $(LFLAGS)

trace-compare: trace-compare.o
	$(CPPC) -o $@ $^ $(LFLAGS)

adjust-spike-log: adjust-spike-log.o
	$(CPPC) -o $@ $^ $(LFLAGS)

all: whisper gen16codes trace-compare adjust-spike-log

RELEASE_DIR := /home/jrahmeh/bin

release: all
	cp whisper $(RELEASE_DIR)/visper
	cp whisper gen16codes trace-compare adjust-spike-log $(RELEASE_DIR)

clean:
	$(RM) sim gen16codes trace-compare $(RISCV) sim.o gen16codes.o \
	trace-compare.o

.PHONY: all clean release

