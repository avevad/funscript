#ifndef FUNSCRIPT_COMMON_HPP
#define FUNSCRIPT_COMMON_HPP

#include <source_location>
#include <stdexcept>
#include <cstdint>
#include <iostream>
#include <filesystem>
#include <unordered_map>

namespace funscript {

    /**
     * Prints specified error message with source location and aborts current process.
     * @param what Short description of the error.
     * @param where The source location where the error occurred.
     */
    [[noreturn]] static void assertion_failed(const std::string &what,
                                              std::source_location where = std::source_location::current()) {
        std::cerr << std::string(where.file_name()) + ":" + std::to_string(where.line()) + ":" +
                     std::to_string(where.column()) + ": function ‘" + where.function_name() +
                     "’: assertion failed: " + what << std::endl;
        std::abort();
    }

    /**
     * Structure that represents position in code (line and column numbers).
     */
    struct code_pos_t {
        size_t row, col;

        /**
         * Produces a human-readable string which represents this code position.
         * @return The string representation of code position.
         */
        [[nodiscard]] std::string to_string() const {
            return std::to_string(row) + ':' + std::to_string(col);
        }
    };

    /**
     * Structure that represents full location of something in code (where it starts and where it ends).
     */
    struct code_loc_t {
        code_pos_t beg, end;

        /**
         * Produces a human-readable string which represents this code location.
         * @return The string representation of code location.
         */
        [[nodiscard]] std::string to_string() const {
            return beg.to_string() + '-' + end.to_string();
        }
    };

    /**
     * Class of errors which happen during compilation of Funscript code.
     */
    class CompilationError : public std::runtime_error {
    public:
        explicit CompilationError(const std::string &filename, const code_loc_t &loc, const std::string &msg);
    };

    /**
     * Enumeration of all the value types available in Funscript VM.
     */
    enum class Type : uint8_t {
        SEP, // Special type of values which separate value packs from each other (unavailable in Funscript code).
        INT, // Type of integer values.
        OBJ, // Type of freeform objects (basically, just key-value mappings).
        FUN, // Type of function values (lambdas).
        BLN, // Type of boolean values (`yes` or `no`).
        STR, // Type of string values (immutable sequences of bytes).
        ARR, // Type of arrays of any values.
        FLP, // Type of float values (IEEE 754).
        PTR, // Type of allocation pointers (used mostly in native code).
    };

