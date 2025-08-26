#ifndef SDB_PIPE_HPP
#define SDB_PIPE_HPP

#include <vector>
#include <cstddef>

namespace sdb {
    class pipe {
    public:
        explicit pipe(bool close_on_exec);
        ~pipe();

        int get_read() const { return fds_[read_fd]; }

        int get_write() const { return fds_[write_fd]; }

        int release_read();
