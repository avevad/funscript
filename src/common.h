//
// Created by avevad on 11/20/21.
//

#ifndef FUNSCRIPT_COMMON_H
#define FUNSCRIPT_COMMON_H

#include <utility>
#include <variant>
#include <string>
#include <map>
#include <stdexcept>
#include <experimental/source_location>
#include <set>
#include <cstring>
#include <sstream>

namespace funscript {

#define FS_VERSION_MAJOR 0
#define FS_VERSION_MINOR 1
#define FS_STR(S) #S
#define FS_TO_STR(X) FS_STR(X)

    const static constexpr size_t VERSION_MAJOR = FS_VERSION_MAJOR;
    const static constexpr size_t VERSION_MINOR = FS_VERSION_MINOR;
    const static constexpr char *VERSION = "Funscript " FS_TO_STR(FS_VERSION_MAJOR) "." FS_TO_STR(FS_VERSION_MINOR);

#undef M_VERSION_MAJOR
#undef M_VERSION_MINOR
#undef M_STR
#undef M_TO_STR

    enum class Operator : char {
        TIMES,
        DIVIDE,
        PLUS,
        MINUS,
        ASSIGN,
        APPEND,
        DISCARD,
        CALL,
        LAMBDA,
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

    enum class Bracket {
        PLAIN, CURLY
    };

    class Allocator {
    public:
        virtual void *allocate(size_t size) = 0;
        virtual void free(void *ptr) = 0;
    };

    class DefaultAllocator : public Allocator {
    public:
        void *allocate(size_t size) override { return std::malloc(size); }

        void free(void *ptr) override { std::free(ptr); }
    };

    template<typename T>
    class AllocatorWrapper {
    public:
        Allocator *alloc;
        typedef T value_type;

        explicit AllocatorWrapper(Allocator *alloc) : alloc(alloc) {}

        AllocatorWrapper(const AllocatorWrapper &old) : alloc(old.alloc) {}

        template<typename E>
        explicit AllocatorWrapper(const AllocatorWrapper<E> &old) : alloc(old.alloc) {}

        [[nodiscard]] T *allocate(size_t n) { return reinterpret_cast<T *>(alloc->allocate(sizeof(T) * n)); }

        void deallocate(T *ptr, size_t n) noexcept { alloc->free(ptr); }

        AllocatorWrapper &operator=(const AllocatorWrapper &old) {
            if (&old != this) alloc = old.alloc;
            return *this;
        }

        bool operator==(const AllocatorWrapper &o) const {
            return o.alloc == alloc;
        }

        bool operator!=(const AllocatorWrapper &o) const {
            return o.alloc == alloc;
        }
    };

    using fchar = char32_t;

    using fstring = std::basic_string<fchar, std::char_traits<fchar>, AllocatorWrapper<fchar>>;
    template<typename K, typename V>
    using fmap = std::map<K, V, std::less<K>, AllocatorWrapper<std::pair<const K, V>>>;

    template<typename K>
    using fset = std::set<K, std::less<>, AllocatorWrapper<K>>;

    struct Token {
        enum Type {
            UNKNOWN = 0,
            ID,
            INTEGER,
            OPERATOR,
            LEFT_BRACKET,
            RIGHT_BRACKET,
            NUL,
            VOID, // special
            INDEX,
            BOOLEAN
        };
        using Data = std::variant<Operator, Bracket, int64_t, fstring, bool>;
        Type type;
        Data data;
    };

    struct OperatorMeta {
        int order;
        bool left;
    };

    static const std::map<Operator, OperatorMeta> OPERATORS{
            {Operator::CALL,          {0,  false}}, // special
            {Operator::NOT,           {1,  false}},
            {Operator::TIMES,         {3,  true}},
            {Operator::DIVIDE,        {4,  true}},
            {Operator::MODULO,        {4,  true}},
            {Operator::PLUS,          {4,  true}},
            {Operator::MINUS,         {4,  true}},
            {Operator::EQUALS,        {6,  true}},
            {Operator::DIFFERS,       {6,  true}},
            {Operator::LESS,          {6,  true}},
            {Operator::GREATER,       {6,  true}},
            {Operator::LESS_EQUAL,    {6,  true}},
            {Operator::GREATER_EQUAL, {6,  true}},
            {Operator::LAMBDA,        {7,  false}}, // special
            {Operator::APPEND,        {9,  false}}, // special
            {Operator::ASSIGN,        {10, true}},  // special
            {Operator::THEN,          {11, false}}, // special
            {Operator::ELSE,          {12, false}}, // special
            {Operator::UNTIL,         {13, false}}, // special
            {Operator::DO,            {13, false}}, // special
            {Operator::DISCARD,       {14, false}}, // special
    };

    enum class Opcode : uint8_t {
        NOP, // do nothing
        SEP, // push a separator
        NUL, // push NUL
        INT, // push an integer
        OP,  // make an operator call
        DIS, // discard values until the separator
        END, // finish execution
        NS,  // push new scope
        DS,  // pop current scope
        FUN, // create function
        OBJ, // push current scope variables map
        VST, // move value to the variable
        VGT, // push value of the variable
        SET, // move value to object's field
        GET, // push
        PBY, // push boolean "yes"
        PBN, // push boolean "no",
        JN,  // jump if "no"
        JMP, // jump unconditionally
        POP, // pop one value from stack
    };

    struct Instruction {
        Opcode op;
        uint8_t u8;
        uint16_t u16;
    };

    class CompilationError : public std::runtime_error {
    public:
        explicit CompilationError(const std::string &msg) : std::runtime_error(msg) {}
    };

    class AssertionError : public std::runtime_error {
    public:
        explicit AssertionError(const std::string &msg) : std::runtime_error(msg) {}
    };

    [[noreturn]] static void assert_failed(const std::string &what,
                                           std::experimental::source_location where = std::experimental::source_location::current()) {
        throw AssertionError(std::string(where.file_name()) + ":" + std::to_string(where.line()) + ":" +
                             std::to_string(where.column()) + ": function ‘" + where.function_name() +
                             "’: assertion failed: " + what);
    }

#define FS_ASSERT(A) do {if(!(A)) assert_failed(#A); } while(0)

    static fstring ascii2fstring(const std::string &str, AllocatorWrapper<fchar> alloc) {
        fstring result(alloc);
        result.reserve(str.size());
        for (char c: str) result += fchar(c);
        return result;
    }

    static char fchar2ascii(fchar c) {
        return char(c);
    }

    static std::string fstring2ascii(const fstring &str) {
        std::string result;
        result.reserve(str.size());
        for (fchar c: str) result += fchar2ascii(c);
        return result;
    }
}

#endif //FUNSCRIPT_COMMON_H
