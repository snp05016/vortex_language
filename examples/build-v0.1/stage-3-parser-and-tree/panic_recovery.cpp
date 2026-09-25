// Panic-mode recovery for a tiny statement language, "name = number ;".
// When a statement's shape is wrong, report it, discard tokens up to the
// next ';' and keep going, so one bad statement does not hide the next.
//
// Follows: Robert Nystrom, Crafting Interpreters, "Parsing Expressions"
// (synchronizing at a statement boundary after an error).

#include <optional>
#include <print>
#include <string>
#include <vector>

struct Token {
  enum Kind { Ident, Num, Equals, Semicolon, End } kind;
  std::string text;
};

struct Statement {
  std::string name;
  int value;
};

class Parser {
 public:
  explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

  std::vector<Statement> parse_program() {
    std::vector<Statement> statements;
    while (tokens_[pos_].kind != Token::End) {
      if (auto statement = parse_statement()) {
        statements.push_back(*statement);
      }
    }
    return statements;
  }

 private:
  // A name, '=', a number, ';'. Each token is checked before it is used, so
  // a statement may start with any token and still fail cleanly.
  std::optional<Statement> parse_statement() {
    if (!expect(Token::Ident, "a name")) return std::nullopt;
    const std::string name = tokens_[pos_ - 1].text;
    if (!expect(Token::Equals, "'='")) return std::nullopt;
    if (!expect(Token::Num, "a number")) return std::nullopt;
    const int value = std::stoi(tokens_[pos_ - 1].text);
    if (!expect(Token::Semicolon, "';'")) return std::nullopt;
    return Statement{name, value};
  }

  // Consume one token of the wanted kind, or report what was found instead
  // and recover.
  bool expect(Token::Kind kind, const char* wanted) {
    if (tokens_[pos_].kind == kind) {
      ++pos_;
      return true;
    }
    std::println("error: expected {}, found '{}'", wanted, tokens_[pos_].text);
    synchronize();
    return false;
  }

  // Discard tokens through the next ';', or up to the end of the file.
  // Either a token is consumed or the end is reached, so parse_program can
  // never loop on the same bad token.
  void synchronize() {
    while (tokens_[pos_].kind != Token::Semicolon &&
           tokens_[pos_].kind != Token::End) {
      ++pos_;
    }
    if (tokens_[pos_].kind == Token::Semicolon) ++pos_;
  }

  std::vector<Token> tokens_;
  std::size_t pos_ = 0;
};

int main() {
  // x = 1 ; y = ; z = 3 ;   (the middle statement is missing its number)
  const std::vector<Token> tokens = {
      {Token::Ident, "x"},     {Token::Equals, "="}, {Token::Num, "1"},
      {Token::Semicolon, ";"}, {Token::Ident, "y"},   {Token::Equals, "="},
      {Token::Semicolon, ";"}, {Token::Ident, "z"},   {Token::Equals, "="},
      {Token::Num, "3"},       {Token::Semicolon, ";"}, {Token::End, ""}};

  Parser parser(tokens);
  for (const auto& statement : parser.parse_program()) {
    std::println("{} = {}", statement.name, statement.value);
  }
}
