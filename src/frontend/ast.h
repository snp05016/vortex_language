#pragma once
#include "token.h"
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>
// a constructor will create a new node with the location of the token, and the
// location of thee node will be be the location of the token. The location of
// the node will be used to report errors and warnings in the source code. a
// constrctor in C++ is a special function that runs everytime a new object is
// created represents the base qualities for an ast node
// for each constructor there is std::move that is used to transfer ownership of
// the object being passed to the constructor. This is done to avoid unnecessary
// copies of the object and to ensure that the object is properly cleaned up
// when it is no longer needed. The std::move function is used to indicate that
// the object being passed to the constructor can be "moved" rather than copied,
// which can improve performance and reduce memory usage.
class Node {
public:
  virtual ~Node() =
      default; // virtual destructor to ensure proper cleanup of derived classes
  SourceLocation location() const { // getter for the location
    return location_;
  }

protected:
  // constructor to initialize the location of the node
  explicit Node(SourceLocation location) : location_(location) {}

private:
  // member variable to store the location of the node
  SourceLocation location_;
};
/* represents a general expression node in the abstract syntax tree. */
struct Expr : public Node {
public:
  virtual ~Expr() =
      default; // virtual destructor for proper cleanup of derived classes
protected:
  // create a new expr node with the location
  explicit Expr(SourceLocation location) : Node(location) {}

private:
  // creating child mnpde
};
/* represents a statement node in the abstract syntax tree. */
struct Stmt : public Node {
public:
  virtual ~Stmt() =
      default; // virtual destructor for proper cleanup of derived classes
protected:
  // create a new stmt node with the location
  explicit Stmt(SourceLocation location) : Node(location) {}
};
/* represents a declaration node in the abstract syntax tree. */
struct Decl : public Node {
public:
  virtual ~Decl() =
      default; // virtual destructor for proper cleanup of derived classes
protected:
  // create a new decl node with the location
  explicit Decl(SourceLocation location) : Node(location) {}
};
/* represents a type node in the abstract syntax tree. */
struct Type : public Node {
public:
  virtual ~Type() =
      default; // virtual destructor for proper cleanup of derived classes
protected:
  // create a new type node with the location
  explicit Type(SourceLocation location) : Node(location) {}
};
enum class LiteralKind { Integer, Float, String, Char, Boolean };
// Use a string representation so this header remains compatible with the
// project's current language standard, which does not provide std::variant.
using LiteralValue = std::variant<std::uint64_t, double, std::string, char,
                                  bool>; // its uint and not int since the ast
                                         // will have a negate if
