//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2018 Western Digital Corporation or its affiliates.
// 
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
// 
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
// 
// You should have received a copy of the GNU General Public License along with
// this program. If not, see <https://www.gnu.org/licenses/>.
//

#include <iomanip>
#include <iostream>
#include <fstream>
#include <experimental/filesystem>

#include <cstring>
#include <ctime>
#include <sys/times.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>

#ifndef __MINGW64__
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#endif

#include "Hart.hpp"
#include "Syscall.hpp"


using namespace WdRiscv;


// Copy x86 stat buffer to riscv kernel_stat buffer.
static void
copyStatBufferToRiscv(const struct stat& buff, void* rvBuff)
{
  char* ptr = (char*) rvBuff;
  *((uint64_t*) ptr) = buff.st_dev;             ptr += 8;
  *((uint64_t*) ptr) = buff.st_ino;             ptr += 8;
  *((uint32_t*) ptr) = buff.st_mode;            ptr += 4;
  *((uint32_t*) ptr) = buff.st_nlink;           ptr += 4;
  *((uint32_t*) ptr) = buff.st_uid;             ptr += 4;
  *((uint32_t*) ptr) = buff.st_gid;             ptr += 4;
  *((uint64_t*) ptr) = buff.st_rdev;            ptr += 8;
  /* __pad1 */                                  ptr += 8;
  *((uint64_t*) ptr) = buff.st_size;            ptr += 8;

#ifdef __APPLE__
  // TODO: adapt code for Mac OS.
  ptr += 40;
#elif defined __MINGW64__
  /* *((uint32_t*) ptr) = buff.st_blksize; */   ptr += 4;
  /* __pad2 */                                  ptr += 4;
  /* *((uint64_t*) ptr) = buff.st_blocks; */    ptr += 8;
  *((uint32_t*) ptr) = buff.st_atime;           ptr += 4;
  *((uint32_t*) ptr) = 0;                       ptr += 4;
  *((uint32_t*) ptr) = buff.st_mtime;           ptr += 4;
  *((uint32_t*) ptr) = 0;                       ptr += 4;
  *((uint32_t*) ptr) = buff.st_ctime;           ptr += 4;
  *((uint32_t*) ptr) = 0;                       ptr += 4;
#else
  *((uint32_t*) ptr) = buff.st_blksize;         ptr += 4;
  /* __pad2 */                                  ptr += 4;
  *((uint64_t*) ptr) = buff.st_blocks;          ptr += 8;
  *((uint32_t*) ptr) = buff.st_atim.tv_sec;     ptr += 4;
  *((uint32_t*) ptr) = buff.st_atim.tv_nsec;    ptr += 4;
  *((uint32_t*) ptr) = buff.st_mtim.tv_sec;     ptr += 4;
  *((uint32_t*) ptr) = buff.st_mtim.tv_nsec;    ptr += 4;
  *((uint32_t*) ptr) = buff.st_ctim.tv_sec;     ptr += 4;
  *((uint32_t*) ptr) = buff.st_ctim.tv_nsec;    ptr += 4;
#endif
}


// Copy x86 tms struct (used by times) to riscv (32-bit version).
static void
copyTmsToRiscv32(const struct tms& buff, void* rvBuff)
{
  char* ptr = (char*) rvBuff;
  *((uint32_t*) ptr) = buff.tms_utime;          ptr += 4;
  *((uint32_t*) ptr) = buff.tms_stime;          ptr += 4;
  *((uint32_t*) ptr) = buff.tms_cutime;         ptr += 4;
  *((uint32_t*) ptr) = buff.tms_cstime;         ptr += 4;
}


// Copy x86 tms struct (used by times) to riscv (64-bit version).
static void
copyTmsToRiscv64(const struct tms& buff, void* rvBuff)
{
  char* ptr = (char*) rvBuff;
  *((uint64_t*) ptr) = buff.tms_utime;          ptr += 8;
  *((uint64_t*) ptr) = buff.tms_stime;          ptr += 8;
  *((uint64_t*) ptr) = buff.tms_cutime;         ptr += 8;
  *((uint64_t*) ptr) = buff.tms_cstime;         ptr += 8;
}


// Copy x86 timeval buffer to riscv timeval buffer (32-bit version).
static void
copyTimevalToRiscv32(const struct timeval& buff, void* rvBuff)
{
  char* ptr = (char*) rvBuff;
  *((uint64_t*) ptr) = buff.tv_sec;             ptr += 8;
  *((uint32_t*) ptr) = buff.tv_usec;            ptr += 4;
}


// Copy x86 timeval buffer to riscv timeval buffer (32-bit version).
static void
copyTimevalToRiscv64(const struct timeval& buff, void* rvBuff)
{
  char* ptr = (char*) rvBuff;
  *((uint64_t*) ptr) = buff.tv_sec;             ptr += 8;
  *((uint64_t*) ptr) = buff.tv_usec;            ptr += 8;
}


// Copy x86 timezone to riscv
static void
copyTimezoneToRiscv(const struct timezone& buff, void* rvBuff)
{
  char* ptr = (char*) rvBuff;
  *((uint32_t*) ptr) = buff.tz_minuteswest;     ptr += 4;
  *((uint32_t*) ptr) = buff.tz_dsttime;         ptr += 4;
}


/// Syscall numbers about which we have already complained.
static std::vector<bool> reportedCalls(4096);


