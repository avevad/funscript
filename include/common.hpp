#ifndef FUNSCRIPT_COMMON_HPP
#define FUNSCRIPT_COMMON_HPP

#include <source_location>
#include <stdexcept>
#include <cstdint>
#include <iostream>

namespace funscript {

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

        [[nodiscard]] std::string to_string() const {
            return std::to_string(row) + ':' + std::to_string(col);
        }
    };

    /**
     * Structure that represents full location of something in code (where it starts and where it ends).
     */
    struct code_loc_t {
        code_pos_t beg, end;

        [[nodiscard]] std::string to_string() const {
            return beg.to_string() + '-' + end.to_string();
        }
    };

    /**
     * Structure that represents the execution metadata (filename, line, column)
     */
    struct code_met_t {
        const char *filename;
        code_pos_t position;
    };

    enum class Type : uint8_t {
        NUL, SEP, INT, OBJ, FUN, BLN, STR, ERR, ARR, FLP
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
         * @brief Shorthand for VAL(u16 = Type::SEP).
         */
        SEP,
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
    };

    struct Instruction {
        Opcode op;
        uint16_t u16;
        uint32_t meta;
        uint64_t u64;

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
        DO,
        AND,
        OR
    };

    using fint = int64_t;
    using fflp = double;

    static double nan() {
        static const double NAN = std::stod("NAN");
        return NAN;
    }

    static double inf() {
        static const double INF = std::stod("INF");
        return INF;
    }

    template<typename Alloc = std::allocator<char>>
    static std::basic_string<char, std::char_traits<char>, Alloc>
    addr_to_string(const void *ptr, const Alloc &alloc = Alloc()) {
        std::basic_ostringstream<char, std::char_traits<char>, Alloc> stream(std::ios_base::out, alloc);
        stream << ptr;
        return stream.str();
    }

}

#endif //FUNSCRIPT_COMMON_HPP
