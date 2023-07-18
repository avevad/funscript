.is_object = load_native_sym '_ZN9funscript6stdlib4lang9is_objectERNS_2VM5StackE';
.is_integer = load_native_sym '_ZN9funscript6stdlib4lang10is_integerERNS_2VM5StackE';
.is_string = load_native_sym '_ZN9funscript6stdlib4lang9is_stringERNS_2VM5StackE';
.is_array = load_native_sym '_ZN9funscript6stdlib4lang8is_arrayERNS_2VM5StackE';
.is_boolean = load_native_sym '_ZN9funscript6stdlib4lang10is_booleanERNS_2VM5StackE';
.is_float = load_native_sym '_ZN9funscript6stdlib4lang8is_floatERNS_2VM5StackE';
.is_function = load_native_sym '_ZN9funscript6stdlib4lang11is_functionERNS_2VM5StackE';
.is_pointer = load_native_sym '_ZN9funscript6stdlib4lang10is_pointerERNS_2VM5StackE';

.native = {
    .panic = load_native_sym '_ZN9funscript6stdlib4lang5panicERNS_2VM5StackE';

    .module = load_native_sym '_ZN9funscript6stdlib4lang7module_ERNS_2VM5StackE';
    .submodule = load_native_sym '_ZN9funscript6stdlib4lang9submoduleERNS_2VM5StackE';
    .import = load_native_sym '_ZN9funscript6stdlib4lang7import_ERNS_2VM5StackE';

    .bytes_allocate = load_native_sym '_ZN9funscript6stdlib4lang14bytes_allocateERNS_2VM5StackE';
    .bytes_paste_from_string = load_native_sym '_ZN9funscript6stdlib4lang23bytes_paste_from_stringERNS_2VM5StackE';
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
            .result = no;
            .cur_type = obj.type;
            (
                result = cur_type == new_type;
                cur_type has supertype then cur_type = cur_type.supertype;
            ) until result or not (cur_type has supertype);
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

.panic = .msg: string -> (): native.panic(msg);

.module = .alias: string -> object: native.module(alias);
.submodule = .alias: string -> object: native.submodule(alias);
.import = .obj: object -> (): native.import(obj);

.Bytes = Type.create('Bytes');
Bytes.(
    .allocate = .size: integer -> Bytes: {
        .type = Bytes;

        .data = native.bytes_allocate(size);
        .size = size;

        .get_size = -> size;

        .paste_from_string = (.pos: integer, .str: string, .beg: integer, .end: integer) -> (): (
            pos < 0 or pos > size then panic 'invalid position';
            beg < 0 or end > sizeof str or beg > end then panic 'invalid range';
            native.bytes_paste_from_string(data, pos, str, beg, end);
        );
    };

    .from_string = (.str: string, .beg: integer, .end: integer) -> Bytes: (
        beg < 0 or end > sizeof str or beg > end then panic 'invalid range';
        .bytes = Bytes.allocate(end - beg);
        bytes.paste_from_string(0, str, beg, end);
        bytes
    );
);

.Result_id = 'Result';
.Result = .ok_types: array -> .err_types: array -> (
    .Result = Type.create(Result_id);
    Result.(
        .args = {{*ok_types}, {*err_types}};
        .ok = (*.values: *ok_types) -> Result: {
            .type = Result;

            .unwrap = -> *ok_types: *values;
            .unwrap_or = *.values1: *ok_types -> *ok_types: *values;

            *values
        };
        .err = (*.values: *err_types) -> Result: {
            .type = Result;

            .error = yes;
            .unwrap = -> *ok_types: panic 'unexpected error';
            .unwrap_or = *.values1: *ok_types -> *ok_types: *values1;

            *values
        };
    );
    Result
);

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

    .Bytes = Bytes;
    .Result = Result;
}