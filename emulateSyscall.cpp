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

#include <cstring>
#include <ctime>
#include <sys/times.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef __MINGW64__
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#endif

#include "Hart.hpp"


using namespace WdRiscv;


// Copy x86 stat buffer to riscv kernel_stat buffer (32-bit version).
static void
copyStatBufferToRiscv32(const struct stat& buff, void* rvBuff)
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


// Copy x86 stat buffer to riscv kernel_stat buffer (64-bit version).
static void
copyStatBufferToRiscv64(const struct stat& buff, void* rvBuff)
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
Hart<URV>::redirectOutputDescriptor(int fd, const std::string& path)
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
  return true;
}


/// Map Linux file descriptor to a RISCV file descriptor and install
/// the result in the riscv-to-linux fd map. Return remapped
/// descritpor or -1 if remapping is not possible.
static
int
registerLinuxFd(std::unordered_map<int, int>& fdMap, int linuxFd)
{
  if (linuxFd < 0)
    return linuxFd;

  int riscvFd = linuxFd;
  int maxFd = linuxFd;
  bool used = false;

  for (auto kv : fdMap)
    {
      int rfd = kv.first;
      if (riscvFd == rfd)
        used = true;
      maxFd = std::max(maxFd, rfd);
    }

  if (used)
    riscvFd = maxFd + 1;

  fdMap[riscvFd] = linuxFd;
  return riscvFd;
}