template <typename URV>
bool
Syscall<URV>::redirectOutputDescriptor(int fd, const std::string& path)
{
  if (fdMap_.count(fd))
    {
      std::cerr << "Hart::redirectOutputDecritpor: Error: File decriptor " << fd
                << " alrady used.\n";
      return false;
    }

  int newFd = open(path.c_str(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
  if (newFd < 0)
    {
      std::cerr << "Error: Failed to open file " << path << " for output\n";
      return false;
    }
  fdMap_[fd] = newFd;
  fdIsRead_[fd] = false;
  fdPath_[fd] = path;

  auto absPath = std::experimental::filesystem::absolute(path);
  writePaths_.insert(absPath.string());

  return true;
}


template <typename URV>
void
Syscall<URV>::reportOpenedFiles(std::ostream& out)
{
  if (not readPaths_.empty())
    {
      out << "Files opened for read:\n";
      for (auto path : readPaths_)
        out << "  " << path << '\n';
    }

  if (not writePaths_.empty())
    {
      out << "Files opened for write/read-write:\n";
      for (auto path : writePaths_)
        out << "  " << path << '\n';
    }
}


template <typename URV>
int
Syscall<URV>::registerLinuxFd(int linuxFd, const std::string& path, bool isRead)
{
  if (linuxFd < 0)
    return linuxFd;

  int riscvFd = linuxFd;
  int maxFd = linuxFd;
  bool used = false;

  for (auto kv : fdMap_)
    {
      int rfd = kv.first;
      if (riscvFd == rfd)
        used = true;
      maxFd = std::max(maxFd, rfd);
    }

  if (used)
    riscvFd = maxFd + 1;

  fdMap_[riscvFd] = linuxFd;
  fdIsRead_[riscvFd] = isRead;
  fdPath_[riscvFd] = path;

  auto absPath = std::experimental::filesystem::absolute(path);
  if (isRead)
    readPaths_.insert(absPath.string());
  else
    writePaths_.insert(absPath.string());

  return riscvFd;
}


template <typename URV>
URV
Syscall<URV>::emulate()
{
  static std::unordered_map<int, std::string> names =
    {
     {0,    "io_setup"},
     {1,    "io_destroy"},
     {2,    "io_submit"},
     {3,    "io_cancel"},
     {4,    "io_getevents"},
     {5,    "setxattr"},
     {6,    "lsetxattr"},
     {7,    "fsetxattr"},
     {8,    "getxattr"},
     {9,    "lgetxattr"},
     {10,   "fgetxattr"},
     {11,   "listxattr"},
     {12,   "llistxattr"},
     {13,   "flistxattr"},
     {14,   "removexattr"},
     {15,   "lremovexattr"},
     {16,   "fremovexattr"},
     {17,   "getcwd"},
     {18,   "lookup_dcookie"},
     {19,   "eventfd2"},
     {20,   "epoll_create1"},
     {21,   "epoll_ctl"},
     {22,   "epoll_pwait"},
     {23,   "dup"},
     {24,   "dup3"},
     {25,   "fcntl"},
     {26,   "inotify_init1"},
     {27,   "inotify_add_watch"},
     {28,   "inotify_rm_watch"},
     {29,   "ioctl"},
     {30,   "ioprio_get"},
     {31,   "ioprio_set"},
     {32,   "flock"},
     {33,   "mknodat"},
     {34,   "mkdirat"},
     {35,   "unlinkat"},
     {36,   "symlinkat"},
     {37,   "linkat"},
     {38,   "renameat"},
     {39,   "umount2"},
     {40,   "mount"},
     {41,   "pivot_root"},
     {42,   "nfsservctl"},
     {43,   "statfs"},
     {44,   "fstatfs"},
     {45,   "truncate"},
     {46,   "ftruncate"},
     {47,   "fallocate"},
     {48,   "faccessat"},
     {49,   "chdir"},
     {50,   "fchdir"},
     {51,   "chroot"},
     {52,   "fchmod"},
     {53,   "fchmodat"},
     {54,   "fchownat"},
     {55,   "fchown"},
     {56,   "openat"},
     {57,   "close"},
     {58,   "vhangup"},
     {59,   "pipe2"},
     {60,   "quotactl"},
     {61,   "getdents64"},
     {62,   "lseek"},
     {63,   "read"},
     {64,   "write"},
     {66,   "writev"},
     {67,   "pread64"},
     {68,   "pwrite64"},
     {69,   "preadv"},
     {70,   "pwritev"},
     {71,   "sendfile"},
     {72,   "pselect6"},
     {73,   "ppoll"},
     {74,   "signalfd64"},
     {75,   "vmsplice"},
     {76,   "splice"},
     {77,   "tee"},
     {78,   "readlinkat"},
     {79,   "fstatat"},
     {80,   "fstat"},
     {81,   "sync"},
     {82,   "fsync"},
     {83,   "fdatasync"},
     {84,   "sync_file_range2"},
     {85,   "timerfd_create"},
     {86,   "timerfd_settime"},
     {87,   "timerfd_gettime"},
     {88,   "utimensat"},
     {89,   "acct"},
     {90,   "capget"},
     {91,   "capset"},
     {92,   "personality"},
     {93,   "exit"},
     {94,   "exit_group"},
     {95,   "waitid"},
     {96,   "set_tid_address"},
     {97,   "unshare"},
     {98,   "futex"},
     {99,   "set_robust_list"},
     {100,  "get_robust_list"},
     {101,  "nanosleep"},
     {102,  "getitimer"},
     {103,  "setitimer"},
     {104,  "kexec_load"},
     {105,  "init_module"},
     {106,  "delete_module"},
     {107,  "timer_create"},
     {108,  "timer_gettime"},
     {109,  "timer_getoverrun"},
     {110,  "timer_settime"},
     {111,  "timer_delete"},
     {112,  "clock_settime"},
     {113,  "clock_gettime"},
     {114,  "clock_getres"},
     {115,  "clock_nanosleep"},
     {116,  "syslog"},
     {117,  "ptrace"},
     {118,  "sched_setparam"},
     {119,  "sched_setscheduler"},
     {120,  "sched_getscheduler"},
     {121,  "sched_getparam"},
     {122,  "sched_setaffinity"},
     {123,  "sched_getaffinity"},
     {124,  "sched_yield"},
     {125,  "sched_get_priority_max"},
     {126,  "sched_get_priority_min"},
     {127,  "scheD_rr_get_interval"},
     {128,  "restart_syscall"},
     {129,  "kill"},
     {130,  "tkill"},
     {131,  "tgkill"},
     {132,  "sigaltstack"},
     {133,  "rt_sigsuspend"},
     {134,  "rt_sigaction"},
     {135,  "rt_sigprocmask"},
     {136,  "rt_sigpending"},
     {137,  "rt_sigtimedwait"},
     {138,  "rt_sigqueueinfo"},
     {139,  "rt_sigreturn"},
     {140,  "setpriority"},
     {141,  "getpriority"},
     {142,  "reboot"},
     {143,  "setregid"},
     {144,  "setgid"},
     {145,  "setreuid"},
     {146,  "setuid"},
     {147,  "setresuid"},
     {148,  "getresuid"},
     {149,  "getresgid"},
     {150,  "getresgid"},
     {151,  "setfsuid"},
     {152,  "setfsgid"},
     {153,  "times"},
     {154,  "setpgid"},
     {155,  "getpgid"},
     {156,  "getsid"},
     {157,  "setsid"},
     {158,  "getgroups"},
     {159,  "setgroups"},
     {160,  "uname"},
     {161,  "sethostname"},
     {162,  "setdomainname"},
     {163,  "getrlimit"},
     {164,  "setrlimit"},
     {165,  "getrusage"},
     {166,  "umask"},
     {167,  "prctl"},
     {168,  "getcpu"},
     {169,  "gettimeofday"},
     {170,  "settimeofday"},
     {171,  "adjtimex"},
     {172,  "getpid"},
     {173,  "getppid"},
     {174,  "getuid" },
     {175,  "geteuid"},
     {176,  "getgid" },
     {177,  "getegid"},
     {178,  "gettid" },
     {179,  "sysinfo"},
     {180,  "mq_open"},
     {181,  "mq_unlink"},
     {182,  "mq_timedsend"},
     {183,  "mq_timedrecieve"},
     {184,  "mq_notify"},
     {185,  "mq_getsetattr"},
     {186,  "msgget"},
     {187,  "msgctl"},
     {188,  "msgrcv"},
     {189,  "msgsnd"},
     {190,  "semget"},
     {191,  "semctl"},
     {192,  "semtimedop"},
     {193,  "semop"},
     {194,  "shmget"},
     {195,  "shmctl"},
     {196,  "shmat"},
     {197,  "shmdt"},
     {198,  "socket"},
     {199,  "socketpair"},
     {200,  "bind"},
     {201,  "listen"},
     {202,  "accept"},
     {203,  "connect"},
     {204,  "getsockname"},
     {205,  "getpeername"},
     {206,  "sendo"},
     {207,  "recvfrom"},
     {208,  "setsockopt"},
     {209,  "getsockopt"},
     {210,  "shutdown"},
     {211,  "sendmsg"},
     {212,  "recvmsg"},
     {213,  "readahead"},
     {214,  "brk"},
     {215,  "munmap"},
     {216,  "mremap"},
     {217,  "add_key"},
     {218,  "request_key"},
     {219,  "keyctl"},
     {220,  "clone"},
     {221,  "execve"},
     {222,  "mmap"},
     {223,  "fadvise64"},
     {224,  "swapon"},
     {225,  "swapoff"},
     {226,  "mprotect"},
     {227,  "msync"},
     {228,  "mlock"},
     {229,  "munlock"},
     {230,  "mlockall"},
     {231,  "munlockall"},
     {232,  "mincore"},
     {233,  "madvise"},
     {234,  "remap_file_pages"},
     {235,  "mbind"},
     {236,  "get_mempolicy"},
     {237,  "set_mempolicy"},
     {238,  "migrate_pages"},
     {239,  "move_pages"},
     {240,  "tgsigqueueinfo"},
     {241,  "perf_event_open"},
     {242,  "accept4"},
     {243,  "recvmmsg"},
     {260,  "wait4"},
     {261,  "prlimit64"},
     {262,  "fanotify_init"},
     {263,  "fanotify_mark"},
     {264,  "name_to_handle_at"},
     {265,  "open_by_handle_at"},
     {266,  "clock_adjtime"},
     {267,  "syncfs"},
     {268,  "setns"},
     {269,  "sendmmsg"},
     {270,  "process_vm_ready"},
     {271,  "process_vm_writev"},
     {272,  "kcmp"},
     {273,  "finit_module"},
     {274,  "sched_setattr"},
     {275,  "sched_getattr"},
     {276,  "renameat2"},
     {277,  "seccomp"},
     {278,  "getrandom"},
     {279,  "memfd_create"},
     {280,  "bpf"},
     {281,  "execveat"},
     {282,  "userfaultid"},
     {283,  "membarrier"},
     {284,  "mlock2"},
     {285,  "copy_file_range"},
     {286,  "preadv2"},
     {287,  "pwritev2"},
     {1024, "open"},
     {1025, "link"},
     {1026, "unlink"},
     {1027, "mknod"},
     {1028, "chmod"},
     {1029, "chown"},
     {1030, "mkdir"},
     {1031, "rmdir"},
     {1032, "lchown"},
     {1033, "access"},
     {1034, "rename"},
     {1035, "readlink"},
     {1036, "symlink"},
     {1037, "utimes"},
     {1038, "stat"},
     {1039, "lstat"},
     {1040, "pipe"},
     {1041, "dup2"},
     {1042, "epoll_create"},
     {1043, "inotifiy_init"},
     {1044, "eventfd"},
     {1045, "signalfd"},
     {1046, "sendfile"},
     {1047, "ftruncate"},
     {1048, "truncate"},
     {1049, "stat"},
     {1050, "lstat"},
     {1051, "fstat"},
     {1052, "fcntl" },
     {1053, "fadvise64"},
     {1054, "newfstatat"},
     {1055, "fstatfs"},
     {1056, "statfs"},
     {1057, "lseek"},
     {1058, "mmap"},
     {1059, "alarm"},
     {1060, "getpgrp"},
     {1061, "pause"},
     {1062, "time"},
     {1063, "utime"},
     {1064, "creat"},
     {1065, "getdents"},
     {1066, "futimesat"},
     {1067, "select"},
     {1068, "poll"},
     {1069, "epoll_wait"},
     {1070, "ustat"},
     {1071, "vfork"},
     {1072, "oldwait4"},
     {1073, "recv"},
     {1074, "send"},
     {1075, "bdflush"},
     {1076, "umount"},
     {1077, "uselib"},
     {1078, "sysctl"},
     {1079, "fork"},
     {2011, "getmainvars"}

    };
  // Preliminary. Need to avoid using syscall numbers.

  // On success syscall returns a non-negtive integer.
  // On failure it returns the negative of the error number.
  URV a0 = hart_.peekIntReg(RegA0);
  URV a1 = hart_.peekIntReg(RegA1);
  URV a2 = hart_.peekIntReg(RegA2);
  // using urv_ll = long long;

#ifndef __MINGW64__
  URV a3 = hart_.peekIntReg(RegA3);
#endif

  URV num = hart_.peekIntReg(RegA7);


  switch (num)
    {
#ifndef __MINGW64__
    case 17:       // getcwd
      {
	size_t size = a1;
	size_t buffAddr = 0;
	if (not hart_.getSimMemAddr(a0, buffAddr))
	  return SRV(-EINVAL);
	errno = 0;
	if (not getcwd((char*) buffAddr, size))
	  return SRV(-errno);
	// Linux getced system call returns count of bytes placed in buffer
	// unlike the C-library interface which returns pointer to buffer.
	return strlen((char*) buffAddr) + 1;
      }

    case 25:       // fcntl
      {
	int fd = effectiveFd(SRV(a0));
	int cmd = SRV(a1);
	void* arg = (void*) size_t(a2);
	switch (cmd)
	  {
	  case F_GETLK:
	  case F_SETLK:
	  case F_SETLKW:
	    {
	      size_t addr = 0;
	      if (not hart_.getSimMemAddr(a2, addr))
		return SRV(-EINVAL);
	      arg = (void*) addr;
	    }
	  }
	int rc = fcntl(fd, cmd, arg);
	return rc;
      }

    case 29:       // ioctl
      {
	int fd = effectiveFd(SRV(a0));
	int req = SRV(a1);
	size_t addr = 0;
	if (a2 != 0)
	  if (not hart_.getSimMemAddr(a2, addr))
	    return SRV(-EINVAL);
	errno = 0;
	int rc = ioctl(fd, req, (char*) addr);
	return rc < 0 ? SRV(-errno) : rc;
      }

    case 35:       // unlinkat
      {
	int fd = effectiveFd(SRV(a0));
	size_t pathAddr = 0;
	if (not hart_.getSimMemAddr(a1, pathAddr))
	  return SRV(-1);
	int flags = SRV(a2);

	errno = 0;
	int rc = unlinkat(fd, (char*) pathAddr, flags);
	return rc < 0 ? SRV(-errno) : rc;
      }

    case 46:       // ftruncate
      {
        errno = 0;
        SRV rc =  ftruncate(a0, a1);
        return rc < 0 ? SRV(-errno) : rc;
      }

    case 49:       // chdir
      {
	size_t pathAddr = 0;
	if (not hart_.getSimMemAddr(a0, pathAddr))
	  return SRV(-1);

	errno = 0;
	int rc = chdir((char*) pathAddr);
	return rc < 0 ? SRV(-errno) : rc;
      }

    case 56:       // openat
      {
	int dirfd = effectiveFd(SRV(a0));

	size_t pathAddr = 0;
	if (not hart_.getSimMemAddr(a1, pathAddr))
	  return SRV(-EINVAL);
	const char* path = (const char*) pathAddr;

	int flags = a2;
	int x86Flags = 0;
	if (linux_)
	  x86Flags = flags;
	else
	  {
	    // Newlib constants differ from Linux: compensate.
	    if (flags & 1)     x86Flags |= O_WRONLY;
	    if (flags & 0x2)   x86Flags |= O_RDWR;
	    if (flags & 0x200) x86Flags |= O_CREAT;
	  }

	mode_t mode = a3;

	errno  = 0;
	int rc = openat(dirfd, path, x86Flags, mode);
        if (rc >= 0)
          {
            bool isRead = not (x86Flags & (O_WRONLY | O_RDWR));
            rc = registerLinuxFd(rc, path, isRead);
            if (rc < 0)
              return SRV(-EINVAL);
          }
	return rc < 0 ? SRV(-errno) : rc;
      }

    case 61:       // getdents64  -- get directory entries
      {
	// TBD: double check that struct linux_dirent is same
	// in x86 and RISCV 32/64.
	int fd = effectiveFd(SRV(a0));
	size_t buffAddr = 0;
	if (not hart_.getSimMemAddr(a1, buffAddr))
	  return SRV(-EINVAL);
	size_t count = a2;
	off64_t base = 0;

	errno = 0;
	int rc = getdirentries64(fd, (char*) buffAddr, count, &base);
	return rc < 0 ? SRV(-errno) : rc;
      }

    case 62:       // lseek
      {
	int fd = effectiveFd(a0);
	size_t offset = a1;
	int whence = a2;

	errno = 0;
	int rc = lseek(fd, offset, whence);
	return rc < 0 ? SRV(-errno) : rc;
      }

    case 66:       // writev
      {
	int fd = effectiveFd(SRV(a0));

	size_t iovAddr = 0;
	if (not hart_.getSimMemAddr(a1, iovAddr))
	  return SRV(-EINVAL);

	int count = a2;

	unsigned errors = 0;
	struct iovec* iov = new struct iovec [count];
	for (int i = 0; i < count; ++i)
	  {
	    URV* vec = (URV*) iovAddr;
	    URV base = vec[i*2];
	    URV len = vec[i*2+1];
	    size_t addr = 0;
	    if (not hart_.getSimMemAddr(base, addr))
	      {
		errors++;
		break;
	      }
	    iov[i].iov_base = (void*) addr;
	    iov[i].iov_len = len;
	  }
	ssize_t rc = -EINVAL;
	if (not errors)
	  {
	    errno = 0;
	    rc = writev(fd, iov, count);
	    rc = rc < 0 ? SRV(-errno) : rc;
	  }

	delete [] iov;
	return SRV(rc);
      }

    case 78:       // readlinat
      {
	int dirfd = effectiveFd(SRV(a0));
	URV path = a1;
	URV buf = a2;
	URV bufSize = a3;

	size_t pathAddr = 0;
	if (not hart_.getSimMemAddr(path, pathAddr))
	  return SRV(-EINVAL);

	size_t bufAddr = 0;
	if (not hart_.getSimMemAddr(buf, bufAddr))
	  return SRV(-EINVAL);

	errno = 0;
	ssize_t rc = readlinkat(dirfd, (const char*) pathAddr,
				(char*) bufAddr, bufSize);
	return  rc < 0 ? SRV(-errno) : rc;
      }

    case 79:       // fstatat
      {
	int dirFd = effectiveFd(SRV(a0));

	size_t pathAddr = 0;
	if (not hart_.getSimMemAddr(a1, pathAddr))
	  return SRV(-1);

	size_t rvBuff = 0;
	if (not hart_.getSimMemAddr(a2, rvBuff))
	  return SRV(-1);

	int flags = a3;

	struct stat buff;
	errno = 0;
	int rc = fstatat(dirFd, (char*) pathAddr, &buff, flags);
	if (rc < 0)
	  return SRV(-errno);

	// RvBuff contains an address: We cast it to a pointer.
        copyStatBufferToRiscv(buff, (void*) rvBuff);
	return rc;
      }
#endif

    case 80:       // fstat
      {
	int fd = effectiveFd(SRV(a0));
	size_t rvBuff = 0;
	if (not hart_.getSimMemAddr(a1, rvBuff))
	  return SRV(-1);
	struct stat buff;

	errno = 0;
	int rc = fstat(fd, &buff);
	if (rc < 0)
	  return SRV(-errno);

	// RvBuff contains an address: We cast it to a pointer.
        copyStatBufferToRiscv(buff, (void*) rvBuff);
	return rc;
      }


    case 214: // brk
       {
     	  URV newBrk = a0;
     	  URV rc = newBrk;
          if (newBrk == 0)
            rc = progBreak_;
          else
            {
              for (URV addr = newBrk; addr<progBreak_; addr++)
                hart_.pokeMemory(addr, uint8_t(0));
              rc = progBreak_ = newBrk;
            }
     	  return rc;
       }


    case 57: // close
      {
	int fd = effectiveFd(SRV(a0));
	int rc = 0;
	if (fd > 2)
	  {
	    errno = 0;
	    rc = close(fd);
	    rc = rc < 0? -errno : rc;
            fdMap_.erase(a0);
            fdIsRead_.erase(a0);
            fdPath_.erase(a0);
	  }
	return SRV(rc);
      }

    case 63: // read
      {
	int fd = effectiveFd(SRV(a0));
	size_t buffAddr = 0;
	if (not hart_.getSimMemAddr(a1, buffAddr))
	  return SRV(-1);
	size_t count = a2;

	errno = 0;
	ssize_t rc = read(fd, (void*) buffAddr, count);
	return rc < 0 ? SRV(-errno) : rc;
      }

    case 64: // write
      {
	int fd = effectiveFd(SRV(a0));
	size_t buffAddr = 0;
	if (not hart_.getSimMemAddr(a1, buffAddr))
	  return SRV(-1);
	size_t count = a2;

	errno = 0;
	auto rc = write(fd, (void*) buffAddr, count);
	return rc < 0 ? SRV(-errno) : rc;
      }

    case 93:  // exit
      {
	throw CoreException(CoreException::Exit, "", 0, a0);
	return 0;
      }

    case 94:  // exit_group
      {
	throw CoreException(CoreException::Exit, "", 0, a0);
	return 0;
      }

#ifndef __MINGW64__
    case 153: // times
      {
	size_t buffAddr = 0;
	if (not hart_.getSimMemAddr(a0, buffAddr))
	  return SRV(-1);

	errno = 0;

	struct tms tms0;
	auto ticks = times(&tms0);
	if (ticks < 0)
	  return SRV(-errno);

	if (sizeof(URV) == 4)
	  copyTmsToRiscv32(tms0, (void*) buffAddr);
	else
	  copyTmsToRiscv64(tms0, (void*) buffAddr);
	
	return ticks;
      }

    case 160: // uname
      {
	// Assumes that x86 and rv Linux have same layout for struct utsname.
	size_t buffAddr = 0;
	if (not hart_.getSimMemAddr(a0, buffAddr))
	  return SRV(-1);
	struct utsname* uts = (struct utsname*) buffAddr;

	errno = 0;
	int rc = uname(uts);
	strcpy(uts->release, "4.14.0");
	return rc < 0 ? SRV(-errno) : rc;
      }

    case 169: // gettimeofday
      {
	size_t tvAddr = 0;  // Address of riscv timeval
	if (not hart_.getSimMemAddr(a0, tvAddr))
	  return SRV(-EINVAL);

	size_t tzAddr = 0;  // Address of rsicv timezone
	if (not hart_.getSimMemAddr(a1, tzAddr))
	  return SRV(-EINVAL);

	struct timeval tv0;
	struct timeval* tv0Ptr = &tv0;

	struct timezone tz0;
	struct timezone* tz0Ptr = &tz0;
	
	if (tvAddr == 0) tv0Ptr = nullptr;
	if (tzAddr == 0) tz0Ptr = nullptr;

	errno = 0;
	int rc = gettimeofday(tv0Ptr, tz0Ptr);
	if (rc < 0)
	  return SRV(-errno);

	if (tvAddr)
	  {
	    if (sizeof(URV) == 4)
	      copyTimevalToRiscv32(tv0, (void*) tvAddr);
	    else
	      copyTimevalToRiscv64(tv0, (void*) tvAddr);
	  }
	
	if (tzAddr)
	  copyTimezoneToRiscv(tz0, (void*) tzAddr);

	return rc;
      }

    case 174: // getuid
      {
	SRV rc = getuid();
	return rc;
      }

    case 175: // geteuid
      {
	SRV rv = geteuid();
	return rv;
      }

    case 176: // getgid
      {
	SRV rv = getgid();
	return rv;
      }

    case 177: // getegid
      {
	SRV rv = getegid();
	return rv;
      }

    case 215: // unmap
      {
    	URV addr = a0;
    	URV size = a1;
    	return mmap_dealloc(addr, size);
      }

    case 216: // mremap
      {
    	URV addr = a0;
    	URV old_size = a1;
    	URV new_size = ((a2+(1<<12)-1)>>12)<<12;
    	bool maymove = a3 & MREMAP_MAYMOVE;
    	return  mmap_remap(addr,old_size,new_size, maymove);
      }

    case 222: // mmap2
      {
        URV start = a0;
        URV length = a1;
        URV prot = a2;
        URV tgt_flags = a3;

        if ((start & (((1<<12)-1) - 1)) ||
            ((tgt_flags & MAP_PRIVATE) == (tgt_flags & MAP_SHARED))  ||
            ((prot & PROT_WRITE) && (tgt_flags & MAP_SHARED)) ||
            !(tgt_flags & MAP_ANONYMOUS) or (tgt_flags & MAP_FIXED) ||
            !length) {
          return -1;
        }

        length = ((length+(1<<12)-1)>>12)<<12;

        return mmap_alloc(length);
      }
#endif

    case 276:  // rename
      {
        size_t pathAddr = 0;
        if (not hart_.getSimMemAddr(a1, pathAddr))
          return SRV(-EINVAL);
        const char* oldName = (const char*) pathAddr;

        size_t newPathAddr = 0;
        if (not hart_.getSimMemAddr(a3, newPathAddr))
          return SRV(-EINVAL);
        const char* newName = (const char*) newPathAddr;

        int result = rename(oldName, newName);
        return (result == -1) ? -errno : result;
      }

    case 1024: // open
      {
	size_t pathAddr = 0;
	if (not hart_.getSimMemAddr(a0, pathAddr))
	  return SRV(-1);
	int flags = a1;
	int x86Flags = 0;
	if (linux_)
          x86Flags = flags;
	else
	  {
	    // Newlib constants differ from Linux: compensate.
	    if (flags & 1)     x86Flags |= O_WRONLY;
	    if (flags & 0x2)   x86Flags |= O_RDWR;
	    if (flags & 0x200) x86Flags |= O_CREAT;
	  }
	int mode = a2;

	errno = 0;
	int rc = open((const char*) pathAddr, x86Flags, mode);
        if (rc >= 0)
          {
            bool isRead = not (x86Flags & (O_WRONLY | O_RDWR));
            rc = registerLinuxFd(rc, (char*) pathAddr, isRead);
            if (rc < 0)
              return SRV(-EINVAL);
          }
	return rc < 0 ? SRV(-errno) : rc;
      }

    case 1026: // unlink
      {
	size_t pathAddr = 0;
	if (not hart_.getSimMemAddr(a0, pathAddr))
	  return SRV(-1);

	errno = 0;
	int rc = unlink((char*) pathAddr);
	return rc < 0 ? SRV(-errno) : rc;
      }

    case 1038: // stat
      {
	size_t filePathAddr = 0;
	if (not hart_.getSimMemAddr(a0, filePathAddr))
	  return SRV(-EINVAL);

	// FilePathAddr contains an address: We cast it to a pointer.
	struct stat buff;
	errno = 0;
	SRV rc = stat((char*) filePathAddr, &buff);
	if (rc < 0)
	  return SRV(-errno);

	size_t rvBuff = 0;
	if (not hart_.getSimMemAddr(a1, rvBuff))
	  return SRV(-EINVAL);

	// RvBuff contains an address: We cast it to a pointer.
        copyStatBufferToRiscv(buff, (void*) rvBuff);
	return rc;
      }
    }
  //printf("syscall %s (0x%llx, 0x%llx, 0x%llx, 0x%llx) = 0x%llx\n",names[num].c_str(),urv_ll(a0), urv_ll(a1),urv_ll(a2), urv_ll(a3), urv_ll(retVal));
  //printf("syscall %s (0x%llx, 0x%llx, 0x%llx, 0x%llx) = unimplemented\n",names[num].c_str(),urv_ll(a0), urv_ll(a1),urv_ll(a2), urv_ll(a3));
  if (num < reportedCalls.size() and reportedCalls.at(num))
	 return -1;

  std::cerr << "Unimplemented syscall " << names[int(num)] << " number " << num << "\n";

   if (num < reportedCalls.size())
	 reportedCalls.at(num) = true;
   return -1;
}


template <typename URV>
bool
Syscall<URV>::saveFileDescriptors(const std::string& path)
{
  std::ofstream ofs(path, std::ios::trunc);
  if (not ofs)
    {
      std::cerr << "Syscall::saveFileDescriptors: Failed to open " << path << " for write\n";
      return false;
    }

  for (auto kv : fdMap_)
    {
      int fd = kv.first;
      int remapped = kv.second;
      std::string path = fdPath_[fd];
      bool isRead = fdIsRead_[fd];
      off_t position = lseek(remapped, 0, SEEK_CUR);
      ofs << path << ' ' << fd << ' ' << position << ' ' << isRead << '\n';
    }

  return true;
}


template <typename URV>
bool
Syscall<URV>::loadFileDescriptors(const std::string& path)
{
  std::ifstream ifs(path);
  if (not ifs)
    {
      std::cerr << "Syscall::loadFileDescriptors: Failed to open "
                << path << " for read\n";
      return false;
    }

  unsigned errors = 0;

  std::string line;
  unsigned lineNum = 0;
  while (std::getline(ifs, line))
    {
      lineNum++;
      std::istringstream iss(line);
      std::string fdPath;
      int fd = 0;
      off_t position = 0;
      bool isRead = false;
      if (not (iss >> fdPath >> fd >> position >> isRead))
        {
          std::cerr << "File " << path << ", Line " << lineNum << ": "
                    << "Failed to parse line\n";
          return false;
        }

      if (isRead)
        {
          int newFd = open(fdPath.c_str(), O_RDONLY);
          if (newFd < 0)
            {
              std::cerr << "Hart::loadFileDecriptors: Failed to open file "
                        << fdPath << " for read\n";
              errors++;
              continue;
            }
          if (lseek(newFd, position, SEEK_SET) == off_t(-1))
            {
              std::cerr << "Hart::loadFileDecriptors: Failed to seek on file "
                        << fdPath << '\n';
              errors++;
              continue;
            }
          fdMap_[fd] = newFd;
          fdIsRead_[fd] = true;
          readPaths_.insert(fdPath);
        }
      else
        {
          int newFd = -1;
          if (std::experimental::filesystem::is_regular_file(fdPath))
            {
              newFd = open(fdPath.c_str(), O_RDWR);
              if (lseek(newFd, position, SEEK_SET) == off_t(-1))
                {
                  std::cerr << "Hart::loadFileDecriptors: Failed to seek on file "
                            << fdPath << '\n';
                  errors++;
                  continue;
                }
            }
          else
            newFd = open(fdPath.c_str(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);

          if (newFd < 0)
            {
              std::cerr << "Hart::loadFileDecriptors: Failed to open file "
                        << fdPath << " for write\n";
              errors++;
              continue;
            }
          fdMap_[fd] = newFd;
          fdIsRead_[fd] = false;
          writePaths_.insert(fdPath);
        }
    }

  return errors == 0;
}


template <typename URV>
uint64_t
Syscall<URV>::mmap_alloc(uint64_t size)
{
  auto it = mmap_blocks_.begin();
  for (; it!=mmap_blocks_.end(); ++it)
    if(it->second.free and it->second.length>=size)
      break;

  if (it != mmap_blocks_.end())
    {
      auto orig_size = it->second.length;
      auto addr = it->first;
      it->second.free = false;
      if(orig_size > size)
        {
          mmap_blocks_.insert(std::make_pair(addr+size, blk_t(orig_size-size, true)));
          it->second.length =  size;
        }
      //print_mmap("alloc");
      return addr;
    }
  assert(false);
  return uint64_t(-1);
}


template <typename URV>
int
Syscall<URV>::mmap_dealloc(uint64_t addr, uint64_t size)
{
  auto curr = mmap_blocks_.find(addr);
  if (curr == mmap_blocks_.end())
    {
      assert(false);
      return -1;
    }
  auto curr_size = curr->second.length;
  assert(not curr->second.free and curr_size == size);
  curr->second.free = true;
  auto mem_addr = curr->first;
  auto mem_end_addr = mem_addr+(curr_size);
  for (; mem_addr<mem_end_addr; mem_addr+=uint64_t(sizeof(uint64_t)))
    hart_.pokeMemory(mem_addr,uint64_t(0));
  auto next = curr;
  if (++next != mmap_blocks_.end() and next->second.free)
    {
      curr->second.length += next->second.length;
      mmap_blocks_.erase(next);
    }
  if(curr != mmap_blocks_.begin())
    {
      auto prev = curr;
      if ((--prev)->second.free)
        {
          prev->second.length += curr->second.length;
          mmap_blocks_.erase(curr);
        }
    }
  //print_mmap("dealloc");
  return 0;
}


template <typename URV>
uint64_t
Syscall<URV>::mmap_remap(uint64_t addr, uint64_t old_size, uint64_t new_size,
                         bool maymove)
{
  if (old_size == new_size) return addr;
  auto curr = mmap_blocks_.find(addr);

  if (old_size>new_size)
    {
      assert(curr != mmap_blocks_.end() and curr->second.length == old_size and not curr->second.free);
      curr->second.length = new_size;
      mmap_blocks_.insert(std::make_pair(addr+new_size, blk_t(old_size-new_size, false)));
      mmap_dealloc(addr+new_size,old_size-new_size);
      //print_mmap("remap1");
      return addr;
    }
  auto next = curr;
  auto diff = new_size - old_size;
  if ((++next) != mmap_blocks_.end() and next->second.free and next->second.length >= diff)
    {
      curr->second.length = new_size;
      if(auto rest = next->second.length - diff)
        mmap_blocks_.insert(std::make_pair(next->first+diff, blk_t(rest, true)));
      mmap_blocks_.erase(next);
      //print_mmap("remap2");
      return addr;
    }
  else if(maymove)
    {
      auto new_addr = mmap_alloc(new_size);
      for (uint64_t index=0; index<old_size; index+=uint64_t(sizeof(uint64_t)))
        {
          uint64_t data;
          hart_.peekMemory(addr+index, data);
          hart_.pokeMemory(new_addr+index, data);
        }
      mmap_dealloc(addr, old_size);
      //print_mmap("remap3");
      return new_addr;
    }
  else
    return -1;

}


template<typename URV>
void
Syscall<URV>::getUsedMemBlocks(std::vector<std::pair<uint64_t,uint64_t>>& used_blocks)
{
  static const uint64_t max_stack_size = 1024*1024*8;
  auto mem_size = hart_.getMemorySize();
  used_blocks.clear();
  if (mem_size<=(max_stack_size+progBreak_))
    {
      used_blocks.push_back(std::pair<uint64_t,uint64_t>(0,mem_size));
      return;
    }
  used_blocks.push_back(std::pair<uint64_t,uint64_t>(0,progBreak_));
  for(auto& it:mmap_blocks_)
    if(not it.second.free)
      used_blocks.push_back(std::pair<uint64_t,uint64_t>(it.first,it.second.length));
  used_blocks.push_back(std::pair<uint64_t,uint64_t>(hart_.getMemorySize()-max_stack_size,max_stack_size));
}


template<typename URV>
bool
Syscall<URV>::loadUsedMemBlocks(const std::string& filename, std::vector<std::pair<uint64_t,uint64_t>>& used_blocks)
{
  // open file for read, check success
  used_blocks.clear();
  std::ifstream ifs(filename);
  if (not ifs)
    {
      std::cerr << "Syscall::loadUsedMemBlocks failed - cannot open "
                << filename << " for read\n";
      return false;
    }
  std::string line;
  mmap_blocks_.clear();
  while (std::getline(ifs, line))
    {
      std::istringstream iss(line);
      uint64_t addr, length;
      iss >> addr;
      iss >> length;
      used_blocks.push_back(std::pair<uint64_t,uint64_t>(addr, length));
    }

  return true;
}


template<typename URV>
bool
Syscall<URV>::saveUsedMemBlocks(const std::string& filename,
                                std::vector<std::pair<uint64_t,uint64_t>>& used_blocks)
{
  // open file for write, check success
  std::ofstream ofs(filename, std::ios::trunc);
  if (not ofs)
    {
      std::cerr << "Syscall::saveUsedMemBlocks failed - cannot open "
                << filename << " for write\n";
      return false;
    }
  getUsedMemBlocks(used_blocks);
  for (auto& it: used_blocks)
    ofs << it.first << " " << it.second << "\n";
  return true;
}


template <typename URV>
bool
Syscall<URV>::saveMmap(const std::string & filename)
{
  // open file for write, check success
  std::ofstream ofs(filename, std::ios::trunc);
  if (not ofs)
    {
      std::cerr << "Syscall::saveMmap failed - cannot open " << filename
                << " for write\n";
      return false;
    }

  for (auto& it: mmap_blocks_)
    ofs << it.first << " " << it.second.length << " " << it.second.free <<"\n";

  return true;
}


template <typename URV>
bool
Syscall<URV>::loadMmap(const std::string & filename)
{
  // open file for read, check success
  std::ifstream ifs(filename);
  if (not ifs)
    {
      std::cerr << "Syscall::loadMmap failed - cannot open " << filename
                << " for read\n";
      return false;
    }
  std::string line;
  mmap_blocks_.clear();
  while(std::getline(ifs, line))
    {
      std::istringstream iss(line);
      uint64_t addr, length;
      bool valid;
      iss >> addr;
      iss >> length;
      iss >> valid;
      mmap_blocks_.insert(std::make_pair(addr, blk_t(length, valid)));
    }

  return true;
}

template class WdRiscv::Syscall<uint32_t>;
template class WdRiscv::Syscall<uint64_t>;
