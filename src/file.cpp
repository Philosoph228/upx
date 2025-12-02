/* file.cpp --

   This file is part of the UPX executable compressor.

   Copyright (C) 1996-2025 Markus Franz Xaver Johannes Oberhumer
   Copyright (C) 1996-2025 Laszlo Molnar
   All Rights Reserved.

   UPX and the UCL library are free software; you can redistribute them
   and/or modify them under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of
   the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.
   If not, write to the Free Software Foundation, Inc.,
   59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

   Markus F.X.J. Oberhumer              Laszlo Molnar
   <markus@oberhumer.com>               <ezerotven+github@gmail.com>
 */

#include <fstream>
#include <istream>
#include <ostream>

#include "conf.h"
#include "file.h"

/*************************************************************************
// static file-related util functions; will throw on error
**************************************************************************/

/*static*/ void FileBase::chmod(const char *name, int mode) {
    assert(name != nullptr && name[0] != 0);
#if HAVE_CHMOD
    if (::chmod(name, mode) != 0)
        throwIOException(name, errno);
#else
    UNUSED(name);
    UNUSED(mode);
    // no error
#endif
}

/*static*/ void FileBase::rename(const char *old_, const char *new_) {
#if (ACC_OS_DOS32) && defined(__DJGPP__)
    if (::_rename(old_, new_) != 0)
#else
    if (::rename(old_, new_) != 0)
#endif
        throwIOException("rename error", errno);
}

/*static*/ bool FileBase::unlink_noexcept(const char *name) noexcept {
    assert_noexcept(name != nullptr && name[0] != 0);
    bool success = ::unlink(name) == 0;
#if HAVE_CHMOD
    if (!success)
        success = (::chmod(name, 0666) == 0 && ::unlink(name) == 0);
#endif
    return success;
}

/*static*/ void FileBase::unlink(const char *name) {
    if (!unlink_noexcept(name))
        throwIOException(name, errno);
}

/*************************************************************************
// FileBase
**************************************************************************/

FileBase::~FileBase() may_throw {
#if 0 && defined(__GNUC__) // debug
    if (isOpen())
        fprintf(stderr, "%s: %s\n", _name, __PRETTY_FUNCTION__);
#endif
    if (std::uncaught_exceptions() == 0)
        closex(); // may_throw
    else
        close_noexcept(); // currently in exception unwinding, use noexcept variant
}

bool FileBase::do_sopen() {
    if (_shflags < 0)
        _fd = ::open(_name, _flags, _mode);
    else {
#if (ACC_OS_DOS32) && defined(__DJGPP__)
        _fd = ::open(_name, _flags | _shflags, _mode);
#elif (ACC_ARCH_M68K && ACC_OS_TOS && ACC_CC_GNUC) && defined(__MINT__)
        _fd = ::open(_name, _flags | (_shflags & O_SHMODE), _mode);
#elif defined(SH_DENYRW)
        _fd = ::sopen(_name, _flags, _shflags, _mode);
#else
        throwInternalError("bad usage of do_sopen()");
#endif
    }
    if (_fd < 0)
        return false;
    st.st_size = 0;
    if (::fstat(_fd, &st) != 0)
        throwIOException(_name, errno);
    _length = st.st_size;
    return true;
}

bool FileBase::close_noexcept() noexcept {
    bool ok = true;
    if (isOpen() && _fd != STDIN_FILENO && _fd != STDOUT_FILENO && _fd != STDERR_FILENO)
        if (::close(_fd) == -1)
            ok = false;
    _fd = -1;
    _flags = 0;
    _mode = 0;
    _name = nullptr;
    _offset = 0;
    _length = 0;
    return ok;
}

void FileBase::closex() may_throw {
    if (!close_noexcept())
        throwIOException("close failed", errno);
}

// Return value of ::seek is the resulting file offset (same as ::tell())
upx_off_t FileBase::seek(upx_off_t off, int whence) {
    if (!isOpen())
        throwIOException("bad seek 1");
    if (!mem_size_valid_bytes(off >= 0 ? off : -off)) // sanity check
        throwIOException("bad seek");
    if (whence == SEEK_SET) {
        if (off < 0)
            throwIOException("bad seek 2");
        off += _offset;
    } else if (whence == SEEK_END) {
        if (off > 0)
            throwIOException("bad seek 3");
        off += _offset + _length;
        whence = SEEK_SET;
    } else if (whence == SEEK_CUR) {
    } else
        throwInternalError("bad seek: whence");
    upx_off_t l = ::lseek(_fd, off, whence);
    if (l < 0)
        throwIOException("seek error", errno);
    return l - _offset;
}

