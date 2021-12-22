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
#include <source_location>
#include <set>

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
        NOT
    };

    enum class Bracket {
        PLAIN, CURLY
    };

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
            INDEX
        };
        using Data = std::variant<Operator, Bracket, int64_t, std::wstring>;
        Type type;
        Data data;
    };

    static const std::map<std::wstring, Operator> OPERATOR_KEYWORDS{
            {L"*",  Operator::TIMES},
            {L"/",  Operator::DIVIDE},
            {L"+",  Operator::PLUS},
            {L"-",  Operator::MINUS},
            {L"=",  Operator::ASSIGN},
            {L",",  Operator::APPEND},
            {L";",  Operator::DISCARD},
            {L":",  Operator::LAMBDA},
            {L"%",  Operator::MODULO},
            {L"==", Operator::EQUALS},
            {L"!=", Operator::DIFFERS},
            {L"!",  Operator::NOT},
    };

    static const std::map<std::wstring, Bracket> LEFT_BRACKET_KEYWORDS{
            {L"(", Bracket::PLAIN},
            {L"{", Bracket::CURLY},
    };

    static const std::map<std::wstring, Bracket> RIGHT_BRACKET_KEYWORDS{
            {L")", Bracket::PLAIN},
            {L"}", Bracket::CURLY},
    };

    struct OperatorMeta {
        int order;
        bool left;
    };

    static const std::map<Operator, OperatorMeta> OPERATORS{
            {Operator::CALL,    {0,  false}}, // special
            {Operator::NOT,     {1,  false}},
            {Operator::TIMES,   {3,  true}},
            {Operator::DIVIDE,  {4,  true}},
            {Operator::MODULO,  {4,  true}},
            {Operator::PLUS,    {4,  true}},
            {Operator::MINUS,   {4,  true}},
            {Operator::LAMBDA,  {7,  false}}, // special
            {Operator::EQUALS,  {8,  true}},
            {Operator::DIFFERS, {8,  true}},
            {Operator::APPEND,  {9,  false}}, // special
            {Operator::ASSIGN,  {10, true}}, // special
            {Operator::DISCARD, {11, false}}, // special
    };

    enum class Opcode : char {
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
        REV, // reverse
    };


    class CompilationError : public std::runtime_error {
    public:
        explicit CompilationError(const std::string &msg) : std::runtime_error(msg) {}
    };

    class AssertionError : public std::runtime_error {
    public:
        explicit AssertionError(const std::string &msg) : std::runtime_error(msg) {}
    };

    static void assert_failed(const std::string &what, std::source_location where = std::source_location::current()) {
        throw AssertionError(std::string(where.file_name()) + ":" + std::to_string(where.line()) + ":" +
                             std::to_string(where.column()) + ": function ‘" + where.function_name() +
                             "’: assertion failed: " + what);
    }

#define FS_ASSERT(A) do {if(!(A)) assert_failed(#A); } while(0)

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
    };

    using fchar = wchar_t;
    using fstring = std::basic_string<fchar, std::char_traits<fchar>, AllocatorWrapper<fchar>>;

    template<typename K, typename V>
    using fmap = std::map<K, V, std::less<K>, AllocatorWrapper<std::pair<const K, V>>>;

    template<typename K>
    using fset = std::set<K, std::less<>, AllocatorWrapper<K>>;
}

#endif //FUNSCRIPT_COMMON_H
