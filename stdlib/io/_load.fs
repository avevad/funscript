.sys = submodule 'sys';

.SystemError = Type.create('SystemError');
SystemError.(
    .from_posix_call = .name: string -> SystemError: panic 'not implemented';

    sys.get_posix().is_ok() then (
        .posix = sys.get_posix().unwrap();

        from_posix_call = .name: string -> SystemError: {
            .type = SystemError;

            .errno = posix.get_errno();

            .display = -> string: 'SystemError: ' + name + ': ' + posix.strerror(errno);
        };
    );
);

.FD = Type.create('FD');
FD.(
    .call = .num: integer -> FD: {
        .type = FD;

        .write = .buf: ByteSpan -> Result[integer][SystemError]: panic 'not implemented';
        .read = .buf: ByteSpan -> Result[integer][SystemError]: panic 'not implemented';

        sys.get_posix().is_ok() then (
            .posix = sys.get_posix().unwrap();

            write = .buf: ByteSpan -> Result[integer][SystemError]: (
                .cnt = posix.write(num, buf.get_bytes(), buf.get_beg(), buf.get_end());
                cnt >= 0 then Result[integer][SystemError].ok(cnt)
                else Result[integer][SystemError].err(SystemError.from_posix_call('write'))
            );
            read = .buf: ByteSpan -> Result[integer][SystemError]: (
                .cnt = posix.read(num, buf.get_bytes(), buf.get_beg(), buf.get_end());
                cnt >= 0 then Result[integer][SystemError].ok(cnt)
                else Result[integer][SystemError].err(SystemError.from_posix_call('read'))
            );
        );
    };
);

.stdin = FD(0);
.stdout = FD(1);
.stderr = FD(2);

exports = {
    .SystemError = SystemError;

    .FD = FD;

    .stdin = stdin;
    .stdout = stdout;
    .stderr = stderr;
};