upx_off_t FileBase::tell() const {
    if (!isOpen())
        throwIOException("bad tell");
    upx_off_t l = ::lseek(_fd, 0, SEEK_CUR);
    if (l < 0)
        throwIOException("tell error", errno);
    return l - _offset;
}

void FileBase::set_extent(upx_off_t offset, upx_off_t length) {
    _offset = offset;
    _length = length;
}

upx_off_t FileBase::st_size() const { return _length; }

/*************************************************************************
// InputFile
**************************************************************************/

void InputFile::sopen(const char *name, int flags, int shflags) {
    closex();
    _name = name;
    _flags = flags;
    _shflags = shflags;
    _mode = 0;
    _offset = 0;
    _length = 0;
    if (!super::do_sopen()) {
        if (errno == ENOENT)
            throw FileNotFoundException(_name, errno);
        else if (errno == EEXIST)
            throw FileAlreadyExistsException(_name, errno);
        else
            throwIOException(_name, errno);
    }
    _length_orig = _length;
}

int InputFile::read(SPAN_P(void) buf, upx_int64_t blen) {
    if (!isOpen() || blen < 0)
        throwIOException("bad read");
    int len = (int) mem_size(1, blen); // sanity check
    errno = 0;
    long l = acc_safe_hread(_fd, raw_bytes(buf, len), len);
    if (errno)
        throwIOException("read error", errno);
    return (int) l;
}

int InputFile::readx(SPAN_P(void) buf, upx_int64_t blen) {
    int l = this->read(buf, blen);
    if (l != blen)
        throwEOFException();
    return l;
}

upx_off_t InputFile::seek(upx_off_t off, int whence) {
    upx_off_t pos = super::seek(off, whence);
    if (_length < pos)
        throwIOException("bad seek 4");
    return pos;
}

upx_off_t InputFile::st_size_orig() const { return _length_orig; }

int InputFile::dupFd() may_throw {
    if (!isOpen())
        throwIOException("bad dup");
#if defined(HAVE_DUP) && (HAVE_DUP + 0 == 0)
    errno = ENOSYS;
    int r = -1;
#else
    int r = ::dup(getFd());
#endif
    if (r < 0)
        throwIOException("dup", errno);
    return r;
}

/*************************************************************************
// InputStream
**************************************************************************/

InputStream::InputStream(std::istream &stream) : _stream(stream) {
    _stream.clear();
    std::streampos current = _stream.tellg();
    if (current == -1) {
        errno = EIO;
        throwIOException("tellg failed", errno);
    }

    _stream.seekg(0, std::ios::end);
    std::streampos end = _stream.tellg();
    if (end == -1) {
        errno = EIO;
        throwIOException("tellg failed at end", errno);
    }
    _length_orig = _length = static_cast<upx_off_t>(end);

    _stream.seekg(current);
}

int InputStream::read(SPAN_P(void) buf, upx_int64_t blen) {
    int len = (int) mem_size(1, blen); // sanity check
    errno = 0;
    _stream.read(static_cast<char *>(raw_bytes(buf, len)), len);
    std::streamsize l = _stream.gcount();
    if (l == 0) {
        if (_stream.eof()) {
            errno = 0;
        } else if (_stream.fail()) {
            errno = EINVAL;
        } else if (_stream.bad()) {
            errno = EIO;
        }
    }
    if (errno)
        throwIOException("read error", errno);
    return static_cast<int>(l);
}

int InputStream::readx(SPAN_P(void) buf, upx_int64_t blen) {
    int l = this->read(buf, blen);
    if (l != blen)
        throwEOFException();
    return l;
}

