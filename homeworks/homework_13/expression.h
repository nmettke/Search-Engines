/*
 * expression.h
 *
 * Class declarations for expressions
 *
 * You should declare more classes and functionality to match
 * the target grammar. Don't forget to implement them in expression.cpp.
 */

#ifndef EXPRESSION_H_
#define EXPRESSION_H_

#include <stdint.h>

/**
 * Just a plain old expression
 */
class Expression {
  public:
    virtual ~Expression();

    virtual int64_t Eval() const = 0;
};
// class Expression

/**
 * A number
 */
class Number : public Expression {
  protected:
    int64_t value;

  public:
    Number(int64_t val);

    int64_t Eval() const override;
};
// class Number

class Add : public Expression {
  private:
    Expression *left;
    Expression *right;

  public:
    Add(Expression *l, Expression *r);

    ~Add() override;

    int64_t Eval() const override;
};

class Subtract : public Expression {
  private:
    Expression *left;
    Expression *right;

  public:
    Subtract(Expression *l, Expression *r);
    ~Subtract() override;

    int64_t Eval() const override;
};

class Multiply : public Expression {
  private:
    Expression *left;
    Expression *right;

  public:
    Multiply(Expression *l, Expression *r);
    ~Multiply() override;

    int64_t Eval() const override;
};

class Divide : public Expression {
  private:
    Expression *left;
    Expression *right;

  public:
    Divide(Expression *l, Expression *r);

    ~Divide() override;

    int64_t Eval() const override;
};

class UnaryMinus : public Expression {
  private:
    Expression *value;

  public:
    UnaryMinus(Expression *value);

    ~UnaryMinus() override;

    int64_t Eval() const override;
};

#endif /* EXPRESSION_H_ */
