/**
 * Copyright (C) 2008 Doug Judd (Zvents, Inc.)
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or any later version.
 *
 * Hypertable is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "Common/Compat.h"

#include <iomanip>
#include <iostream>
#include <sstream>

extern "C" {
#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#ifdef HT_HT_XATTR_ENABLED
# include <sys/xattr.h>
# if defined(__linux__)
#   include <attr/xattr.h>
# endif
#endif
}

#include <boost/shared_array.hpp>

#include "FileUtils.h"
#include "Logger.h"

using namespace Hypertable;
using namespace std;


/**
 */
ssize_t FileUtils::read(int fd, void *vptr, size_t n) {
  size_t nleft;
  ssize_t nread;
  char *ptr;

  ptr = (char *)vptr;
  nleft = n;
  while (nleft > 0) {
    if ((nread = ::read(fd, ptr, nleft)) < 0) {
      if (errno == EINTR)
        nread = 0;/* and call read() again */
      else if (errno == EAGAIN)
        break;
      else
        return -1;
    } else if (nread == 0)
      break;/* EOF */

    nleft -= nread;
    ptr   += nread;
  }
  return n - nleft;
}

/**
 */
ssize_t FileUtils::pread(int fd, void *vptr, size_t n, off_t offset) {
  size_t nleft;
  ssize_t nread;
  char *ptr;

  ptr = (char *)vptr;
  nleft = n;
  while (nleft > 0) {
    if ((nread = ::pread(fd, ptr, nleft, offset)) < 0) {
      if (errno == EINTR)
        nread = 0;/* and call read() again */
      else if (errno == EAGAIN)
        break;
      else
        return -1;
    } else if (nread == 0)
      break;/* EOF */

    nleft -= nread;
    ptr   += nread;
    offset += nread;
  }
  return n - nleft;
}

/**
 */
ssize_t FileUtils::write(int fd, const void *vptr, size_t n) {
  size_t nleft;
  ssize_t nwritten;
  const char *ptr;

  ptr = (const char *)vptr;
  nleft = n;
  while (nleft > 0) {
    if ((nwritten = ::write(fd, ptr, nleft)) <= 0) {
      if (errno == EINTR)
        nwritten = 0; /* and call write() again */
      if (errno == EAGAIN)
        break;
      else
        return -1; /* error */
    }

    nleft -= nwritten;
    ptr   += nwritten;
  }
  return n - nleft;
}

ssize_t FileUtils::writev(int fd, const struct iovec *vector, int count) {
  ssize_t nwritten;
  while ((nwritten = ::writev(fd, vector, count)) <= 0) {
    if (errno == EINTR)
      nwritten = 0; /* and call write() again */
    if (errno == EAGAIN) {
      nwritten = 0;
      break;
    }
    else
      return -1; /* error */
  }
  return nwritten;
}


ssize_t FileUtils::sendto(int fd, const void *vptr, size_t n, const struct sockaddr *to, socklen_t tolen) {
  size_t nleft;
  ssize_t nsent;
  const char *ptr;

  ptr = (const char *)vptr;
  nleft = n;
  while (nleft > 0) {
    if ((nsent = ::sendto(fd, ptr, nleft, 0, to, tolen)) <= 0) {
      if (errno == EINTR)
        nsent = 0; /* and call sendto() again */
      if (errno == EAGAIN || errno == ENOBUFS)
        break;
      else
        return -1; /* error */
    }

    nleft -= nsent;
    ptr   += nsent;
  }
  return n - nleft;
}

ssize_t FileUtils::recvfrom(int fd, void *vptr, size_t n, struct sockaddr *from, socklen_t *fromlen) {
  ssize_t nread;
  while (true) {
    if ((nread = ::recvfrom(fd, vptr, n, 0, from, fromlen)) < 0) {
      if (errno != EINTR)
        break;
    }
    else
      break;
  }
  return nread;
}


/* flags are file status flags to turn on */
void FileUtils::set_flags(int fd, int flags) {
  int val;

  if ((val = fcntl(fd, F_GETFL, 0)) < 0)
    cerr << "fcnt(F_GETFL) failed : " << strerror(errno) << endl;

  val |= flags;

  if (fcntl(fd, F_SETFL, val) < 0)
    cerr << "fcnt(F_SETFL) failed : " << strerror(errno) << endl;
}



/**
 */
char *FileUtils::file_to_buffer(const String &fname, off_t *lenp) {
  struct stat statbuf;
  int fd;

  *lenp = 0;

  if ((fd = open(fname.c_str(), O_RDONLY)) < 0) {
    HT_ERRORF("open(\"%s\") failure - %s", fname.c_str(),  strerror(errno));
    return 0;
  }

  if (fstat(fd, &statbuf) < 0) {
    HT_ERRORF("fstat(\"%s\") failure - %s", fname.c_str(),  strerror(errno));
    return 0;
  }

  *lenp = statbuf.st_size;

  char *rbuf = new char [*lenp + 1];

  ssize_t nread = FileUtils::read(fd, rbuf, *lenp);

  if (nread == (ssize_t)-1) {
    HT_ERRORF("read(\"%s\") failure - %s", fname.c_str(),  strerror(errno));
    delete [] rbuf;
    *lenp = 0;
    return 0;
  }

  if (nread < *lenp) {
    HT_WARNF("short read (%d of %d bytes)", nread, *lenp);
    *lenp = nread;
  }

  rbuf[nread] = 0;

  return rbuf;
}