upx_off_t InputStream::seek(upx_off_t off, int whence) {
    std::ios::seekdir dir;
    switch (whence) {
    case SEEK_SET:
        dir = std::ios::beg;
        break;
    case SEEK_CUR:
        dir = std::ios::cur;
        break;
    case SEEK_END:
        dir = std::ios::end;
        break;
    default:
        throwIOException("bad seek whence");
        break;
    }

    _stream.clear();

    _stream.seekg(static_cast<std::streamoff>(off), dir);
    if (!_stream) {
        errno = EIO;
        throwIOException("seek failed");
    }

    std::streampos pos = _stream.tellg();
    if (pos < 0) {
        errno = EIO;
        throwIOException("tellg failed", errno);
    }

    if (_length >= 0 && pos > static_cast<std::streampos>(_length)) {
        errno = EINVAL;
        throwIOException("seek beyond end of stream", errno);
    }

    return static_cast<upx_off_t>(pos);
}

upx_off_t InputStream::st_size() const {
    _stream.clear();
    std::streampos current = _stream.tellg();
    if (current == -1) {
        errno = EIO;
        throwIOException("tellg failed", errno);
    }

    _stream.seekg(0, std::ios::end);
    std::streampos end = _stream.tellg();
    if (end == -1) {
        errno = EIO;
        throwIOException("tellg failed at end", errno);
    }
    _stream.seekg(current);
    return static_cast<upx_off_t>(end);
}

/*************************************************************************
// OutputFile
**************************************************************************/

void OutputFile::sopen(const char *name, int flags, int shflags, int mode) {
    closex();
    _name = name;
    _flags = flags;
    _shflags = shflags;
    _mode = mode;
    _offset = 0;
    _length = 0;
    if (!super::do_sopen()) {
#if 0
        // don't throw FileNotFound here -- this is confusing
        if (errno == ENOENT)
            throw FileNotFoundException(_name,errno);
        else
#endif
        if (errno == EEXIST)
            throw FileAlreadyExistsException(_name, errno);
        else
            throwIOException(_name, errno);
    }
}

bool OutputFile::openStdout(int flags, bool force) {
    closex();
    int fd = STDOUT_FILENO;
    if (!force && acc_isatty(fd))
        return false;
    _name = "<stdout>";
    _flags = flags;
    _shflags = -1;
    _mode = 0;
    _offset = 0;
    _length = 0;
    if (flags && acc_set_binmode(fd, 1) == -1)
        throwIOException(_name, errno);
    _fd = fd;
    return true;
}

void OutputFile::write(SPAN_0(const void) buf, upx_int64_t blen) {
    if (!isOpen() || blen < 0)
        throwIOException("bad write");
    // allow nullptr if blen == 0
    if (blen == 0)
        return;
    int len = (int) mem_size(1, blen); // sanity check
    errno = 0;
#if WITH_XSPAN >= 2
    NO_fprintf(stderr, "write %p %zd (%p) %d\n", buf.raw_ptr(), buf.raw_size_in_bytes(),
               buf.raw_base(), len);
#endif
    long l = acc_safe_hwrite(_fd, raw_bytes(buf, len), len);
    if (l != len)
        throwIOException("write error", errno);
    bytes_written += len;
#if TESTING && 0
    static upx_std_atomic(bool) dumping;
    if (!dumping) {
        dumping = true;
        char fn[64];
        static int part = 0;
        snprintf(fn, sizeof(fn), "upx-dump-%04d.data", part++);
        OutputFile::dump(fn, buf, len);
        dumping = false;
    }
#endif
}

upx_off_t OutputFile::st_size() const {
    if (opt->to_stdout) {     // might be a pipe ==> .st_size is invalid
        return bytes_written; // too big if seek()+write() instead of rewrite()
    }
    struct stat my_st;
    my_st.st_size = 0;
    if (::fstat(_fd, &my_st) != 0)
        throwIOException(_name, errno);
    return my_st.st_size;
}

void OutputFile::rewrite(SPAN_P(const void) buf, int len) {
    assert(!opt->to_stdout);
    write(buf, len);
    bytes_written -= len; // restore
}

upx_off_t OutputFile::seek(upx_off_t off, int whence) {
    if (!mem_size_valid_bytes(off >= 0 ? off : -off)) // sanity check
        throwIOException("bad seek");
    assert(!opt->to_stdout);
    switch (whence) {
    case SEEK_SET:
        if (bytes_written < off)
            bytes_written = off;
        _length = bytes_written; // cheap, lazy update; needed?
        break;
    case SEEK_END:
        _length = bytes_written; // necessary
        break;
    }
    return super::seek(off, whence);
}