    /**
     * Enumeration of all the instruction types handled by Funscript VM.
     */
    enum class Opcode : uint16_t {
        NOP, // Do nothing
        /**
         * @brief Push literal value (except strings).
         * @param u16 Value type (enum funscript::Type).
         * @param u64 Representation of the value or the pointer to the offset of value representation.
         */
        VAL,
        /**
         * @brief Shorthand for VAL{.u16 = Type::SEP}.
         */
        SEP,
        /**
         * Get indexed value of an object.
         * @param u64 The index of the value.
         */
        IND,
        /**
         * @brief Check if an object contains a field.
         * @param u64 The offset of identifier string.
         */
        HAS,
        /**
         * @brief Get a field of an object.
         * @param u64 The offset of identifier string.
         */
        GET,
        /**
         * @brief Set a field of an object.
         * @param u64 The offset of identifier string.
         */
        SET,
        /**
         * @brief Get a variable in current scope.
         * @param u64 The offset of identifier string.
         */
        VGT,
        /**
         * @brief Set a variable in current scope.
         * @param u64 The offset of identifier string.
         */
        VST,
        /**
         * @brief Change current scope.
         * @param u16 Bool `true` if the action is creating a new scope and `false` if the action is to discard the scope.
         */
        SCP,
        /**
         * @brief Discard values until (and including) the topmost separator.
         * @param u16 Bool `true` to throw an error if any values were actually discarded.
         */
        DIS,
        /**
         * @brief Reverse values until the topmost separator.
         */
        REV,
        /**
         * @brief Execute an operator call.
         * @param u16 Operator type (enum funscript::Operator)
         */
        OPR,
        /**
         * @brief End execution of the function.
         */
        END,
        /**
         * @brief Jump if the value is boolean `no`.
         * @param u64 Bytecode offset of where to jump.
         */
        JNO,
        /**
         * @brief Jump if the value is boolean `yes`.
         * @param u64 Bytecode offset of where to jump.
         */
        JYS,
        /**
         * @brief Jump unconditionally.
         * @param u64 Bytecode offset of where to jump.
         */
        JMP,
        /**
         * @brief Push string value.
         * @param u16 Length of the string.
         * @param u64 Bytecode offset of where the string is stored.
         */
        STR,
        /**
         * @brief Create and push an array.
         */
        ARR,
        /**
         * @brief Create and push an object (from current scope and the topmost value pack).
         */
        OBJ,
        /**
         * @brief Execute assignment call (as in `arr[5] = 1`).
         */
        MOV,
        /**
         * @brief Duplicate value pack at the top of the stack.
         */
        DUP,
        /**
         * @brief Remove the topmost separator.
         */
        REM,
        /**
         * @brief Set location of metadata chunk (basically, the location of DATA chunk).
         * Metadata chunk should start with the name of the file, in which the function is located.
         * @u64 Bytecode offset of metadata chunk.
         */
        MET,
        /**
         * @brief Extract values from the result object.
         * @param u64 Where to jump if the result object is not an error. Zero if error objects should be propagated.
         */
        EXT,
        /**
         * @brief Make a type check.
         *
         * The topmost value pack is treated as types, the next value pack is the values to be checked against this types.
         * @param u16 `true` if excess values should be ignored.
         */
        CHK,
        /**
         * @brief Make a new scope from an object.
         */
        OSC,
        /**
         * @brief Wrap all values until the separator (excluding the separator itself) in a new object and put the object on the stack.
         */
        WRP
    };

    /**
     * Structure that represents single instruction of Funscript VM's bytecode.
     */
    struct Instruction {
        Opcode op; // Opcode (operation code) of the instruction.
        uint16_t u16; // Short argument of the instruction.
        uint32_t meta; // Instruction metadata. It may contain the offset (relative to function's start of metadata) of this instruction's source code position.
        uint64_t u64; // Long argument of the instruction.

        Instruction(Opcode op, uint32_t meta, uint16_t u16, uint64_t u64) : op(op), u16(u16), meta(meta), u64(u64) {}
    };

    /**
     * Enumeration of language operators.
     */
    enum class Operator : uint8_t {
        TIMES,
        DIVIDE,
        PLUS,
        MINUS,
        ASSIGN,
        APPEND,
        DISCARD,
        CALL, // Implicitly inserted during parsing
        LAMBDA,
        INDEX,
        MODULO,
        EQUALS,
        DIFFERS,
        NOT,
        LESS,
        GREATER,
        LESS_EQUAL,
        GREATER_EQUAL,
        THEN,
        ELSE,
        UNTIL,
        REPEATS,
        AND,
        OR,
        IS,
        EXTRACT,
        CHECK,
        HAS,
        BW_AND,
        BW_OR,
        BW_XOR,
        BW_SHL,
        BW_SHR,
        BW_NOT,
        SIZEOF
    };

    /**
     * A structure that holds some static operator metadata.
     */
    struct OperatorMeta {
        int order; // Precedence of an operator (less value - higher precedence)
        bool left; // Whether it is a left-associative operator
    };