bool FileUtils::mkdirs(const String &dirname) {
  struct stat statbuf;
  boost::shared_array<char> tmp_dir(new char [dirname.length() + 1]);
  char *tmpdir = tmp_dir.get();
  char *ptr = tmpdir+1;

  strcpy(tmpdir, dirname.c_str());

  while ((ptr = strchr(ptr, '/')) != 0) {
    *ptr = 0;
    if (stat(tmpdir, &statbuf) != 0) {
      if (errno == ENOENT) {
        if (mkdir(tmpdir, 0755) != 0) {
          HT_ERRORF("Problem creating directory '%s' - %s", tmpdir, strerror(errno));
          return false;
        }
      }
      else {
        HT_ERRORF("Problem stat'ing directory '%s' - %s", tmpdir, strerror(errno));
        return false;
      }
    }
    *ptr++ = '/';
  }

  if (stat(tmpdir, &statbuf) != 0) {
    if (errno == ENOENT) {
      if (mkdir(tmpdir, 0755) != 0) {
        HT_ERRORF("Problem creating directory '%s' - %s", tmpdir, strerror(errno));
        return false;
      }
    }
    else {
      HT_ERRORF("Problem stat'ing directory '%s' - %s", tmpdir, strerror(errno));
      return false;
    }
  }

  return true;
}


bool FileUtils::exists(const String &fname) {
  struct stat statbuf;
  if (stat(fname.c_str(), &statbuf) != 0)
    return false;
  return true;
}

uint64_t FileUtils::size(const String &fname) {
  struct stat statbuf;
  if (stat(fname.c_str(), &statbuf) != 0)
    return 0;
  return statbuf.st_size;

}


off_t FileUtils::length(const String &fname) {
  struct stat statbuf;
  if (stat(fname.c_str(), &statbuf) != 0)
    return (off_t)-1;
  return statbuf.st_size;
}


void FileUtils::add_trailing_slash(String &path) {
  if (path.find('/', path.length()-1) == string::npos)
    path += "/";
}


bool FileUtils::expand_tilde(String &fname) {
  struct passwd pbuf;
  struct passwd *prbuf;
  char buf[256];

  if (fname[0] != '~')
    return false;

  if (fname[1] == '/') {
    if (getpwuid_r(getuid() , &pbuf, buf, 256, &prbuf) != 0 || prbuf == 0)
      return false;
    fname = (String)pbuf.pw_dir + fname.substr(1);
  }
  else {
    String name;
    size_t first_slash = fname.find_first_of('/');

    if (first_slash == string::npos)
      name = fname.substr(1);
    else
      name = fname.substr(1, first_slash-1);

    if (getpwnam_r(name.c_str() , &pbuf, buf, 256, &prbuf) != 0 || prbuf == 0)
      return false;

    if (first_slash == string::npos)
      fname = pbuf.pw_dir;
    else
      fname = (String)pbuf.pw_dir + fname.substr(first_slash);
  }

  return true;
}


#ifdef HT_XATTR_ENABLED

int FileUtils::getxattr(const String &path, const String &name, void *value, size_t size) {
  String canonic = (String)"user." + name;
#if defined(__linux__)
  return ::getxattr(path.c_str(), canonic.c_str(), value, size);
#elif defined(__APPLE__)
  return ::getxattr(path.c_str(), canonic.c_str(), value, size, 0, 0);
#else
  ImplementMe;
#endif
}


int FileUtils::setxattr(const String &path, const String &name, const void *value, size_t size, int flags) {
  String canonic = (String)"user." + name;
#if defined(__linux__)
  return ::setxattr(path.c_str(), canonic.c_str(), value, size, flags);
#elif defined(__APPLE__)
  return ::setxattr(path.c_str(), canonic.c_str(), value, size, 0, flags);
#else
  ImplementMe;
#endif
}


int FileUtils::fgetxattr(int fd, const String &name, void *value, size_t size) {
  String canonic = (String)"user." + name;
#if defined(__linux__)
  return ::fgetxattr(fd, canonic.c_str(), value, size);
#elif defined(__APPLE__)
  return ::fgetxattr(fd, canonic.c_str(), value, size, 0, 0);
#else
  ImplementMe;
#endif
}


int FileUtils::fsetxattr(int fd, const String &name, const void *value, size_t size, int flags) {
  String canonic = (String)"user." + name;
#if defined(__linux__)
  return ::fsetxattr(fd, canonic.c_str(), value, size, flags);
#elif defined(__APPLE__)
  return ::fsetxattr(fd, canonic.c_str(), value, size, 0, flags);
#else
  ImplementMe;
#endif
}


int FileUtils::removexattr(const String &path, const String &name) {
  String canonic = (String)"user." + name;
#if defined(__linux__)
  return ::removexattr(path.c_str(), canonic.c_str());
#elif defined(__APPLE__)
  return ::removexattr(path.c_str(), canonic.c_str(), 0);
#else
  ImplementMe;
#endif
}

int FileUtils::fremovexattr(int fd, const String &name) {
  String canonic = (String)"user." + name;
#if defined(__linux__)
  return ::fremovexattr(fd, canonic.c_str());
#elif defined(__APPLE__)
  return ::fremovexattr(fd, canonic.c_str(), 0);
#else
  ImplementMe;
#endif

}

#endif // HT_XATTR_ENABLED
