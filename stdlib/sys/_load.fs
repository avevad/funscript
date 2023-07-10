import submodule 'native';

.native = {
    .fd_write = load_native_sym '_ZN9funscript6stdlib3sys8fd_writeERNS_2VM5StackE';
    .fd_read = load_native_sym '_ZN9funscript6stdlib3sys7fd_readERNS_2VM5StackE';
};

.fd_write = (.fd: integer, .bytes: Bytes, .beg: integer, .end: integer) -> integer: (
    beg < 0 or end > sizeof bytes or beg > end then panic 'invalid range';
    native.fd_write(fd, bytes.data, beg, end)
);

.fd_read = (.fd: integer, .bytes: Bytes, .beg: integer, .end: integer) -> integer: (
    beg < 0 or end > sizeof bytes or beg > end then panic 'invalid range';
    native.fd_read(fd, bytes.data, beg, end)
);

exports = {
    .fd_write = fd_write;
    .fd_read = fd_read;
};