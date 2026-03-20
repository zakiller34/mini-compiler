#include "ast.h"
#include "interpreter.h"
#include "lexer.h"
#include "parser.h"
#include "type_checker.h"
#include "passes/assign_homes.h"
#include "passes/emit.h"
#include "passes/explicate_control.h"
#include "passes/expose_allocation.h"
#include "passes/patch_instructions.h"
#include "passes/prelude_conclusion.h"
#include "passes/rco.h"
#include "passes/select_instructions.h"
#include "passes/shrink.h"
#include "passes/uncover_get.h"
#include "passes/uniquify.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

using namespace mc;

/// @brief Open and parse .mc file into AST
/// @requires filename is a readable .mc file path
/// @ensures result has value on success, nullopt on file error
std::optional<Program> parse_file(const std::string &filename) {
    std::ifstream ifs(filename);
    if (!ifs) {
        std::cerr << "error: cannot open " << filename << "\n";
        return std::nullopt;
    }
    Lexer lex(ifs);
    Parser parser(lex);
    auto p = parser.parse_program();
    return Program{std::move(p->body)};
}

/// @brief Full compilation pipeline: parse -> type check -> passes -> emit x86
/// @requires src_file is a valid .mc file
/// @ensures out_file contains AT&T x86-64 assembly on success (returns 0)
int compile(const std::string &src_file, const std::string &out_file) {
    auto prog = parse_file(src_file);
    if (!prog) return 1;

    try { type_check(*prog); }
    catch (const TypeError &e) {
        std::cerr << "type error: " << e.what() << "\n";
        return 1;
    }

    auto p0 = shrink(*prog);
    auto p1 = uniquify(*p0);
    auto p1b = uncover_get(*p1);
    auto p1c = expose_allocation(*p1b);
    auto p2 = remove_complex_operands(*p1c);
    auto c_prog = explicate_control(*p2);
    auto x1 = select_instructions(c_prog);
    auto x2 = assign_homes(x1);
    auto x3 = patch_instructions(x2);
    auto x4 = generate_prelude_conclusion(x3);
    std::string asm_out = emit(x4);

    std::ofstream ofs(out_file);
    if (!ofs) {
        std::cerr << "error: cannot write " << out_file << "\n";
        return 1;
    }
    ofs << asm_out;
    return 0;
}

/// @brief Parse, type-check, and interpret program; print result to stdout
/// @requires src_file is a valid .mc file
/// @ensures prints result value to stdout, returns 0 on success
int run_interpret(const std::string &src_file,
                  const std::string &input_file) {
    auto prog = parse_file(src_file);
    if (!prog) return 1;

    try { type_check(*prog); }
    catch (const TypeError &e) {
        std::cerr << "type error: " << e.what() << "\n";
        return 1;
    }

    auto do_interp = [&](std::istream &in) {
        Value result = interpret(*prog, in);
        if (auto *i = std::get_if<int64_t>(&result)) {
            std::cout << *i << "\n";
        } else if (auto *b = std::get_if<bool>(&result)) {
            std::cout << (*b ? "true" : "false") << "\n";
        } else if (std::holds_alternative<Tuple>(result)) {
            std::cout << "(vector ...)" << "\n";
        } else {
            std::cout << "(void)" << "\n";
        }
    };

    if (!input_file.empty()) {
        std::ifstream ifs(input_file);
        if (!ifs) {
            std::cerr << "error: cannot open " << input_file << "\n";
            return 1;
        }
        do_interp(ifs);
    } else {
        do_interp(std::cin);
    }
    return 0;
}

/// @brief CLI entry point: compile or interpret .mc files
/// @requires argc >= 2
int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "usage: mc <input.mc> -o <output.s>\n";
        std::cerr << "       mc -i <input.mc> [-input <input.txt>]\n";
        return 1;
    }

    if (std::strcmp(argv[1], "-i") == 0) {
        if (argc < 3) {
            std::cerr << "usage: mc -i <input.mc> [-input <input.txt>]\n";
            return 1;
        }
        std::string input_file;
        if (argc >= 5 && std::strcmp(argv[3], "-input") == 0) {
            input_file = argv[4];
        }
        return run_interpret(argv[2], input_file);
    }

    if (argc < 4 || std::strcmp(argv[2], "-o") != 0) {
        std::cerr << "usage: mc <input.mc> -o <output.s>\n";
        return 1;
    }
    return compile(argv[1], argv[3]);
}
