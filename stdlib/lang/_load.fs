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
};

# Temporary placeholders
.Type = 0;
.object = { .check_value = .obj -> () };

.create_type = .name -> (
    .new_type = {
        .type = Type;
        .check_value = .obj: object -> (): (
            not (obj has type) then panic 'object does not have a type';
            .result = no;
            .cur_type = obj.type;
            (
                result = cur_type is new_type;
                cur_type has supertype then cur_type = cur_type.supertype;
            ) until result or not (cur_type has supertype);
        );
        .create_base = -> {
            .type = new_type;
            .get_debug_str = -> string: name + '{ #[...]# }';
        };
        .get_debug_str = -> string: name;
    };
    new_type
);

Type = create_type 'Type';
Type.type = Type;
Type: Type = Type; # Type self-check
Type.create = create_type;

.integer = Type.create('integer');
integer.(
    .check_value = .int -> (
        not is_integer(int) then panic 'not an integer';
    );
    .create = -> 0;
);

.object = Type.create('object');
object.(
    .check_value = .obj -> (
        not is_object(obj) then panic 'not an object';
    );
    .create = -> {};
);

.string = Type.create('string');
string.(
    .check_value = .str -> (
        not is_string(str) then panic 'not a string';
    );
    .create = -> '';
);

.array = Type.create('array');
array.(
    .check_value = .arr -> (
        not is_array(arr) then panic 'not an array';
    );
    .create = -> [];
);

.boolean = Type.create('boolean');
boolean.(
    .check_value = .bln -> (
        not is_boolean(bln) then panic 'not a boolean';
    );
    .create = -> false;
);

.float = Type.create('float');
float.(
    .check_value = .flp -> (
        not is_float(flp) then panic 'not a float';
    );
    .create = -> 0.0;
);

.function = Type.create('function');
function.(
    .check_value = .fun -> (
        not is_function(fun) then panic 'not a function';
    );
    .create = -> (->);
);

.pointer = Type.create('pointer');
pointer.(
    .check_value = .ptr -> (
        not is_pointer(ptr) then panic 'not a pointer';
    );
    .create = -> panic 'cannot create a pointer';
);

# Redefinition with typechecking enabled
Type.create = .name: string -> Type: create_type(name);

.panic = .msg: string -> (): native.panic(msg);

.module = .alias: string -> object: native.module(alias);
.submodule = .alias: string -> object: native.submodule(alias);
.import = .obj: object -> (): native.import(obj);

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
}