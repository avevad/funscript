.io = submodule 'io';
.coroutines = submodule 'coroutines';

.REPL = Type.create('REPL');
REPL.create = (.reader: io.BufferedReader, .writer: io.BufferedWriter) -> {
    .type = REPL;

    .input = io.Scanner.with_source(reader);
    .print = io.Printer.with_destination(writer);

    .repl_globals = { import lang };

    .request_expr = -> Result[string][]: (
        print.with_end('    ')();
        input.lines().get_one()
    );

    .eval_expr = .expr: string -> Result[array][array, string]: (
        .results = [];
        .expression_evaluator = -> (): (
            .fun = compile_expr(expr, '<stdin>', '<expr>', repl_globals);
            results = [fun()];
        );
        .stack = coroutines.Stack.create(expression_evaluator);
        stack.push_sep();
        stack.execute();
        stack.is_panicked() then Result[array][array, string].err(stack.generate_stack_trace(), stack.top())
        else Result[array][array, string].ok(results)
    );

    .print_results = .results: array -> (): (
        sizeof results != 0 then (
            print.with_end('# = ')();
            print.with_sep(', ').with_debug()(*results);
        );
    );

    .print_panic = (.stacktrace: array, .message: string) -> (): (
        Flow[string].from_array(stacktrace).for_each(.frame -> print('# !', frame));
        print('# !', message);
    );
};

run = -> (
    .repl = REPL.create(
        io.BufferedReader.bufferize(io.stdin, 8192),
        io.BufferedWriter.bufferize(io.stdout, 8192)
    );
    repl.print('# Funscript REPL');
    Flow[string]
        .from_generator(repl.request_expr)
        .map[Result[array][array, string]](.expr -> repl.eval_expr(expr))
        .for_each(.res -> res
            .then_map[](repl.print_results)
            .else_map[](repl.print_panic)
        );
);

exports = {
    .REPL = REPL;
}
