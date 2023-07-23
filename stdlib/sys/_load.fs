import submodule 'native';

.posix = 0;

.get_posix = -> Result[object][]: (
    posix is 0 and has_native_sym '_ZN9funscript6stdlib3sys15posix_get_errnoERNS_2VM5StackE' then (
        .native = {
            .get_errno = load_native_sym '_ZN9funscript6stdlib3sys15posix_get_errnoERNS_2VM5StackE';
            .write = load_native_sym '_ZN9funscript6stdlib3sys11posix_writeERNS_2VM5StackE';
            .read = load_native_sym '_ZN9funscript6stdlib3sys10posix_readERNS_2VM5StackE';
            .strerror = load_native_sym '_ZN9funscript6stdlib3sys14posix_strerrorERNS_2VM5StackE';
        };
        posix = {
            .get_errno = -> integer: native.get_errno();
            .write = (.fd: integer, .bytes: Bytes, .beg: integer, .end: integer) -> integer: (
                beg < 0 or end > sizeof bytes or beg > end then panic 'invalid range';
                native.write(fd, bytes.data, beg, end)
            );
            .read = (.fd: integer, .bytes: Bytes, .beg: integer, .end: integer) -> integer: (
                beg < 0 or end > sizeof bytes or beg > end then panic 'invalid range';
                native.read(fd, bytes.data, beg, end)
            );
            .strerror = .err_num: integer -> string: native.strerror(err_num);
        };
    );
    posix is 0 then Result[object][].err()
    else Result[object][].ok(posix)
);

exports = {
    .get_posix = get_posix;

    .eol = '\x0a';
};