    /**
     * @return The mapping from operators to their metadata.
     */
    static const std::unordered_map<Operator, OperatorMeta> &get_operators_meta() {
        static const std::unordered_map<Operator, OperatorMeta> OPERATORS{
                {Operator::INDEX,         {0,  true}},
                {Operator::CALL,          {0,  false /* should not be used */ }},
                {Operator::EXTRACT,       {1,  false}},
                {Operator::NOT,           {2,  false}},
                {Operator::BW_NOT,        {2,  false}},
                {Operator::SIZEOF,        {2,  false}},
                {Operator::TIMES,         {3,  true}},
                {Operator::DIVIDE,        {3,  true}},
                {Operator::MODULO,        {3,  true}},
                {Operator::PLUS,          {4,  true}},
                {Operator::MINUS,         {4,  true}},
                {Operator::EQUALS,        {5,  true}},
                {Operator::DIFFERS,       {5,  true}},
                {Operator::LESS,          {5,  true}},
                {Operator::GREATER,       {5,  true}},
                {Operator::LESS_EQUAL,    {5,  true}},
                {Operator::GREATER_EQUAL, {5,  true}},
                {Operator::BW_SHL,        {6,  true}},
                {Operator::BW_SHR,        {6,  true}},
                {Operator::BW_AND,        {7,  true}},
                {Operator::BW_XOR,        {8,  true}},
                {Operator::BW_OR,         {9,  true}},
                {Operator::HAS,           {10, true}},
                {Operator::IS,            {11, true}},
                {Operator::AND,           {12, true}},
                {Operator::OR,            {13, true}},
                {Operator::CHECK,         {14, true}},
                {Operator::LAMBDA,        {15, false}},
                {Operator::APPEND,        {16, false}},
                {Operator::ASSIGN,        {17, true}},
                {Operator::THEN,          {18, false}},
                {Operator::ELSE,          {19, false}},
                {Operator::UNTIL,         {20, false}},
                {Operator::REPEATS,       {20, false}},
                {Operator::DISCARD,       {21, false}},
        };
        return OPERATORS;
    }

    static const char *CALL_OPERATOR_OVERLOAD_NAME = "call";
    static const char *TIMES_OPERATOR_OVERLOAD_NAME = "times";
    static const char *DIVIDE_OPERATOR_OVERLOAD_NAME = "divide";
    static const char *MODULO_OPERATOR_OVERLOAD_NAME = "modulo";
    static const char *PLUS_OPERATOR_OVERLOAD_NAME = "add";
    static const char *MINUS_OPERATOR_OVERLOAD_NAME = "subtract";
    static const char *EQUALS_OPERATOR_OVERLOAD_NAME = "equals";
    static const char *DIFFERS_OPERATOR_OVERLOAD_NAME = "differs_from";
    static const char *LESS_OPERATOR_OVERLOAD_NAME = "less_than";
    static const char *GREATER_OPERATOR_OVERLOAD_NAME = "greater_than";
    static const char *LESS_EQUAL_OPERATOR_OVERLOAD_NAME = "less_equal_than";
    static const char *BW_SHL_OPERATOR_OVERLOAD_NAME = "bitwise_shift_left_by";
    static const char *BW_SHR_OPERATOR_OVERLOAD_NAME = "bitwise_shift_right_by";
    static const char *BW_AND_OPERATOR_OVERLOAD_NAME = "bitwise_and";
    static const char *BW_XOR_OPERATOR_OVERLOAD_NAME = "bitwise_xor";
    static const char *BW_OR_OPERATOR_OVERLOAD_NAME = "bitwise_or";
    static const char *GREATER_EQUAL_OPERATOR_OVERLOAD_NAME = "greater_equal_than";

    static const char *SIZEOF_OPERATOR_OVERLOAD_NAME = "get_size";

    static const char *TYPE_CHECK_NAME = "check_value";
    static const char *ERR_FLAG_NAME = "error";

    // Aliases for Funscript primitive types

    using fint = int64_t;
    using fflp = double;
    using fbln = bool;

    static double nan() {
        static const double NAN = std::stod("NAN");
        return NAN;
    }

    static double inf() {
        static const double INF = std::stod("INF");
        return INF;
    }

    /**
     * Converts any pointer into hexadecimal representation of its address.
     * @tparam Alloc Allocator type.
     * @param ptr The pointer to convert.
     * @param alloc The allocator for the resulting string.
     * @return String representation of the pointer.
     */
    template<typename Alloc = std::allocator<char>>
    static std::basic_string<char, std::char_traits<char>, Alloc>
    addr_to_string(const void *ptr, const Alloc &alloc = Alloc()) {
        std::basic_ostringstream<char, std::char_traits<char>, Alloc> stream(std::ios_base::out, alloc);
        stream << ptr;
        return stream.str();
    }