// WARNING: fsync() does not exist in some Windows environments.
// This trick works only on UNIX-like systems.
// int OutputFile::read(void *buf, int len) {
//    fsync(_fd);
//    InputFile infile;
//    infile.open(this->getName(), O_RDONLY | O_BINARY);
//    infile.seek(this->tell(), SEEK_SET);
//    return infile.read(buf, len);
//}

void OutputFile::set_extent(upx_off_t offset, upx_off_t length) {
    super::set_extent(offset, length);
    bytes_written = 0;
    if (0 == offset && 0xffffffffLL == length) { // TODO: check all callers of this method
        st.st_size = 0;
        if (::fstat(_fd, &st) != 0)
            throwIOException(_name, errno);
        _length = st.st_size - offset;
    }
}

upx_off_t OutputFile::unset_extent() {
    upx_off_t l = ::lseek(_fd, 0, SEEK_END);
    if (l < 0)
        throwIOException("lseek error", errno);
    _offset = 0;
    _length = l;
    bytes_written = _length;
    return _length;
}

/*static*/ void OutputFile::dump(const char *name, SPAN_P(const void) buf, int len, int flags) {
    if (flags < 0)
        flags = O_CREAT | O_TRUNC;
    flags |= O_WRONLY | O_BINARY;
    OutputFile f;
    f.open(name, flags, 0600);
    f.write(raw_bytes(buf, len), len);
    f.closex();
}

/*************************************************************************
// OutputStream
**************************************************************************/

OutputStream::OutputStream(std::ostream &stream) : _stream(stream) {
    // clear stream error flags so seek/read will work
    _stream.clear();

    // try to determine current position and stream length if seekable
    std::streampos curr = _stream.tellp();
    if (curr != static_cast<std::streampos>(-1)) {
        // try to compute length by seeking to end and back
        if (_stream.seekp(0, std::ios::end)) {
            std::streampos end = _stream.tellp();
            if (end != static_cast<std::streampos>(-1)) {
                _length = static_cast<upx_off_t>(end);
            }
            // restore
            _stream.seekp(curr);
        }
    }
    // initialize bytes_written sensibly: if we could get _length, use it; otherwise 0
    bytes_written = _length;
}

void OutputStream::write(SPAN_0(const void) buf, upx_int64_t blen) {
    if (!isOpen() && !_stream.good()) // isOpen for FileBase; for ostream check stream state
        throwIOException("bad write");

    if (blen == 0)
        return;

    int len = (int) mem_size(1, blen); // sanity check
    errno = 0;

    // perform write via std::ostream
    _stream.write(static_cast<const char *>(raw_bytes(buf, len)), len);

    // flush? Usually not necessary. But if you want to ensure errors are reported:
    // _stream.flush();

    if (!_stream) {
        // map stream state to errno similar to InputStream
        if (_stream.bad())
            errno = EIO;
        else if (_stream.fail())
            errno = EINVAL;
        else
            errno = EIO;
        throwIOException("write error", errno);
    }

    // update bookkeeping: bytes_written increases by actual bytes written (assume len)
    bytes_written += len;

    // also update _length lazily
    if (_length < bytes_written)
        _length = bytes_written;
}

// st_size: try to compute via seekp/tellp if possible; otherwise fallback
upx_off_t OutputStream::st_size() const {
    // If this OutputStream was created for stdout-like (non-seekable) streams,
    // we cannot compute file size via tellp; return bytes_written as fallback.
    std::ostream &s = const_cast<std::ostream &>(_stream); // tellp non-const API
    std::streampos saved = s.tellp();
    if (saved == static_cast<std::streampos>(-1)) {
        // cannot determine; fallback to bytes_written
        return bytes_written;
    }
    s.seekp(0, std::ios::end);
    std::streampos end = s.tellp();
    if (end == static_cast<std::streampos>(-1)) {
        // restore and fallback
        s.seekp(saved);
        return bytes_written;
    }
    // restore
    s.seekp(saved);
    return static_cast<upx_off_t>(end);
}

