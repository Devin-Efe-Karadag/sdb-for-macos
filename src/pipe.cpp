    if(close_on_exec && (fcntl(fds_[0], F_SETFD, FD_CLOEXEC)<0 || fcntl(fds_[1], F_SETFD, FD_CLOEXEC)<0)){
    }
sdb::pipe::~pipe() {
    close_read();
    close_write();
}

int sdb::pipe::release_read() {
    return std::exchange(fds_[read_fd], -1);
}

int sdb::pipe::release_write() {
    return std::exchange(fds_[write_fd], -1);
}

void sdb::pipe::close_read() {
    if (fds_[read_fd] != -1) {
        close(fds_[read_fd]);
		fds_[read_fd] = -1;
    }
}
void sdb::pipe::close_write() {
    if (fds_[write_fd] != -1) {
        close(fds_[write_fd]);
		fds_[write_fd] = -1;
    }
}