    /**
     * Strips version from the full module name.
     * @param name The name of the module.
     * @return The alias of the module (its name without any version).
     */
    static std::string get_module_alias(const std::string &name) {
        return name.substr(name.rfind('-') + 1);
    }

    // Name of the environment variable that holds path to the directory that contains Funscript modules.
    static const char *MODULES_PATH_ENV_VAR = "FS_MODULES_PATH";

    /**
     * Produces the common part of possible module paths (native module, source module).
     * @param name The name of the module.
     * @return The specified module's base path string.
     */
    static std::string get_module_base_path_str(std::string name) {
        const char *modules_path_str = getenv(MODULES_PATH_ENV_VAR);
        for (char &c : name) if (c == '.') c = std::filesystem::path::preferred_separator;
        return (std::filesystem::path(modules_path_str) / name).string();
    }

    /**
     * Produces the path to the library of the native module.
     * @param name The name of the native module.
     * @return The path to native module's library.
     */
    static std::filesystem::path get_native_module_lib_path(const std::string &name) {
        return get_module_base_path_str(name) + ".so";
    }

    /**
     * Produces the path to the loader of the source module.
     * @param name The name of the source module.
     * @return The path to source module's loader.
     */
    static std::filesystem::path get_src_module_loader_path(const std::string &name) {
        auto base = get_module_base_path_str(name);
        auto path_simple = std::filesystem::path(base + ".fs");
        if (std::filesystem::exists(path_simple)) return path_simple;
        static const std::string MODULE_LOADER_FILENAME = "_load.fs";
        auto path = std::filesystem::path(base) / MODULE_LOADER_FILENAME;
        return path;
    }

    static const char *MODULE_EXPORTS_VAR = "exports"; // Name of the variable that holds module's exports.
    static const char *MODULE_RUNNER_VAR = "run"; // Name of the variable that holds module's runner function.

    // Name of the variable that holds native module's symbol lookup function.
    static const char *NATIVE_MODULE_SYMBOL_LOADER_VAR = "load_native_sym";

    // Name of the variable that holds native module's symbol checking function.
    static const char *NATIVE_MODULE_SYMBOL_CHECKER_VAR = "has_native_sym";


    static const std::unordered_map<Opcode, const char *> OPCODES{
            {Opcode::NOP, "NOP"},
            {Opcode::VAL, "VAL"},
            {Opcode::SEP, "SEP"},
            {Opcode::IND, "IND"},
            {Opcode::HAS, "HAS"},
            {Opcode::GET, "GET"},
            {Opcode::SET, "SET"},
            {Opcode::VGT, "VGT"},
            {Opcode::VST, "VST"},
            {Opcode::SCP, "SCP"},
            {Opcode::DIS, "DIS"},
            {Opcode::REV, "REV"},
            {Opcode::OPR, "OPR"},
            {Opcode::END, "END"},
            {Opcode::JNO, "JNO"},
            {Opcode::JYS, "JYS"},
            {Opcode::JMP, "JMP"},
            {Opcode::STR, "STR"},
            {Opcode::ARR, "ARR"},
            {Opcode::OBJ, "OBJ"},
            {Opcode::MOV, "MOV"},
            {Opcode::DUP, "DUP"},
            {Opcode::REM, "REM"},
            {Opcode::MET, "MET"},
            {Opcode::EXT, "EXT"},
            {Opcode::CHK, "CHK"},
            {Opcode::OSC, "OSC"},
            {Opcode::WRP, "WRP"},
    };

    static const char *get_opcode_name(Opcode op) {
        return OPCODES.at(op);
    }

    static void dump_instruction(const Instruction &ins) {
        std::cerr << get_opcode_name(ins.op) << ' ' << std::hex << ins.u16 << ' ' << std::hex << ins.u64 << std::endl;
    }
}

#endif //FUNSCRIPT_COMMON_HPP
