//
// Created by avevad on 11/20/21.
//

#ifndef FUNSCRIPT_TOKENIZER_H
#define FUNSCRIPT_TOKENIZER_H

#include "common.h"

#include <functional>

namespace funscript {

    static const std::wstring NUL_KW = L"nul";
    static const std::wstring YES_KW = L"yes";
    static const std::wstring NO_KW = L"no";

    class TokenAutomaton {
        size_t len = 0;
        bool nul_part = true;
        bool yes_part = true;
        bool no_part = true;
        bool id_part = true;
        bool int_part = true;
        bool index_part = true;
        bool left_bracket_part = true;
        bool right_bracket_part = true;
        std::vector<std::wstring> ops_part;
    public:
        TokenAutomaton();

        void append(wchar_t ch);

        [[nodiscard]] bool is_valid() const;
    };

    bool is_valid_id(const std::wstring &token);

    Token get_token(const std::wstring &token);

    void tokenize(const std::wstring &code, const std::function<void(Token)> &cb);

}

#endif //FUNSCRIPT_TOKENIZER_H
