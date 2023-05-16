#ifndef FUNSCRIPT_COMMON_H
#define FUNSCRIPT_COMMON_H

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

    enum class Type : uint8_t {
        NUL, SEP, INT, OBJ, FUN, BLN, STR, ERR, ARR
    };

    /**
     * Enumeration of all the instruction types handled by Funscript VM
     */
    enum class Opcode : uint16_t {
        NOP, // Do nothing
        /**
         * @brief Push literal value (except strings)
         * @param u16 Value type (enum funscript::Type).
         * @param u64 Representation of the value or the pointer to the offset of value representation.
         */
        VAL,
        /**
         * @brief Shorthand for VAL(u16 = Type::SEP)
         */
        SEP,
        /**
         * @brief Get a field of an object or the scope.
         * @param u16 Bool `true` if the alternative lookup method should be used.
         * @param u64 The offset of identifier string.
         */
        GET,
        /**
         * @brief Set a field of an object or the scope.
         * @param u16 Bool `true` if the alternative lookup method should be used.
         * @param u64 The offset of identifier string.
         */
        SET,
        /**
         * @brief Change current scope.
         * @param u16 Bool `true` if the action is creating a new scope and `false` if the action is to discard the scope.
         */
        SCP,
        /**
         * @brief Discard values until (and including) the topmost separator.
         */
        DIS,
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
         * @brief Execute assignment call (as in `arr[5] = 1`)
         */
        MOV
    };

    struct Instruction {
        Opcode op;
        uint16_t u16;
        uint32_t reserved;
        uint64_t u64;

        Instruction(Opcode op, uint16_t u16 = 0, uint64_t u64 = 0) : op(op), u16(u16), reserved(0), u64(u64) {}
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
        DO
    };
}

#endif //FUNSCRIPT_COMMON_H
