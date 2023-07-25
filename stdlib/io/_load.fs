.sys = submodule 'sys';

.SystemError = Type.create('SystemError');
SystemError.(
    .from_posix_call = .name: string -> SystemError: panic 'not implemented';

    sys.get_posix().is_ok() then (
        .posix = sys.get_posix().unwrap();

        from_posix_call = .name: string -> SystemError: {
            .type = SystemError;

            .errno = posix.get_errno();

            .to_string = .fmt -> string: 'SystemError: ' + name + ': ' + posix.strerror(errno);
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

.BufferedWriter = Type.create('BufferedWriter');
BufferedWriter.(
    .bufferize = (.stream, .buf_size: integer) -> BufferedWriter: {
        .type = BufferedWriter;

        .buf = Bytes.allocate(buf_size);
        .cnt = 0;

        .flush = -> Result[][SystemError]: (
            .pos = 0;
            .res = Result[][SystemError].ok();
            pos < cnt and res.is_ok() repeats (
                res = stream.write(buf.span(pos, cnt)).then_map[](.add -> (pos = pos + add));
            );
            cnt = 0;
            res
        );

        .ensure_free = .size: integer -> Result[][SystemError]: (
            (cnt + size <= buf_size) then Result[][SystemError].ok()
            else flush()
        );

        .write_string = .str: string -> Result[][SystemError]: (
            ensure_free(sizeof str).and_then[](-> (
                sizeof str > buf_size then stream.write(Bytes.from_string(str, 0, sizeof str).span(0, sizeof str))
                else (
                    buf.paste_string(cnt, str);
                    cnt = cnt + sizeof str;
                    Result[][SystemError].ok()
                )
            ))
        );
    };
);

.Printer = Type.create('Printer');
Printer.(
    .supertype = Formatter;

    .with_destination = .dest: BufferedWriter -> Printer: (
        .printer = Formatter.create();
        printer.(
            .type = Printer;

            .dest = dest;
            .sep = ' ';
            .end = '\x0a';

            .copy = -> Printer: (
                .printer = Printer.with_destination(dest);
                printer.debug = debug;
                printer.depth_limit = depth_limit;
                printer.sep = sep;
                printer.end = end;
                printer
            );

            .with_sep = .sep: string -> Printer: (
                .printer = copy();
                printer.sep = sep;
                printer
            );

            .with_end = .end: string -> Printer: (
                .printer = copy();
                printer.end = end;
                printer
            );

            .call = (*.values) -> (): (
                values = [*values];
                .pos = 0;
                pos < sizeof values repeats (
                    pos != 0 then dest.write_string(sep).unwrap_or_else(panic_format);
                    dest.write_string(value_to_string(values[pos])).unwrap_or_else(panic_format);
                    pos = pos + 1;
                );
                dest.write_string(end).unwrap_or_else(panic_format);
                dest.flush();
            );
        );
        printer
    );
);

.BufferedReader = Type.create('BufferedReader');
BufferedReader.(
    .bufferize = (.stream, .buf_size: integer) -> BufferedReader: {
        .type = BufferedReader;

        .buf = Bytes.allocate(buf_size);
        .cnt = 0;

        .read_until = .suffix: string -> Result[string][SystemError]: (
            .result = Result[Result[string][SystemError]][].err();
            .res_buf = Bytes.allocate(buf_size);
            .res_cnt, .res_pos = 0, 0;
            not result.is_ok() repeats (
                (
                    cnt == 0 then stream.read(buf.span(0, buf_size))
                    else Result[integer][SystemError].ok(cnt)
                ).then_map[](.cnt_read -> (
                    res_cnt + cnt_read < sizeof res_buf then (
                        .res_buf_new = Bytes.allocate(2 * (sizeof res_buf));
                        res_buf_new.paste(0, res_buf.span(0, res_cnt));
                        res_buf = res_buf_new;
                    );
                    res_buf.paste(res_cnt, buf.span(0, cnt_read));
                    res_cnt = res_cnt + cnt_read;
                    cnt = 0;
                    cnt_read == 0 then result = Result[Result[string][SystemError]][].ok(Result[string][SystemError].ok(res_buf.span(0, res_cnt).to_string()))
                    else res_buf.span(res_pos, res_cnt).find_string(suffix).then_map[](.off -> (
                        buf.paste(0, res_buf.span(res_pos + off, res_cnt));
                        cnt = res_cnt - (res_pos + off);
                        result = Result[Result[string][SystemError]][].ok(Result[string][SystemError].ok(res_buf.span(0, res_pos + off).to_string()));
                    )).else_map[]( -> (res_pos = res_cnt - sizeof suffix + 1));
                )).else_map[](.err -> (
                    result = Result[Result[string][SystemError]][].ok(Result[string][SystemError].err(err));
                ));
            );
            result.unwrap()
        );

        .read_exactly = .count: integer -> Result[string][SystemError]: (
            .result = Result[Result[string][SystemError]][].err();
            .res_buf = Bytes.allocate(buf_size);
            .res_cnt = 0;
            not result.is_ok() repeats (
                (
                    cnt == 0 then stream.read(buf.span(0, buf_size))
                    else Result[integer][SystemError].ok(cnt)
                ).then_map[](.cnt_read -> (
                    res_cnt + cnt_read < sizeof res_buf then (
                        .res_buf_new = Bytes.allocate(2 * (sizeof res_buf));
                        res_buf_new.paste(0, res_buf.span(0, res_cnt));
                        res_buf = res_buf_new;
                    );
                    res_buf.paste(res_cnt, buf.span(0, cnt_read));
                    res_cnt = res_cnt + cnt_read;
                    cnt = 0;
                    cnt_read == 0 then result = Result[Result[string][SystemError]][].ok(Result[string][SystemError].ok(res_buf.span(0, res_cnt).to_string()))
                    else res_cnt >= count then (
                        buf.paste(0, res_buf.span(count, res_cnt));
                        cnt = res_cnt - count;
                        result = Result[Result[string][SystemError]][].ok(Result[string][SystemError].ok(res_buf.span(0, count).to_string()));
                    );
                )).else_map[](.err -> (
                    result = Result[Result[string][SystemError]][].ok(Result[string][SystemError].err(err));
                ));
            );
            result.unwrap()
        );
    }
);

.Scanner = Type.create('Scanner');
Scanner.(
    .with_source = .src: BufferedReader -> Scanner: {
        .type = Scanner;

        .end = no;

        .strings_separated_by = .delim: string -> Flow[string]: Flow[string].from_generator(-> (
            end then Result[string][].err()
            else (
                .str = src.read_until(delim).unwrap_or_else(panic_format);
                sizeof(src.read_exactly(sizeof delim).unwrap_or_else(panic_format)) != sizeof delim then end = yes;
                Result[string][].ok(str)
            )
        ));

        .lines = -> strings_separated_by(sys.eol);
    };
);

exports = {
    .SystemError = SystemError;

    .FD = FD;

    .stdin = stdin;
    .stdout = stdout;
    .stderr = stderr;

    .BufferedWriter = BufferedWriter;
    .Printer = Printer;

    .BufferedReader = BufferedReader;
    .Scanner = Scanner;
};