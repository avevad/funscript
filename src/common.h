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
        ELSE
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
            INDEX,
            BOOLEAN
        };
        using Data = std::variant<Operator, Bracket, int64_t, std::wstring, bool>;
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
            {L"<",  Operator::LESS},
            {L">",  Operator::GREATER},
            {L"<=", Operator::LESS_EQUAL},
            {L">=", Operator::GREATER_EQUAL},
            {L"=>", Operator::THEN},
            {L"then", Operator::THEN},
            {L"?",  Operator::ELSE},
            {L"else",  Operator::ELSE}
    };

    static const std::map<wchar_t, Bracket> LEFT_BRACKET_KEYWORDS{
            {L'(', Bracket::PLAIN},
            {L'{', Bracket::CURLY},
    };

    static const std::map<wchar_t, Bracket> RIGHT_BRACKET_KEYWORDS{
            {L')', Bracket::PLAIN},
            {L'}', Bracket::CURLY},
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
            {Operator::DISCARD,       {13, false}}, // special
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
        PBY, // push boolean "yes"
        PBN, // push boolean "no",
        JN,  // jump if "no"
        JMP, // jump unconditionally
        POP, // pop one value from stack
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

    template<typename T>
    static std::string addr_to_string(const T *ptr) {
        std::ostringstream str;
        str.imbue(std::locale("C"));
        str << (const void *) ptr;
        return str.str();
    }

    using fchar = wchar_t;

    using fstring = std::basic_string<fchar, std::char_traits<fchar>, AllocatorWrapper<fchar>>;
    template<typename K, typename V>
    using fmap = std::map<K, V, std::less<K>, AllocatorWrapper<std::pair<const K, V>>>;

    template<typename K>
    using fset = std::set<K, std::less<>, AllocatorWrapper<K>>;

    static std::string dump_bytecode(const char *bytecode, const char *end) {
        std::string result;
        const char *ptr = bytecode;
        while (ptr != end) {
            auto opcode = (Opcode) *ptr;
            result += addr_to_string(ptr);
            result += ": ";
            switch (opcode) {
                case Opcode::NOP:
                    ptr++;
                    result += "NOP";
                    break;
                case Opcode::NUL: {
                    ptr++;
                    result += "NUL";
                    break;
                }
                case Opcode::SEP: {
                    ptr++;
                    result += "SEP";
                    break;
                }
                case Opcode::INT: {
                    ptr++;
                    result += "INT ";
                    int64_t num;
                    memcpy(&num, ptr, sizeof(int64_t));
                    ptr += sizeof(int64_t);
                    result += std::to_string(num);
                    break;
                }
                case Opcode::OP: {
                    ptr++;
                    result += "OP ";
                    auto oper = (Operator) *ptr;
                    ptr++;
                    result += std::to_string(int(oper));
                    break;
                }
                case Opcode::DIS: {
                    ptr++;
                    result += "DIS";
                    break;
                }
                case Opcode::NS: {
                    ptr++;
                    result += "NS";
                    break;
                }
                case Opcode::DS: {
                    ptr++;
                    result += "DS";
                    break;
                }
                case Opcode::END:
                    ptr++;
                    result += "END";
                    break;
                case Opcode::FUN: {
                    ptr++;
                    result += "FUN ";
                    size_t pos = 0;
                    memcpy(&pos, ptr, sizeof(size_t));
                    ptr += sizeof(size_t);
                    result += addr_to_string(bytecode + pos);
                    break;
                }
                case Opcode::OBJ: {
                    ptr++;
                    result += "OBJ";
                    break;
                }
                case Opcode::VGT: {
                    ptr++;
                    result += "VGT ";
                    size_t pos = 0;
                    memcpy(&pos, ptr, sizeof(size_t));
                    ptr += sizeof(size_t);
                    std::string name(reinterpret_cast<const char *>(bytecode + pos));
                    result += name;
                    break;
                }
                case Opcode::VST: {
                    ptr++;
                    result += "VST ";
                    size_t pos = 0;
                    memcpy(&pos, ptr, sizeof(size_t));
                    ptr += sizeof(size_t);
                    std::string name(reinterpret_cast<const char *>(bytecode + pos));
                    result += name;
                    break;
                }
                case Opcode::GET: {
                    ptr++;
                    result += "GET ";
                    size_t pos = 0;
                    memcpy(&pos, ptr, sizeof(size_t));
                    ptr += sizeof(size_t);
                    std::string name(reinterpret_cast<const char *>(bytecode + pos));
                    result += name;
                    break;
                }
                case Opcode::SET: {
                    ptr++;
                    result += "SET ";
                    size_t pos = 0;
                    memcpy(&pos, ptr, sizeof(size_t));
                    ptr += sizeof(size_t);
                    std::string name(reinterpret_cast<const char *>(bytecode + pos));
                    result += name;
                    break;
                }
                case Opcode::PBY: {
                    ptr++;
                    result += "PBY";
                    break;
                }
                case Opcode::PBN: {
                    ptr++;
                    result += "PBN";
                    break;
                }
                case Opcode::JMP: {
                    ptr++;
                    result += "JMP ";
                    size_t pos = 0;
                    memcpy(&pos, ptr, sizeof(size_t));
                    ptr += sizeof(size_t);
                    result += addr_to_string(bytecode + pos);
                    break;
                }
                case Opcode::JN: {
                    ptr++;
                    result += "JN ";
                    size_t pos = 0;
                    memcpy(&pos, ptr, sizeof(size_t));
                    ptr += sizeof(size_t);
                    result += addr_to_string(bytecode + pos);
                    break;
                }
                case Opcode::POP: {
                    ptr++;
                    result += "POP";
                    break;
                }
                default:
                    throw std::runtime_error("unknown opcode");
            }
            result += '\n';
        }
        return result;
    }
}

#endif //FUNSCRIPT_COMMON_H