// the number is negative.
/* represents a literal value expression. */
struct Literal : public Expr {
public:
  virtual ~Literal() =
      default; // virtual destructor for proper cleanup of derived classes
  // create a new literal node with the location
  Literal(SourceLocation location, LiteralKind kind, LiteralValue value)
      : Expr(location), kind_(kind), value_(std::move(value)) {
  } // move will transfer the ownership of value from value ( param ) to value_
    // and avoid copying the value, which can be expensive for large objects.
    // This is done to improve performance and reduce memory usage.
  LiteralKind kind() const { return kind_; }
  const LiteralValue &value() const { return value_; }

private:
  // member variable to store the kind of literal
  LiteralKind kind_;
  // member variable to store the value of the literal
  LiteralValue value_;
};
/* represents an identifier expression., for example) a variable name */
struct Identifier : public Expr {
public:
  virtual ~Identifier() =
      default; // virtual destructor for proper cleanup of derived classes
  // create a new identifier node with the location
  Identifier(SourceLocation location, std::string name)
      : Expr(location), name_(std::move(name)) {}
  const std::string &name() const { return name_; }

private:
  // member variable to store the name of the identifier
  std::string name_;
};
/* represents a grouped expression enclosed by delimiters. */
struct Grp : public Expr {
public:
  virtual ~Grp() =
      default; // virtual destructor for proper cleanup of derived classes
  // create a new group node with the location
  Grp(SourceLocation location, std::unique_ptr<Expr> expr)
      : Expr(location), expr_(std::move(expr)) {}
  const Expr &expr() const { return *expr_; }

private:
  // member variable to store the expression inside the group
  std::unique_ptr<Expr> expr_;
};
enum class UnaryOp {
  Negate,       // -x
  LogicalNot,   // !x
  BitwiseNot,   // ~x
  Positive,     // +x
  Reference,    // &x
  MutReference, // &mut x
};
/* represents an expression with one operand and one unary operator. */
struct Unary : public Expr {
public:
  virtual ~Unary() = default;
  // create a new unary node with the location
  Unary(SourceLocation location, UnaryOp op, std::unique_ptr<Expr> operand)
      : Expr(location), op_(op), operand_(std::move(operand)) {}
  UnaryOp op() const { return op_; }
  const Expr &operand() const { return *operand_; }

private:
  // member variable to store the operator of the unary expression
  UnaryOp op_;
  // member variable to store the operand of the unary expression
  // unique_ptr is a smart pointer that owns and manages another object through
  // a pointer and disposes of that object when the unique_ptr goes out of scope
  std::unique_ptr<Expr> operand_;
  // used unique_ptr because we wnat to ensure that the operand is properly
  // cleaned up when the unary object is destroyed, dint use Expr* because we
  // want to avoid memory leaks and dangling pointers, and unique_ptr provides
  // automatic memory management and ownership semantics.
};
enum class BinOp {
  Add,          // +
  Subtract,     // -
  Multiply,     // *
  Divide,       // /
  Modulo,       // %
  Equal,        // ==
  NotEqual,     // !=
  Less,         // <
  Greater,      // >
  LessEqual,    // <=
  GreaterEqual, // >=
  LogicalAnd,   // &&
  LogicalOr,    // ||
  BitwiseAnd,   // &
  BitwiseOr,    // |
  BitwiseXor,   // ^
  LeftShift,    // <<
  RightShift,   // >>
};
/* represents an expression with two operands and one binary operator. */
struct BinaryExpr : public Expr {
public:
  virtual ~BinaryExpr() =
      default; // virtual destructor for proper cleanup of derived classes
  // create a new binary expression node with the location
  BinaryExpr(SourceLocation location, BinOp op, std::unique_ptr<Expr> left,
             std::unique_ptr<Expr> right)
      : Expr(location), op_(op), left_(std::move(left)),
        right_(std::move(right)) {}
  BinOp op() const { return op_; }
  const Expr &left() const { return *left_; }
  const Expr &right() const { return *right_; }

private:
  // member variable to store the operator of the binary expression
  BinOp op_;
  // member variable to store the left operand of the binary expression
  std::unique_ptr<Expr> left_;
  std::unique_ptr<Expr> right_;
};
/* represents a range expression with a start and an end. */
struct RangeExpr : public Expr {
public:
  virtual ~RangeExpr() =
      default; // virtual destructor for proper cleanup of derived classes
  // create a new range expression node with the location
  RangeExpr(SourceLocation location, std::unique_ptr<Expr> start,
            std::unique_ptr<Expr> end, bool is_inclusive)
      : Expr(location), start_(std::move(start)), end_(std::move(end)),
        is_inclusive_(is_inclusive) {}
  const Expr &start() const { return *start_; }
  const Expr &end() const { return *end_; }
  bool is_inclusive() const { return is_inclusive_; }

private:
  // member variable to store the start of the range expression
  std::unique_ptr<Expr> start_;
  // member variable to store the end of the range expression
  std::unique_ptr<Expr> end_;
  // member variable to store whether the range is inclusive
  bool is_inclusive_;
};
enum class PrimitiveTypeKind;
/* represents a function call expression with arguments. */
struct CallCastExpr : public Expr {
public:
  virtual ~CallCastExpr() =
      default; // virtual destructor for proper cleanup of derived classes
  // create a new call cast expression node with the location
  CallCastExpr(SourceLocation location, std::unique_ptr<Expr> function,
               std::vector<std::unique_ptr<Expr>> arguments)
      : Expr(location), function_(std::move(function)),
        arguments_(std::move(arguments)) {}
  const Expr &function() const { return *function_; }
  const std::vector<std::unique_ptr<Expr>> &arguments() const {
    return arguments_;
  }

private:
  // member variable to store the function being called
  std::unique_ptr<Expr> function_;
  // member variable to store the arguments of the function call
  std::vector<std::unique_ptr<Expr>> arguments_;
};
/* represents a numeric conversion expression. */
struct CastExpr : public Expr {
public:
  virtual ~CastExpr() = default;
  CastExpr(SourceLocation location, PrimitiveTypeKind target_type,
           std::unique_ptr<Expr> operand)
      : Expr(location), target_type_(target_type), operand_(std::move(operand)) {}
  PrimitiveTypeKind target_type() const { return target_type_; }
  const Expr &operand() const { return *operand_; }

private:
  PrimitiveTypeKind target_type_;
  std::unique_ptr<Expr> operand_;
};
/* represents an indexing expression that accesses an element. */
struct IndexExpr : public Expr {
public:
  virtual ~IndexExpr() =
      default; // virtual destructor for proper cleanup of derived classes
  // create a new index expression node with the location
  IndexExpr(SourceLocation location, std::unique_ptr<Expr> collection,
            std::vector<std::unique_ptr<Expr>> index)
      : Expr(location), collection_(std::move(collection)),
        index_(std::move(index)) {}
  const Expr &collection() const { return *collection_; }
  const std::vector<std::unique_ptr<Expr>> &indices() const { return index_; }

private:
  // member variable to store the collection being indexed
  std::unique_ptr<Expr> collection_;
  // member variable to store the index expression
  // A vector allows multi-dimensional indexing.
  std::vector<std::unique_ptr<Expr>> index_;
};
/* represents an expression that accesses a named field. */
struct FieldAccessExpr : public Expr {
public:
  virtual ~FieldAccessExpr() =
      default; // virtual destructor for proper cleanup of derived classes
  // create a new field access expression node with the location
  FieldAccessExpr(SourceLocation location, std::unique_ptr<Expr> object,
                  std::string field_name)
      : Expr(location), object_(std::move(object)),
        field_name_(std::move(field_name)) {}
  const Expr &object() const { return *object_; }
  const std::string &field_name() const { return field_name_; }

private:
  // member variable to store the object being accessed
  std::unique_ptr<Expr> object_;
  // member variable to store the field name being accessed, string cuz only the
  // name of hte field is needed
  std::string field_name_;
};
/* represents an array expression containing multiple elements. */
struct ArrayExpr : public Expr {
public:
  virtual ~ArrayExpr() =
      default; // virtual destructor for proper cleanup of derived classes
  // create a new array expression node with the location
  ArrayExpr(SourceLocation location,
            std::vector<std::unique_ptr<Expr>> elements)
      : Expr(location), elements_(std::move(elements)) {}
  const std::vector<std::unique_ptr<Expr>> &elements() const {
    return elements_;
  }

private:
  // member variable to store the elements of the array expression
  std::vector<std::unique_ptr<Expr>> elements_;
};
// represents an array expression that repeats a single element a specified
// number of times. example) [0; 10] represents an array of 10 elements, all
// initialized to 0.
struct RepeatArrayExpr : public Expr {
public:
  virtual ~RepeatArrayExpr() =
      default; // virtual destructor for proper cleanup of derived classes
  RepeatArrayExpr(SourceLocation location, std::unique_ptr<Expr> value_,
                  std::vector<std::unique_ptr<Expr>> dimensions_)
      : Expr(location), value_(std::move(value_)),
        dimensions_(std::move(dimensions_)) {}
  const Expr &value() const { return *value_; }
  const std::vector<std::unique_ptr<Expr>> &dimensions() const {
    return dimensions_;
  }

private:
  // member variable to store the value to be repeated
  std::unique_ptr<Expr> value_;
  // member variable to store the dimensions of the array
  std::vector<std::unique_ptr<Expr>> dimensions_;
};
/* represents an expression that constructs a struct value. */
struct StructConstructionExpr : public Expr {
public:
  virtual ~StructConstructionExpr() =
      default; // virtual destructor for proper cleanup of derived classes
  // create a new struct construction expression node with the location
  StructConstructionExpr(
      SourceLocation location, std::string struct_name,
      std::vector<std::pair<std::string, std::unique_ptr<Expr>>> fields)
      : Expr(location), struct_name_(std::move(struct_name)),
        fields_(std::move(fields)) {}
  const std::string &struct_name() const { return struct_name_; }
  const std::vector<std::pair<std::string, std::unique_ptr<Expr>>> &
  fields() const {
    return fields_;
  }

private:
  // member variable to store the name of the struct being constructed
  std::string struct_name_;
  // member variable to store the fields of the struct construction expression,
  // in the form <name> { <string> : <expr> }
  std::vector<std::pair<std::string, std::unique_ptr<Expr>>> fields_;
};
// statments include:
//      variable declartions
//      assignments
//      return statements
//      expression statements
//      if statements
//      while statements
//      for statements
//      break statements
//      continue statements
//      block statments
/* represents a statement that declares and initializes a variable. */
struct VarDeclStmt : public Stmt {
public:
  virtual ~VarDeclStmt() = default;
  VarDeclStmt(SourceLocation location, bool is_mutable, std::string var_name,
              std::unique_ptr<Type> var_type, std::unique_ptr<Expr> initializer)
      : Stmt(location), is_mutable_(is_mutable), var_name_(std::move(var_name)),
        var_type_(std::move(var_type)), initializer_(std::move(initializer)) {}
  bool is_mutable() const { return is_mutable_; }
  const std::string &var_name() const { return var_name_; }
  const Type *var_type() const { return var_type_.get(); }
  const Expr &initializer() const { return *initializer_; }

private:
  bool is_mutable_;
  std::string var_name_;
  // Null when the source declaration relies on type inference.
  std::unique_ptr<Type> var_type_;
  std::unique_ptr<Expr> initializer_;
};
enum class AssignmentOperation {
  Assign,         // =
  AddAssign,      // +=
  SubtractAssign, // -=
  MultiplyAssign, // *=
  DivideAssign,   // /=
  RemainderAssign // %=
};
/* represents a statement that assigns a value to an expression. */
struct AssignmentStatement : public Stmt {
public:
  virtual ~AssignmentStatement() = default;
  AssignmentStatement(SourceLocation location, std::unique_ptr<Expr> lhs,
                      std::unique_ptr<Expr> rhs, AssignmentOperation op)
      : Stmt(location), lhs_(std::move(lhs)), rhs_(std::move(rhs)), op_(op) {}
  const Expr &lhs() const { return *lhs_; }
  const Expr &rhs() const { return *rhs_; }
  AssignmentOperation op() const { return op_; }

private:
  std::unique_ptr<Expr> lhs_;
  std::unique_ptr<Expr> rhs_;
  AssignmentOperation op_;
};
/* represents a statement that returns a value from a function. */
struct RetStmt : public Stmt {
public:
  virtual ~RetStmt() = default;
  RetStmt(SourceLocation location, std::unique_ptr<Expr> return_value = nullptr)
      : Stmt(location), return_value_(std::move(return_value)) {}
  const Expr *return_value() const { return return_value_.get(); }

private:
  // Null for a return statement without a value.
  std::unique_ptr<Expr> return_value_;
};
/* represents a statement formed from an expression. */
struct ExprStmt : public Stmt {
public:
  virtual ~ExprStmt() = default;
  ExprStmt(SourceLocation location, std::unique_ptr<Expr> expr)
      : Stmt(location), expr_(std::move(expr)) {}
  const Expr &expr() const { return *expr_; }

private:
  std::unique_ptr<Expr> expr_;
};
/* represents a conditional statement with optional branches. */
struct IfStmt : public Stmt {
public:
  virtual ~IfStmt() = default;
  IfStmt(SourceLocation location, std::unique_ptr<Expr> condition,
         std::unique_ptr<Stmt> then_branch, std::unique_ptr<Stmt> else_branch)
      : Stmt(location), condition_(std::move(condition)),
        then_branch_(std::move(then_branch)),
        else_branch_(std::move(else_branch)) {}
  const Expr &condition() const { return *condition_; }
  const Stmt &then_branch() const { return *then_branch_; }
  const Stmt *else_branch() const { return else_branch_.get(); }

private:
  std::unique_ptr<Expr> condition_;
  std::unique_ptr<Stmt> then_branch_;
  std::unique_ptr<Stmt> else_branch_;
};
/* represents a loop that repeats while a condition is true. */
struct WhileStmt : public Stmt {
public:
  virtual ~WhileStmt() = default;
  WhileStmt(SourceLocation location, std::unique_ptr<Expr> condition,
            std::unique_ptr<Stmt> body)
      : Stmt(location), condition_(std::move(condition)),
        body_(std::move(body)) {}
  const Expr &condition() const { return *condition_; }
  const Stmt &body() const { return *body_; }

private:
  std::unique_ptr<Expr> condition_;
  std::unique_ptr<Stmt> body_;
};
/* represents a loop with initialization, condition, and increment steps.
  for i in 0..10{    /// for i in 0..=10{
    // loop body    ///   // loop body
  }                ///   }
*/
struct ForStmt : public Stmt {
public:
  virtual ~ForStmt() = default;
  ForStmt(SourceLocation location, std::string var_name,
          std::unique_ptr<Expr> iterable_, std::unique_ptr<Stmt> body)
      : Stmt(location), var_name_(std::move(var_name)),
        iterable_(std::move(iterable_)), body_(std::move(body)) {}
  const std::string &var_name() const { return var_name_; }
  const Expr &iterable() const { return *iterable_; }
  const Stmt &body() const { return *body_; }

private:
  std::string var_name_;
  std::unique_ptr<Expr>
      iterable_; // for i in 0..10 / for i in 0..=10, can be any expression
  std::unique_ptr<Stmt> body_;
};
/* represents a statement that exits the nearest loop. */
struct BreakStmt : public Stmt {
public:
  virtual ~BreakStmt() = default;
  explicit BreakStmt(SourceLocation location) : Stmt(location) {}
};
/* represents a statement that skips to the next loop iteration. */
struct ContinueStmt : public Stmt {
public:
  virtual ~ContinueStmt() = default;
  explicit ContinueStmt(SourceLocation location) : Stmt(location) {}
};
/* represents a statement block containing an ordered list of statements. */
// anything in a block { statment* ; }
struct BlockStmt : public Stmt {
public:
  virtual ~BlockStmt() = default;
  BlockStmt(SourceLocation location,
            std::vector<std::unique_ptr<Stmt>> statements)
      : Stmt(location), statements_(std::move(statements)) {}
  const std::vector<std::unique_ptr<Stmt>> &statements() const {
    return statements_;
  }

private:
  std::vector<std::unique_ptr<Stmt>> statements_;
};
// rep param decl, represents the parameter in a function. for example left:i32
// in fn add(left:i32, right:i32) {}
struct ParamDecl {
public:
  ParamDecl(SourceLocation location, std::string param_name,
            std::unique_ptr<Type> param_type)
      : location_(location), param_name_(std::move(param_name)),
        param_type_(std::move(param_type)) {}
  SourceLocation location() const { return location_; }
  const std::string &param_name() const { return param_name_; }
  const Type &param_type() const { return *param_type_; }

private:
  SourceLocation location_;
  std::string param_name_;           // left
  std::unique_ptr<Type> param_type_; // i32
};
// represent hte field type for struct, example) x:i32 in struct point { x: i32
// , y:i32 }
struct StructFieldDecl {
public:
  StructFieldDecl(SourceLocation location, std::string struct_param_name,
                  std::unique_ptr<Type> struct_param_type)
      : location_(location), struct_param_name_(std::move(struct_param_name)),
        struct_param_type_(std::move(struct_param_type)) {}
  SourceLocation location() const { return location_; }
  const std::string &field_name() const { return struct_param_name_; }
  const Type &field_type() const { return *struct_param_type_; }

private:
  SourceLocation location_;
  std::string struct_param_name_;           // x
  std::unique_ptr<Type> struct_param_type_; // i32
};
// represents function declaration
struct FunctionDecl : public Decl {
public:
  virtual ~FunctionDecl() = default;
  FunctionDecl(SourceLocation location, std::string function_name,
               std::vector<ParamDecl> parameters,
               std::unique_ptr<Type> return_type,
               std::unique_ptr<BlockStmt> body)
      : Decl(location), function_name_(std::move(function_name)),
        parameters_(std::move(parameters)),
        return_type_(std::move(return_type)), body_(std::move(body)) {}
  const std::string &function_name() const { return function_name_; }
  const std::vector<ParamDecl> &parameters() const { return parameters_; }
  const Type &return_type() const { return *return_type_; }
  const BlockStmt &body() const { return *body_; }

private:
  std::string function_name_;
  std::vector<ParamDecl> parameters_;
  std::unique_ptr<Type> return_type_;
  std::unique_ptr<BlockStmt> body_;
};
// rep struct decl
struct StructDecl : public Decl {
public:
  virtual ~StructDecl() = default;
  StructDecl(SourceLocation location, std::string struct_name,
             std::vector<StructFieldDecl> fields)
      : Decl(location), struct_name_(std::move(struct_name)),
        fields_(std::move(fields)) {}
  const std::string &struct_name() const { return struct_name_; }
  const std::vector<StructFieldDecl> &fields() const { return fields_; }

private:
  std::string struct_name_;
  std::vector<StructFieldDecl> fields_;
};
// type of primitive kind
enum class PrimitiveTypeKind {
  Void,
  Bool,
  Char,
  i32,
  u32,
  usize,
  f32,
  f64,
  String
};
// each primitive type can be used as a
struct PrimitiveType : public Type {
public:
  PrimitiveType(SourceLocation location, PrimitiveTypeKind kind)
      : Type(location), primitive_type_(kind) {}
  PrimitiveTypeKind primitive_type() const { return primitive_type_; }

private:
  PrimitiveTypeKind primitive_type_;
};
struct ArrayType : public Type {
public:
  ArrayType(SourceLocation location, std::unique_ptr<Type> element_type,
            std::vector<std::unique_ptr<Expr>> dimensions)
      : Type(location), element_type_(std::move(element_type)),
        dimensions_(std::move(dimensions)) {}
  const Type &element_type() const { return *element_type_; }
  const std::vector<std::unique_ptr<Expr>> &dimensions() const {
    return dimensions_;
  }

private:
  std::unique_ptr<Type> element_type_;
  std::vector<std::unique_ptr<Expr>> dimensions_;
};
struct StructType : public Type {
public:
  StructType(SourceLocation location, std::string struct_name)
      : Type(location), struct_name_(std::move(struct_name)) {}
  const std::string &struct_name() const { return struct_name_; }

private:
  std::string struct_name_;
};
struct ReferenceType : public Type {
public:
  ReferenceType(SourceLocation location, std::unique_ptr<Type> referenced_type,
                bool is_mutable)
      : Type(location), referenced_type_(std::move(referenced_type)),
        is_mutable_(is_mutable) {}
  const Type &referenced_type() const { return *referenced_type_; }
  bool is_mutable() const { return is_mutable_; }

private:
  std::unique_ptr<Type> referenced_type_;
  bool is_mutable_;
};
class Program {
public:
  Program(std::vector<std::unique_ptr<Decl>> declarations)
      : declarations_(std::move(declarations)) {}
  const std::vector<std::unique_ptr<Decl>> &declarations() const {
    return declarations_;
  }

private:
  std::vector<std::unique_ptr<Decl>> declarations_;
};