template <typename URV>
URV
Hart<URV>::emulateSyscall()
{
  // Preliminary. Need to avoid using syscall numbers.

  // On success syscall returns a non-negtive integer.
  // On failure it returns the negative of the error number.

  URV a0 = intRegs_.read(RegA0);
  URV a1 = intRegs_.read(RegA1);
  URV a2 = intRegs_.read(RegA2);

#ifndef __MINGW64__
  URV a3 = intRegs_.read(RegA3);
#endif

  URV num = intRegs_.read(RegA7);

  switch (num)
    {
#ifndef __MINGW64__
    case 17:       // getcwd
      {
	size_t size = a1;
	size_t buffAddr = 0;
	if (not memory_.getSimMemAddr(a0, buffAddr))
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
	      if (not memory_.getSimMemAddr(a2, addr))
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
	  if (not memory_.getSimMemAddr(a2, addr))
	    return SRV(-EINVAL);
	errno = 0;
	int rc = ioctl(fd, req, (char*) addr);
	return rc < 0 ? SRV(-errno) : rc;
      }

    case 35:       // unlinkat
      {
	int fd = effectiveFd(SRV(a0));
	size_t pathAddr = 0;
	if (not memory_.getSimMemAddr(a1, pathAddr))
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
	if (not memory_.getSimMemAddr(a0, pathAddr))
	  return SRV(-1);

	errno = 0;
	int rc = chdir((char*) pathAddr);
	return rc < 0 ? SRV(-errno) : rc;
      }

    case 56:       // openat
      {
	int dirfd = effectiveFd(SRV(a0));

	size_t pathAddr = 0;
	if (not memory_.getSimMemAddr(a1, pathAddr))
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
            fdIsRead_[rc] = isRead;
            fdPath_[rc] = path;
            rc = registerLinuxFd(fdMap_, rc);
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
	if (not memory_.getSimMemAddr(a1, buffAddr))
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
	if (not memory_.getSimMemAddr(a1, iovAddr))
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
	    if (not memory_.getSimMemAddr(base, addr))
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
	if (not memory_.getSimMemAddr(path, pathAddr))
	  return SRV(-EINVAL);

	size_t bufAddr = 0;
	if (not memory_.getSimMemAddr(buf, bufAddr))
	  return SRV(-EINVAL);

	errno = 0;
	ssize_t rc = readlinkat(dirfd, (const char*) pathAddr,
				(char*) bufAddr, bufSize);
	return rc < 0 ? SRV(-errno) : rc;
      }

    case 79:       // fstatat
      {
	int dirFd = effectiveFd(SRV(a0));

	size_t pathAddr = 0;
	if (not memory_.getSimMemAddr(a1, pathAddr))
	  return SRV(-1);

	size_t rvBuff = 0;
	if (not memory_.getSimMemAddr(a2, rvBuff))
	  return SRV(-1);

	int flags = a3;

	struct stat buff;
	errno = 0;
	int rc = fstatat(dirFd, (char*) pathAddr, &buff, flags);
	if (rc < 0)
	  return SRV(-errno);

	// RvBuff contains an address: We cast it to a pointer.
	if (sizeof(URV) == 4)
	  copyStatBufferToRiscv32(buff, (void*) rvBuff);
	else
	  copyStatBufferToRiscv64(buff, (void*) rvBuff);
	return rc;
      }
#endif

    case 80:       // fstat
      {
	int fd = effectiveFd(SRV(a0));
	size_t rvBuff = 0;
	if (not memory_.getSimMemAddr(a1, rvBuff))
	  return SRV(-1);
	struct stat buff;

	errno = 0;
	int rc = fstat(fd, &buff);
	if (rc < 0)
	  return SRV(-errno);

	// RvBuff contains an address: We cast it to a pointer.
	if (sizeof(URV) == 4)
	  copyStatBufferToRiscv32(buff, (void*) rvBuff);
	else
	  copyStatBufferToRiscv64(buff, (void*) rvBuff);
	return rc;
      }

    case 214: // brk
      {
	if (a0 < progBreak_)
	  return progBreak_;
        if (a0 > memory_.size())
          return SRV(-1);
	progBreak_ = a0;
	return a0;
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
	if (not memory_.getSimMemAddr(a1, buffAddr))
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
	if (not memory_.getSimMemAddr(a1, buffAddr))
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
	if (not memory_.getSimMemAddr(a0, buffAddr))
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
	if (not memory_.getSimMemAddr(a0, buffAddr))
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
	if (not memory_.getSimMemAddr(a0, tvAddr))
	  return SRV(-EINVAL);

	size_t tzAddr = 0;  // Address of rsicv timezone
	if (not memory_.getSimMemAddr(a1, tzAddr))
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
	SRV rv = getuid();
	return rv;
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

    case 222: // mmap2
	{
	  // size_t addr = a0;
	  // size_t len = a1;
	  // int prot = a2;
	  // int flags = a3;
	  // int fd = intRegs_.read(RegA4);
	  // off_t offset = intRegs_.read(RegA5);
	  return -1;
	}
#endif

    case 276:  // rename
      {
        size_t pathAddr = 0;
        if (not memory_.getSimMemAddr(a1, pathAddr))
          return SRV(-EINVAL);
        const char* oldName = (const char*) pathAddr;

        size_t newPathAddr = 0;
        if (not memory_.getSimMemAddr(a3, newPathAddr))
          return SRV(-EINVAL);
        const char* newName = (const char*) newPathAddr;

        int result = rename(oldName, newName);
        return (result == -1) ? -errno : result;
      }

    case 1024: // open
      {
	size_t pathAddr = 0;
	if (not memory_.getSimMemAddr(a0, pathAddr))
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
            fdIsRead_[rc] = isRead;
            fdPath_[rc] = (char*) pathAddr;
            fdMap_[rc] = rc;
          }
	return rc < 0 ? SRV(-errno) : rc;
      }

    case 1026: // unlink
      {
	size_t pathAddr = 0;
	if (not memory_.getSimMemAddr(a0, pathAddr))
	  return SRV(-1);

	errno = 0;
	int rc = unlink((char*) pathAddr);
	return rc < 0 ? SRV(-errno) : rc;
      }

    case 1038: // stat
      {
	size_t filePathAddr = 0;
	if (not memory_.getSimMemAddr(a0, filePathAddr))
	  return SRV(-EINVAL);

	// FilePathAddr contains an address: We cast it to a pointer.
	struct stat buff;
	errno = 0;
	SRV rc = stat((char*) filePathAddr, &buff);
	if (rc < 0)
	  return SRV(-errno);

	size_t rvBuff = 0;
	if (not memory_.getSimMemAddr(a1, rvBuff))
	  return SRV(-EINVAL);

	// RvBuff contains an address: We cast it to a pointer.
	if (sizeof(URV) == 4)
	  copyStatBufferToRiscv32(buff, (void*) rvBuff);
	else
	  copyStatBufferToRiscv64(buff, (void*) rvBuff);
	return rc;
      }

    default:
      break;
    }

  if (num < reportedCalls.size() and reportedCalls.at(num))
    return -1;

  std::cerr << "Unimplemented syscall number " << num << "\n";
  if (num < reportedCalls.size())
    reportedCalls.at(num) = true;

  return -1;
}


template class WdRiscv::Hart<uint32_t>;
template class WdRiscv::Hart<uint64_t>;
