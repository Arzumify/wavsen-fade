module;

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

module wavsen.audio.byte_stream;

import rstd.cppstd;
import rstd;

namespace wavsen::audio {

using rstd::io::SeekFrom;
using rstd::io::error::Error;

auto PosixFile::open(const std::string& path)
    -> rstd::io::Result<std::unique_ptr<PosixFile>>
{
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return rstd::Err(Error::from_raw_os_error(errno));
    }
    return rstd::Ok(std::unique_ptr<PosixFile>(new PosixFile(fd)));
}

PosixFile::~PosixFile() {
    if (fd_ >= 0) ::close(fd_);
}

auto PosixFile::read(rstd::u8* buf, rstd::usize len)
    -> rstd::io::Result<rstd::usize>
{
    while (true) {
        auto n = ::read(fd_, buf, len);
        if (n >= 0) return rstd::Ok(static_cast<rstd::usize>(n));
        if (errno == EINTR) continue;
        return rstd::Err(Error::from_raw_os_error(errno));
    }
}

auto PosixFile::seek(SeekFrom pos)
    -> rstd::io::Result<rstd::u64>
{
    int whence;
    switch (pos.which) {
    case SeekFrom::Which::Start:   whence = SEEK_SET; break;
    case SeekFrom::Which::Current: whence = SEEK_CUR; break;
    case SeekFrom::Which::End:     whence = SEEK_END; break;
    }
    auto off = ::lseek(fd_, static_cast<off_t>(pos.offset), whence);
    if (off < 0) {
        return rstd::Err(Error::from_raw_os_error(errno));
    }
    return rstd::Ok(static_cast<rstd::u64>(off));
}

} // namespace wavsen::audio
