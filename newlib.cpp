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

#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef __MINGW64__
#include <sys/uio.h>
#include <sys/utsname.h>
#endif

#include "Core.hpp"


using namespace WdRiscv;


// Copy x86 stat buffer to riscv kernel_stat buffer (32-bit version).
static void
copyStatBufferToRiscv32(const struct stat& buff, void* rvBuff)
{
  char* ptr = (char*) rvBuff;
  *((uint64_t*) ptr) = buff.st_dev;             ptr += 4;
  *((uint64_t*) ptr) = buff.st_ino;             ptr += 4;
  *((uint32_t*) ptr) = buff.st_mode;            ptr += 4;
  *((uint32_t*) ptr) = buff.st_nlink;           ptr += 4;
  *((uint32_t*) ptr) = buff.st_uid;             ptr += 4;
  *((uint32_t*) ptr) = buff.st_gid;             ptr += 4;
  *((uint64_t*) ptr) = buff.st_rdev;            ptr += 4;
  /* __pad1 */                                  ptr += 4;
  *((uint64_t*) ptr) = buff.st_size;            ptr += 4;

#ifdef __APPLE__
  // TODO: adapt code for Mac OS.
  ptr += 36;
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
  *((uint64_t*) ptr) = buff.st_blocks;          ptr += 4;
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



template <typename URV>
URV
Core<URV>::emulateNewlib()
{
  // Preliminary. Need to avoid using syscall numbers.

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
    case 56:       // openat
      {
	int dirfd = a0;

	size_t pathAddr = 0;
	if (not memory_.getSimMemAddr(a1, pathAddr))
	  return SRV(-1);
	const char* path = (const char*) pathAddr;

	int flags = a2;
	int x86Flags = 0;
	if (flags & 1) x86Flags |= O_WRONLY;
	if (flags & 0x200) x86Flags |= O_CREAT;

	mode_t mode = a3;
	int rc = openat(dirfd, path, x86Flags, mode);
	return SRV(rc);
      }

    case 62:       // lseek
      {
	int fd = a0;
	size_t offset = a1;
	int whence = a2;
	int rc = lseek(fd, offset, whence);
	return SRV(rc);
      }

    case 66:       // writev
      {
	int fd = a0;

	size_t iovAddr = 0;
	if (not memory_.getSimMemAddr(a1, iovAddr))
	  return SRV(-1);

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
	ssize_t rc = -1;
	if (not errors)
	  rc = writev(fd, iov, count);
	delete [] iov;
	return SRV(rc);
      }

    case 78:       // readlinat
      {
	int dirfd = a0;
	URV path = a1;
	URV buf = a2;
	URV bufSize = a3;

	size_t pathAddr = 0;
	if (not memory_.getSimMemAddr(path, pathAddr))
	  return SRV(-1);

	size_t bufAddr = 0;
	if (not memory_.getSimMemAddr(buf, bufAddr))
	  return SRV(-1);
	ssize_t rc = readlinkat(dirfd, (const char*) pathAddr,
				(char*) bufAddr, bufSize);
	return SRV(rc);
      }

    case 79:       // fstatat
      {
	int dirFd = a0;

	size_t pathAddr = 0;
	if (not memory_.getSimMemAddr(a1, pathAddr))
	  return SRV(-1);

	size_t rvBuff = 0;
	if (not memory_.getSimMemAddr(a2, rvBuff))
	  return SRV(-1);

	int flags = a3;

	struct stat buff;
	SRV rv = fstatat(dirFd, (char*) pathAddr, &buff, flags);
	if (rv < 0)
	  return rv;

	// RvBuff contains an address: We cast it to a pointer.
	if (sizeof(URV) == 4)
	  copyStatBufferToRiscv32(buff, (void*) rvBuff);
	else
	  copyStatBufferToRiscv64(buff, (void*) rvBuff);
	return rv;
      }
#endif

    case 80:       // fstat
      {
	int fd = a0;
	size_t rvBuff = 0;
	if (not memory_.getSimMemAddr(a1, rvBuff))
	  return SRV(-1);
	struct stat buff;
	SRV rv = fstat(fd, &buff);
	if (rv < 0)
	  return rv;

	// RvBuff contains an address: We cast it to a pointer.
	if (sizeof(URV) == 4)
	  copyStatBufferToRiscv32(buff, (void*) rvBuff);
	else
	  copyStatBufferToRiscv64(buff, (void*) rvBuff);
	return rv;
      }

    case 214: // brk
      {
	if (a0 < progBreak_)
	  return progBreak_;
	progBreak_ = a0;
	return a0;
      }

    case 57: // close
      {
	int fd = a0;
	SRV rv = 0;
	if (fd > 2)
	  rv = close(fd);
	return rv;
      }

    case 63: // read
      {
	int fd = a0;
	size_t buffAddr = 0;
	if (not memory_.getSimMemAddr(a1, buffAddr))
	  return SRV(-1);
	size_t count = a2;
	ssize_t rv = read(fd, (void*) buffAddr, count);
	return URV(rv);
      }

    case 64: // write
      {
	int fd = a0;
	size_t buffAddr = 0;
	if (not memory_.getSimMemAddr(a1, buffAddr))
	  return SRV(-1);
	size_t count = a2;
	auto rv = write(fd, (void*) buffAddr, count);
	if (rv < 0)
	  {
	    char buffer[512];
	    char* p = strerror_r(errno, buffer, 512);
	    std::cerr << p << '\n';
	  }
	return URV(rv);
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
    case 160: // uname
      {
	// Assumes that x86 and rv Linux have same layout for struct utsname.
	size_t buffAddr = 0;
	if (not memory_.getSimMemAddr(a0, buffAddr))
	  return SRV(-1);
	struct utsname* uts = (struct utsname*) buffAddr;
	int rc = uname(uts);
	strcpy(uts->release, "4.14.0");
	return SRV(rc);
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
#endif

    case 1024: // open
      {
	size_t pathAddr = 0;
	if (not memory_.getSimMemAddr(a0, pathAddr))
	  return SRV(-1);
	int flags = a1;
	int x86Flags = 0;
	if (flags & 1) x86Flags |= O_WRONLY;
	if (flags & 0x2) x86Flags |= O_RDWR;
	if (flags & 0x200) x86Flags |= O_CREAT;
	int mode = a2;
	SRV fd = open((const char*) pathAddr, x86Flags, mode);
	return fd;
      }

    case 1038: // stat
      {
	size_t filePathAddr = 0;
	if (not memory_.getSimMemAddr(a0, filePathAddr))
	  return SRV(-1);

	// FilePathAddr contains an address: We cast it to a pointer.
	struct stat buff;
	SRV rv = stat((char*) filePathAddr, &buff);
	if (rv < 0)
	  return rv;

	size_t rvBuff = 0;
	if (not memory_.getSimMemAddr(a1, rvBuff))
	  return SRV(-1);

	// RvBuff contains an address: We cast it to a pointer.
	if (sizeof(URV) == 4)
	  copyStatBufferToRiscv32(buff, (void*) rvBuff);
	else
	  copyStatBufferToRiscv64(buff, (void*) rvBuff);
	return rv;
      }

    default:
      break;
    }

  std::cerr << "Unimplemented syscall number " << num << "\n";
  return -1;
}


template class WdRiscv::Core<uint32_t>;
template class WdRiscv::Core<uint64_t>;
