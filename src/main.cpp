#include "ast.h"
#include "interpreter.h"
#include "lexer.h"
#include "parser.h"
#include "type_checker.h"
#include "passes/assign_homes.h"
#include "passes/emit.h"
#include "passes/explicate_control.h"
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
#include <string>

std::unique_ptr<Program> parse_file(const std::string &filename) {
    std::ifstream ifs(filename);
    if (!ifs) {
        std::cerr << "error: cannot open " << filename << "\n";
        return nullptr;
    }
    Lexer lex(ifs);
    Parser parser(lex);
    return parser.parse_program();
}

int compile(const std::string &src_file, const std::string &out_file) {
    auto prog = parse_file(src_file);
    if (prog == nullptr) return 1;

    try { type_check(*prog); }
    catch (const TypeError &e) {
        std::cerr << "type error: " << e.what() << "\n";
        return 1;
    }

    auto p0 = shrink(*prog);
    auto p1 = uniquify(*p0);
    auto p1b = uncover_get(*p1);
    auto p2 = remove_complex_operands(*p1b);
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

int run_interpret(const std::string &src_file,
                  const std::string &input_file) {
    auto prog = parse_file(src_file);
    if (prog == nullptr) return 1;

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