// rewrite: do a write but restore bytes_written as in original
void OutputStream::rewrite(SPAN_P(const void) buf, int len) {
    write(buf, len);
    bytes_written -= len; // restore like original
}

// seek using std::ostream positioning (seekp/tellp)
upx_off_t OutputStream::seek(upx_off_t off, int whence) {
    // sanity
    if (!mem_size_valid_bytes(off >= 0 ? off : -off))
        throwIOException("bad seek");

    std::ios::seekdir dir;
    switch (whence) {
    case SEEK_SET:
        dir = std::ios::beg;
        break;
    case SEEK_CUR:
        dir = std::ios::cur;
        break;
    case SEEK_END:
        dir = std::ios::end;
        break;
    default:
        throwIOException("invalid whence", EINVAL);
    }

    _stream.clear(); // clear any flags so seekp may work
    _stream.seekp(static_cast<std::streamoff>(off), dir);

    if (!_stream) {
        // seek failed
        if (_stream.bad())
            errno = EIO;
        else if (_stream.fail())
            errno = EINVAL;
        else
            errno = EIO;
        throwIOException("seek failed", errno);
    }

    std::streampos p = _stream.tellp();
    if (p == static_cast<std::streampos>(-1)) {
        errno = EIO;
        throwIOException("tellp failed", errno);
    }

    upx_off_t pos = static_cast<upx_off_t>(p);

    // mirror your original semantics about bytes_written/_length updates
    switch (whence) {
    case SEEK_SET:
        if (bytes_written < off)
            bytes_written = off;
        _length = bytes_written; // cheap lazy update
        break;
    case SEEK_END:
        _length = bytes_written; // necessary in original
        break;
    default:
        break;
    }

    return pos;
}

// WARNING: fsync() does not exist in some Windows environments.
// This trick works only on UNIX-like systems.
// int OutputFile::read(void *buf, int len) {
//    fsync(_fd);
//    InputFile infile;
//    infile.open(this->getName(), O_RDONLY | O_BINARY);
//    infile.seek(this->tell(), SEEK_SET);
//    return infile.read(buf, len);
//}

// overriding set_extent if you need special behaviour
void OutputStream::set_extent(upx_off_t offset, upx_off_t length) {
    super::set_extent(offset, length);
    bytes_written = 0;
    if (0 == offset && 0xffffffffLL == length) {
        // attempt to read current stream size
        try {
            st.st_size = static_cast<off_t>(st_size());
            _length = st.st_size - offset;
        } catch (...) {
            // leave _length unchanged or set to 0
        }
    }
}

// unset_extent: get actual length by seeking to end and returning it
upx_off_t OutputStream::unset_extent() {
    _stream.clear();
    _stream.seekp(0, std::ios::end);
    if (!_stream) {
        if (_stream.bad())
            errno = EIO;
        else if (_stream.fail())
            errno = EINVAL;
        else
            errno = EIO;
        throwIOException("seek failed", errno);
    }
    std::streampos end = _stream.tellp();
    if (end == static_cast<std::streampos>(-1)) {
        errno = EIO;
        throwIOException("tellp failed", errno);
    }
    _offset = 0;
    _length = static_cast<upx_off_t>(end);
    bytes_written = _length;
    return _length;
}

/*static*/ void OutputStream::dump(const char *name, SPAN_P(const void) buf, int len, int flags) {
    (void) flags; // keep API compatibility if you don't use flags here
    if (!name)
        return;
    std::ofstream ofs;
    ofs.open(name, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) {
        // map error and throw
        throwIOException(name, errno);
    }
    ofs.write(static_cast<const char *>(raw_bytes(buf, len)), len);
    if (!ofs) {
        // writing failed
        throwIOException(name, errno ? errno : EIO);
    }
    ofs.close();
    if (!ofs.good()) {
        throwIOException(name, errno ? errno : EIO);
    }
}

/*************************************************************************
//
**************************************************************************/

TEST_CASE("file") {
    InputFile fi;
    CHECK(!fi.isOpen());
    CHECK(fi.getFd() == -1);
    CHECK(fi.st_size() == 0);
    OutputFile fo;
    CHECK(!fo.isOpen());
    CHECK(fo.getFd() == -1);
    CHECK(fo.getBytesWritten() == 0);
}

/* vim:set ts=4 sw=4 et: */
