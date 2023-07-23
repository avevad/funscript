.is_object = load_native_sym '_ZN9funscript6stdlib4lang9is_objectERNS_2VM5StackE';
.is_integer = load_native_sym '_ZN9funscript6stdlib4lang10is_integerERNS_2VM5StackE';
.is_string = load_native_sym '_ZN9funscript6stdlib4lang9is_stringERNS_2VM5StackE';
.is_array = load_native_sym '_ZN9funscript6stdlib4lang8is_arrayERNS_2VM5StackE';
.is_boolean = load_native_sym '_ZN9funscript6stdlib4lang10is_booleanERNS_2VM5StackE';
.is_float = load_native_sym '_ZN9funscript6stdlib4lang8is_floatERNS_2VM5StackE';
.is_function = load_native_sym '_ZN9funscript6stdlib4lang11is_functionERNS_2VM5StackE';
.is_pointer = load_native_sym '_ZN9funscript6stdlib4lang10is_pointerERNS_2VM5StackE';

.int_to_str = load_native_sym '_ZN9funscript6stdlib4lang10int_to_strERNS_2VM5StackE';
.flp_to_str = load_native_sym '_ZN9funscript6stdlib4lang10flp_to_strERNS_2VM5StackE';
.fun_to_str = load_native_sym '_ZN9funscript6stdlib4lang10fun_to_strERNS_2VM5StackE';
.ptr_to_str = load_native_sym '_ZN9funscript6stdlib4lang10ptr_to_strERNS_2VM5StackE';
.str_to_str = load_native_sym '_ZN9funscript6stdlib4lang10str_to_strERNS_2VM5StackE';

.native = {
    .panic = load_native_sym '_ZN9funscript6stdlib4lang5panicERNS_2VM5StackE';

    .module = load_native_sym '_ZN9funscript6stdlib4lang7module_ERNS_2VM5StackE';
    .submodule = load_native_sym '_ZN9funscript6stdlib4lang9submoduleERNS_2VM5StackE';
    .import = load_native_sym '_ZN9funscript6stdlib4lang7import_ERNS_2VM5StackE';

    .bytes_allocate = load_native_sym '_ZN9funscript6stdlib4lang14bytes_allocateERNS_2VM5StackE';
    .bytes_paste_from_string = load_native_sym '_ZN9funscript6stdlib4lang23bytes_paste_from_stringERNS_2VM5StackE';
    .bytes_paste_from_bytes = load_native_sym '_ZN9funscript6stdlib4lang22bytes_paste_from_bytesERNS_2VM5StackE';
    .bytes_find_string = load_native_sym '_ZN9funscript6stdlib4lang17bytes_find_stringERNS_2VM5StackE';
    .bytes_to_string = load_native_sym '_ZN9funscript6stdlib4lang15bytes_to_stringERNS_2VM5StackE';

    .concat = load_native_sym '_ZN9funscript6stdlib4lang6concatERNS_2VM5StackE';
};

# Temporary placeholders
.Type = 0;
.object = { .check_value = .obj -> () };
.boolean = { .check_value = .bln -> () };

.create_type = .id -> (
    .new_type = {
        .args = {};
        .id = id;
        .type = Type;
        .get_dbg_str = -> string: id;
        .check_value = .obj: object -> (): (
            not (obj has type) then panic(get_dbg_str() + ' expected');
            .result, .end = no, no;
            .cur_type = obj.type;
            (
                result = cur_type == new_type;
                cur_type has supertype then cur_type = cur_type.supertype
                else end = yes;
            ) until result or end;
            not result then panic(get_dbg_str() + ' expected');
        );
        .equals = .type -> boolean: (
            not (type has type) or not (type.type is Type) then panic 'Type expected';
            id is type.id and args == type.args
        );
    };
    new_type
);

Type = create_type 'Type';
Type.type = Type;
Type: Type = Type; # Type self-check
Type.create = create_type;

.integer = Type.create('integer');
integer.check_value = .int -> (not is_integer(int) then panic 'integer expected');

.object = Type.create('object');
object.check_value = .obj -> (not is_object(obj) then panic 'object expected');

.string = Type.create('string');
string.check_value = .str -> (not is_string(str) then panic 'string expected');

.array = Type.create('array');
array.check_value = .arr -> (not is_array(arr) then panic 'array expected');

