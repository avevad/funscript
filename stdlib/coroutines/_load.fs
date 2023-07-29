import submodule 'native';

.native = {
    .stack_create = load_native_sym '_ZN9funscript6stdlib10coroutines12stack_createERNS_2VM5StackE';
    .stack_execute = load_native_sym '_ZN9funscript6stdlib10coroutines13stack_executeERNS_2VM5StackE';
    .stack_generate_stack_trace = load_native_sym '_ZN9funscript6stdlib10coroutines26stack_generate_stack_traceERNS_2VM5StackE';
    .stack_is_panicked = load_native_sym '_ZN9funscript6stdlib10coroutines17stack_is_panickedERNS_2VM5StackE';
    .stack_top = load_native_sym '_ZN9funscript6stdlib10coroutines9stack_topERNS_2VM5StackE';
    .stack_push_sep = load_native_sym '_ZN9funscript6stdlib10coroutines14stack_push_sepERNS_2VM5StackE';
};

.Stack = Type.create('Stack');
Stack.(
    .create = .start_fun: function -> Stack: {
        .type = Stack;

        .stack: pointer = native.stack_create(start_fun);

        .execute = -> (): native.stack_execute(stack);
        .generate_stack_trace = -> array: native.stack_generate_stack_trace(stack);
        .is_panicked = -> boolean: native.stack_is_panicked(stack);
        .top = -> native.stack_top(stack);
        .push_sep = -> (): native.stack_push_sep(stack);
    };
);

exports = {
    .Stack = Stack;
};