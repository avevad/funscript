#include "ast.hpp"
#include <cstring>

namespace funscript {

    Assembler::Chunk::Chunk(size_t id) : id(id) {}

    size_t Assembler::Chunk::put_instruction(Instruction ins) {
        auto pos = size();
        bytes.append(reinterpret_cast<char *>(&ins), sizeof(ins));
        return pos;
    }

    void Assembler::Chunk::set_instruction(size_t pos, Instruction ins) {
        memcpy(bytes.data() + pos, &ins, sizeof ins);
    }

    size_t Assembler::Chunk::size() const { return bytes.length(); }

    void Assembler::add_pointer(size_t from_chunk, size_t from_pos, size_t to_chunk, size_t to_pos) {
        pointers.emplace_back(from_chunk, from_pos, to_chunk, to_pos);
    }

    Assembler::Chunk &Assembler::new_chunk() {
        chunks.emplace_back(new Chunk(chunks.size()));
        return *chunks.back();
    }

    size_t Assembler::add_string(const std::string &str) {
        size_t pos = chunks[DATA]->bytes.size();
        chunks[DATA]->bytes.append(str.data(), str.size());
        chunks[DATA]->bytes += '\0';
        return pos;
    }

    void Assembler::compile_expression(funscript::AST *ast) {
        chunks.clear();
        pointers.clear();
        new_chunk(); // Data chunk
        auto &ch = new_chunk(); // Main chunk
        ch.put_instruction({Opcode::DIS}); // Discard any arguments in the main function
        ast->compile_eval(*this, ch, {});
        ch.put_instruction({Opcode::END});
    }

    size_t Assembler::total_size() const {
        size_t size = 0;
        for (auto &ch : chunks) size += ch->size();
        return size;
    }

    void Assembler::assemble(char *buffer) const {
        std::vector<size_t> chunks_pos(chunks.size());
        // Concatenating all chunks in proper order, remembering their start positions
        size_t pos = 0;
        for (size_t i = 0; i < chunks.size(); i++) {
            size_t ch_id = (i + 1) % chunks.size(); // Data chunk should be at the end, so we rotate chunks by 1
            chunks_pos[ch_id] = pos;
            memcpy(buffer + pos, chunks[ch_id]->bytes.data(),
                   chunks[ch_id]->size()); // NOLINT(bugprone-not-null-terminated-result)
            pos += chunks[ch_id]->size();
        }
        // Executing scheduled pointer insertion
        for (auto [from_chunk, from_pos, to_chunk, to_pos] : pointers) {
            size_t to_real_pos = chunks_pos[to_chunk] + to_pos;
            size_t from_real_pos = chunks_pos[from_chunk] + from_pos;
            memcpy(buffer + from_real_pos, &to_real_pos, sizeof to_real_pos);
        }
    }
}