.boolean = Type.create('boolean');
boolean.check_value = .bln -> (not is_boolean(bln) then panic 'boolean expected');

.float = Type.create('float');
float.check_value = .flp -> (not is_float(flp) then panic 'float expected');

.function = Type.create('function');
function.check_value = .fun -> (not is_function(fun) then panic 'function expected');

.pointer = Type.create('pointer');
pointer.check_value = .ptr -> (not is_pointer(ptr) then panic 'pointer expected');

# Redefinition with typechecking enabled
Type.create = .name: string -> Type: create_type(name);

.typeof = .val -> Type: (
    (is_object(val) then (
        val has type and val.type has type and val.type.type == Type then val.type
        else object
    )),
    (is_integer(val) then integer),
    (is_string(val) then string),
    (is_array(val) then array),
    (is_boolean(val) then boolean),
    (is_float(val) then float),
    (is_function(val) then function),
    (is_pointer(val) then pointer),
);

.panic = .msg: string -> (): native.panic(msg);

.module = .alias: string -> object: native.module(alias);
.submodule = .alias: string -> object: native.submodule(alias);
.import = .obj: object -> (): native.import(obj);

.Bytes = Type.create('Bytes');
.ByteSpan = Type.create('ByteSpan');
Bytes.(
    .allocate = .size: integer -> Bytes: (
        .bytes = {
            .type = Bytes;

            .data = native.bytes_allocate(size);

            .get_size = -> size;

            .paste_from_string = (.pos: integer, .str: string, .beg: integer, .end: integer) -> (): (
                beg < 0 or end > sizeof str or beg > end then panic 'invalid range';
                pos < 0 or pos + (end - beg) > size then panic 'invalid position';
                native.bytes_paste_from_string(data, pos, str, beg, end);
            );

            .paste_from_bytes = (.pos: integer, .bytes: Bytes, .beg: integer, .end: integer) -> (): (
                beg < 0 or end > sizeof bytes or beg > end then panic 'invalid range';
                pos < 0 or pos + (end - beg) > size then panic 'invalid position';
                native.bytes_paste_from_bytes(data, pos, bytes.data, beg, end);
            );

            .paste_string = (.pos: integer, .str: string) -> (): (
                paste_from_string(pos, str, 0, sizeof str);
            );

            .paste = (.pos: integer, .span: ByteSpan) -> (): paste_from_bytes(pos, span.get_bytes(), span.get_beg(), span.get_end());

            .find_string = (.beg: integer, .end: integer, .str: string) -> Result[integer][]: (
                beg < 0 or end > sizeof bytes or beg > end then panic 'invalid range';
                .pos = native.bytes_find_string(data, beg, end, str);
                pos == end then Result[integer][].err() else Result[integer][].ok(pos)
            );

            .to_string = (.beg: integer, .end: integer) -> string: (
                beg < 0 or end > sizeof bytes or beg > end then panic 'invalid range';
                native.bytes_to_string(data, beg, end)
            );

            .span = (.beg: integer, .end: integer) -> ByteSpan: ByteSpan.from_bytes(bytes, beg, end);
        };
        bytes
    );

    .from_string = (.str: string, .beg: integer, .end: integer) -> Bytes: (
        beg < 0 or end > sizeof str or beg > end then panic 'invalid range';
        .bytes = Bytes.allocate(end - beg);
        bytes.paste_from_string(0, str, beg, end);
        bytes
    );
);
ByteSpan.(
    .from_bytes = (.bytes: Bytes, .beg: integer, .end: integer) -> ByteSpan: {
        beg < 0 or end > sizeof bytes or beg > end then panic 'invalid range';
        .type = ByteSpan;

        .get_beg = -> beg;
        .get_end = -> end;
        .get_size = -> end - beg;

        .get_bytes = -> bytes;

        .find_string = .str: string -> Result[integer][]: bytes.find_string(beg, end, str).then_map[integer](.pos -> pos - beg);

        .to_string = -> string: bytes.to_string(beg, end);
    };
);

