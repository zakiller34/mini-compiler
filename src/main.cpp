#include "ast.h"
#include "interpreter.h"
#include "lexer.h"
#include "parser.h"
#include "passes/assign_homes.h"
#include "passes/emit.h"
#include "passes/explicate_control.h"
#include "passes/patch_instructions.h"
#include "passes/prelude_conclusion.h"
#include "passes/rco.h"
#include "passes/select_instructions.h"
#include "passes/uniquify.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

/// @brief Parse source file into AST Program
/// @requires filename points to valid .mc file
/// @ensures result contains parsed AST, nullptr on error
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

/// @brief Run full compilation pipeline: parse -> ... -> emit
/// @requires src_file is valid .mc path, out_file is writable path
/// @ensures out_file contains x86-64 AT&T assembly
int compile(const std::string &src_file, const std::string &out_file) {
    auto prog = parse_file(src_file);
    if (prog == nullptr) {
        return 1;
    }

    auto p1 = uniquify(*prog);
    auto p2 = remove_complex_operands(*p1);
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

/// @brief Run interpreter mode
/// @requires src_file is valid .mc path
/// @ensures prints interpreted result to stdout
int run_interpret(const std::string &src_file, const std::string &input_file) {
    auto prog = parse_file(src_file);
    if (prog == nullptr) {
        return 1;
    }

    if (!input_file.empty()) {
        std::ifstream ifs(input_file);
        if (!ifs) {
            std::cerr << "error: cannot open " << input_file << "\n";
            return 1;
        }
        int64_t result = interpret(*prog, ifs);
        std::cout << result << "\n";
    } else {
        int64_t result = interpret(*prog, std::cin);
        std::cout << result << "\n";
    }
    return 0;
}

/// @brief CLI entry point
/// @ensures returns 0 on success, 1 on error
int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "usage: mc <input.mc> -o <output.s>\n";
        std::cerr << "       mc -i <input.mc> [-input <input.txt>]\n";
        return 1;
    }

    // Interpret mode: mc -i input.mc [-input input.txt]
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

    // Compile mode: mc input.mc -o output.s
    if (argc < 4 || std::strcmp(argv[2], "-o") != 0) {
        std::cerr << "usage: mc <input.mc> -o <output.s>\n";
        return 1;
    }
    return compile(argv[1], argv[3]);
}
