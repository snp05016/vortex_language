#include "frontend/debugVisitor.h"
#include "frontend/lexer.h"
#include "frontend/parser/parser.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: vortex_debug <source-file>\n";
    return 1;
  }
  const std::string path = argv[1];
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    std::cerr << "Could not open: " << path << '\n';
    return 1;
  }
  const std::string source{std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>()};
  Lexer lexer(source.data(), source.size());
  std::cout << "Tokenizing " << path << "\n\n";
  Parser parser(source.data(), source.size());
  parser.parse();
  DebugVisitor debugVisitor;
  std::cout << "\n Debugging tokens:\n";
  while (true) {
    const Token token = lexer.next_token();
    const std::string_view text(source.data() + token.location.start,
                                token.location.length);
    debugVisitor.visit(token);
    if (token.kind == TokenKind::EOF_TOKEN) {
      break;
    }
  }
  return 0;
}