.Result_id = 'Result';
.Result = .ok_types: array -> .err_types: array -> (
    .ThisResult = Type.create(Result_id);
    ThisResult.(
        .args = {{*ok_types}, {*err_types}};
        .ok = (*.values: *ok_types) -> ThisResult: {
            .type = ThisResult;

            .unwrap = -> *ok_types: *values;
            .unwrap_or_else = .err_fun -> *values;
            .unwrap_or_default = (*.values1) -> *values;

            .and_then = .ok_types1: array -> .ok_fun -> Result(ok_types1)(err_types): ok_fun(*values);
            .then_map = .ok_types1: array -> .ok_fun -> Result(ok_types1)(err_types).ok(ok_fun(*values));

            .or_else = .err_types1: array -> .err_fun -> Result(ok_types)(err_types1).ok(*values);
            .else_map = .err_types1: array -> .err_fun -> Result(ok_types)(err_types1).ok(*values);

            .is_ok = -> boolean: yes;
            .is_err = -> boolean: no;

            *values
        };
        .err = (*.values: *err_types) -> ThisResult: {
            .type = ThisResult;
            .error = yes;

            .unwrap = -> *ok_types: panic 'attempt to unwrap error value(s)';
            .unwrap_or_else = .err_fun -> err_fun(*values);
            .unwrap_or_default = (*.values1) -> *values1;

            .and_then = .ok_types1: array -> .ok_fun -> Result(ok_types1)(err_types).err(*values);
            .then_map = .ok_types1: array -> .ok_fun -> Result(ok_types1)(err_types).err(*values);

            .or_else = .err_types1: array -> .err_fun -> Result(ok_types)(err_types1): err_fun(*values);
            .else_map = .err_types1: array -> .err_fun -> Result(ok_types)(err_types1).err(err_fun(*values));

            .is_ok = -> boolean: no;
            .is_err = -> boolean: yes;

            *values
        };
    );
    ThisResult
);

.Formatter = Type.create('Formatter');
.values_to_string = (.fmt: Formatter, .values: array) -> string: (
    .pos = 0;
    native.concat(pos < sizeof values repeats (
        (pos != 0 then ', '),
        fmt.value_to_string(values[pos]),
        (pos = pos + 1)
    ))
);
.object_to_string = (.fmt: Formatter, .obj: object) -> string: (
    '{#[...]#; ' + values_to_string(fmt, [*obj]) + '}'
);
Formatter.create = -> Formatter: {
    .type = Formatter;

    .debug = no;
    .depth_limit = 8;

    .copy = -> Formatter: (
        .fmt = Formatter.create();
        fmt.debug = debug;
        fmt.depth_limit = depth_limit;
        fmt
    );

    .with_debug = () -> Formatter: (
        .fmt = copy();
        fmt.debug = yes;
        fmt
    );

    .without_debug = () -> Formatter: (
        .fmt = copy();
        fmt.debug = no;
        fmt
    );

    .with_depth_limit = .new_limit: integer -> Formatter: (
        new_limit < 0 then new_limit = depth_limit + new_limit;
        .fmt = copy();
        fmt.depth_limit = new_limit;
        fmt
    );

    .value_to_string = .val -> string: (
        depth_limit > 0 then (
            (is_integer(val) then int_to_str(val)),
            (is_object(val) then (
                debug then (
                    val has to_debug_string then val.to_debug_string(copy())
                    else typeof(val).id + ' ' + object_to_string(with_debug().with_depth_limit(-1), val)
                ) else (
                    val has to_string then val.to_string(copy())
                    else typeof(val).id + ' ' + object_to_string(with_debug().with_depth_limit(-1), val)
                )
            )),
            (is_boolean(val) then (val then 'yes' else 'no')),
            (is_function(val) then fun_to_str(val)),
            (is_string(val) then (debug then str_to_str(val) else val)),
            (is_array(val) then '[' + values_to_string(with_debug().with_depth_limit(-1), val) + ']'),
            (is_float(val) then flp_to_str(val)),
            (is_pointer(val) then ptr_to_str(val))
        ) else '#[...]#'
    );
};

exports = {
    .panic = panic;

    .module = module;
    .submodule = submodule;
    .import = import;

    .Type = Type;

    .integer = integer;
    .object = object;
    .string = string;
    .array = array;
    .float = float;
    .boolean = boolean;
    .function = function;
    .pointer = pointer;

    .typeof = typeof;

    .Bytes = Bytes;
    .ByteSpan = ByteSpan;

    .Result = Result;

    .Formatter = Formatter;
}