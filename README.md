# Introduction

Whisper is a RISCV instruction set simulator (ISS) developed for the
verification of the Swerv micro-controller. It allows the user to run
RISCV code without RISCV hardware. It has an interactive mode where
the user can single step the target RISCV code and inspect/modify the
RISCV registers or the simulated system memory. It can also run in
lock step with a Verilog simulator serving as a "golden model" against
which an implementation is checked after each instruction of a test
program.

# Requirements

To use Whisper, you would need to download its source code, compile
it, prepare some target test program, compile the test program to
RISCV binary code and then run the RISCV binary within the Whisper
simulator. In particular you would need:

1. A Linux machine to host the RISCV tool-chain and Whisper.

2. The RISCV tool-chain which contains a cross-compiler to compile
   C/C++ code to RISCV binary. The tool chain can be downloaded from
   [github.](https://github.com/riscv/riscv-gnu-toolchain)

3. The Whisper source code which can be downloaded from 
   [github.](https://github.com/westerndigitalcorporation/swerv-ISS)

4. The g++ compiler version 7.2 or higher to compiler Whisper. The g++
   compiler can be installed from a Linux distribution. Alternatively,
   the source code can be downloaded from
   [gnu.org/software/gcc.](https://www.gnu.org/software/gcc)

5. The boost library version 1.67 of higher compiled with c++-14 or
   c++-17. Boost source can be downloaded from
   [boost.org.](https://www.boost.org)


# Compiling Whisper

In the whisper directory, do the following:

1. Set the BOOST_ROOT environment variable to the path to your Boost
   library installation. For example:
    
    export BOOST_ROOT=/wdc/apps/utilities/boost-1.67

2. Run the make program: make.


# Preparing Target Programs

Standalone C/assembly programs not requiring operating system support (such programs
cannot do any I/O) should be compiled as follows:

    $ riscv32-unknown-elf-gcc -mabi=ilp32 -march=rv32imc -static -O3 -nostdlib -o test1 test1.c


The key switch in the above compilation command is "-nostdlib" which
prevents the compiler from linking-in the standard C library.

Note that without the standard C library, there is no "_start"
symbol. The linker will complain that the start symbol is missing and
will use another symbol as the default start address of the
program. The user can always override that start address (program
counter at the beginning of the simulation) by using the --startpc
command line option.

Also note that without an operating system, the simulator does not
know when the program finishes. It will execute instructions
indefinitely. Consider the following test program:

    int
    main(int argc, char* argv[])
    {
      int x = 1;
      int y = 2;
      int z = x + y;
      return z;
    }


The simulator will start execution at the ELF file entry point
(address corresponding to main) and will return to address 0 (initial
value of return address register) when the instruction corresponding
to "return z" is executed. This will most likely cause an illegal
instruction exception and given that no trap handlers are loaded into
the memory, it will cause an infinite loop of illegal traps. To avoid
this, simple stand-alone no-operating-system programs should define a
global 32-bit integer named "tohost" and should write to that location
at the end of the program. This signals the simulator to terminate the
program.

Here's a modified version of the above program that stop once main is done:

    #include <stdint.h>

    volatile uint32_t tohost = 0;
    
    int
    main(int argc, char* argv[])
    {
      int x = 1;
      int y = 2;
      int z = x + y;
      return z;
    }
    
    void _start()
    {
      main(0, 0);
      tohost = 1;
    }
    
And here's how to compile and run the above program

    $ riscv32-unknown-elf-gcc -mabi=ilp32 -march=rv32imc -nostdlib -g -o test2 test2.c
    $ whisper test2

If no global variable named "tohost" is written by the program, the
simulator will stop on its own if a sequence of 64 consecutive illegal
instructions is encountered.

For programs requiring minimal operating system support (e.g. brk,
open, read and write) the user can compile with the newlib C library
and use the simulator with the "--newlib" option.

Here's a sample program:

    #include <stdio.h>

    int
    main(int argc, char* argv[])
    {
       printf("hello world\n");
       return 0;
    }
   
And here's how to compile and run it (assuming riscv32-unknown-elf-gcc
was compiled with newlib):

    $ riscv32-unknown-elf-gcc -mabi=ilp32 -march=rv32imc -static -O3 -o test3 test3.c
    $ whisper --newlib test3

Note that in this case the simulator will intercept the exit system
call invoked by the C library code and terminate the program
accordingly. There is no need for the "tohost" mechanism.

# Running Whisper

Running whisper with -h or --help will print a brief description of all the
command line options. To run a RISCV program, prog, in whisper, one would
issue the Linux command:

    whisper prog
   
which will run the program until it writes to the "tohost" location.

A program compiled with the newlib C library need not have a "tohost"
location. Such a program will run until it calls exit. Such a program
would be run as follows:

    whisper --newlib prog


## Command Line Options

The following is a brief description of the command line options:

    --help      
       Produce help message.

    --log
       Enable tracing to standard output of executed instructions.

    --xlen len
       Specify register width (32 or 64), defaults to 32.

    --isa string
       Select the RISCV options to enable. The currently supported options are
       a (atomic), c (compressed instructions), d (double precision fp), 
       f (single precision fp), i (base integer), m (multiply divide),
       s (supervisor mode), and u (user mode). By default, only i, m and c
       are enabled. Note that option i cannot be turned off. Example: --isa imcf

    --target program
       Specify target program (ELF file) to load into simulated memory. In newlib
       emulations mode, program options may follow program name.

    --hex file
       Hexadecimal file to load into simulator memory.

    --logfile file
       Enable tracing to given file of executed instructions.

    --consoleoutfile file
       Redirect console output to given file.

    --commandlog file
       Enable logging of interactive/socket commands to the given file.

    --startpc address
       Set program entry point to the given address (in hex notation with a 0x prefix).
       If not specified, use the ELF file entry point.

    --endpc address
       Set stop program counter to the given address (in hex notation with a 0x 
       prefix). Simulator will stop once instruction at the stop program counter
       is executed. If not specified, use the ELF file _finish symbol.

    --tohost address
       Memory address to which a write stops the simulator (in hex with 0x prefix).

    --consoleio address
       Memory address corresponding to console io (in hex with 0x prefix).
       Reading/writing a byte (using lb/sb instruction) from given address
       reads/writes a byte from the console.

    --maxinst limit
       Limit executed instruction count to given number.

    --interactive
       After loading any target file into memory, the simulator enters interactive
       mode.

    --triggers
       Enable debug triggers (triggers are automatically enabled in interactive and
       server modes).

    --counters
       Enable performance counters.

    --gdb
       Run in gdb mode enabling remote debugging from gdb.

    --profileinst file
       Report executed instruction frequencies to the given file.

    --setreg spec ...
       Initialize registers. Example --setreg x1=4 x2=0xff

    --disass code ...
       Disassemble instruction code(s). Example --disass 0x93 0x33

    --configfile file
       Configuration file (JSON file defining system features).

    --abinames
       Use ABI register names (e.g. sp instead of x2) in instruction disassembly.

    --newlib
       Enable limited emulation of newlib system calls.
  
    --verbose
       Produce additional messages.

    --version 
       Print version.


## Interactive Mode

Whisper is started in interactive mode using the "--interactive" command line option.
Here's are some examples:

    $ whisper --interactive
    $ whisper --interactive test1

In the second example, the program test1 is first loaded into the
simulated memory.  In interactive mode the user can issue commands to
control the execution of the target program and to set/examine the
registers and memory location of the simulated system. The help command
will produce a list of all available interactive commands. The "help x"
command will produce information about command x.

Here's the output of the "help" command:

    help [<command>]
      Print help for given command or for all commands if no command given.
    
    run
      Run till interrupted.
    
    until <address>
      Run until address or interrupted.
    
    step [<n>]
      Execute n instructions (1 if n is missing).
    
    peek <res> <addr>
      Print value of resource res (one of r, f, c, m) and address addr.
      For memory (m) up to 2 addresses may be provided to define a range
      of memory locations to be printed.
      examples: peek r x1   peek c mtval   peek m 0x4096
    
    peek pc
      Print value of the program counter.
    
    peek all
      Print value of all non-memory resources.
    
    poke res addr value
      Set value of resource res (one of r, c or m) and address addr.
      Examples: poke r x1 0xff  poke c 0x4096 0xabcd
    
    disass opcode <code> <code> ...
      Disassemble opcodes. Example: disass opcode 0x3b 0x8082
    
    disass function <name>
      Disassemble function with given name. Example: disass func main
    
    disass <addr1> <addr2>>
      Disassemble memory locations between addr1 and addr2.
    
    elf file
      Load elf file into simulated memory.
    
    hex file
      Load hex file into simulated memory.
    
    replay_file file
      Open command file for replay.
    
    replay n
      Execute the next n commands in the replay file or all the
      remaining commands if n is missing.
    
    replay step n
      Execute consecutive commands from the replay file until n
      step commands are executed or the file is exhausted.
    
    reset [<reset_pc>]
      Reset hart.  If reset_pc is given, then change the reset program
      counter to the given reset_pc before resetting the hart.

    quit
      Terminate the simulator.
    

## Newlib Emulation

Whisper will emulate the newlib open, close, read, write, brk and exit
system calls. This allows simple programs to run and use the newlib C-library
functions such as printf, fopen, fread, fwrite, fclose, malloc,
free and exit. Here an example of running a program with limited
C-library support:

    $ whisper --newlib test3

And here are examples of passing the command line arguments arg1 and arg2
to the to the target program test3:

    $ whisper --newlib test3 arg1 arg2

If the target program command line arguments require the use of dashes
then a double dash must be used to separate the target program and its
command line switches from those of whisper:

    $ whisper --newlib -- test4 -opt1 ...

# Debugging RISCV Programs Using Gdb and Whisper

With the --gdb option, whisper will follow the gdb remote debugging
protocol.  This allows the user to debug a RISCV program using a
cross-compiled gdb and whisper.  For example, to debug a RISCV program
named xyz on a Linux x86 machine, we would start the (cross-compiled)
RISCV gdb as follows: 

    $ riscv-unknown-elf-gdb xyz

at the gdb prompt, we would connect to whisper by issuing a "target remote"
gdb command as follows:

    target remote | whisper --gdb xyz


# Configuring Whisper

# Known Issues

The MISA register is read only. It is not possible to change XLEN at
run time by writing to the MISA register.

The "round to nearest break tie to max magnitude" rounding mode is not
implemented.

No multi-hart support.

No virtual memory support.

Only extensions A, C, D, F, I, M, S and U are supported.

The code was originally written to support the Swerv
micro-controller. That controller only supports machine mode.
