//
// Created by avevad on 11/20/21.
//

#ifndef FUNSCRIPT_TOKENIZER_H
#define FUNSCRIPT_TOKENIZER_H

#include "common.h"

#include <functional>

namespace funscript {

    bool is_valid_id(const std::wstring &token);

    Token get_token(const std::wstring &token);

    void tokenize(const std::wstring &code, const std::function<void(Token)> &cb);

}

#endif //FUNSCRIPT_TOKENIZER_H
