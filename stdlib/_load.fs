exports = {
    import submodule 'lang';

    .sys = submodule 'sys';
    .io = submodule 'io';

    .print = io.Printer.with_destination(io.BufferedWriter.bufferize(io.stdout, 8192));
};
