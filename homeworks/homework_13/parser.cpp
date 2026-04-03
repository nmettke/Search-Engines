/*
 * parser.cpp
 *
 * Implementation of parser.h
 *
 * See parser.h for a full BNF of the grammar to implement.
 *
 * You should implement the different Find( ) functions,
 * as well as any additional functions you declare in parser.h.
 */

#include "parser.h"
#include "expression.h"

Expression *Parser::FindFactor() {

    if (error)
        return nullptr;

    if (stream.Match('(')) {
        Expression *expression = FindAdd();

        if (!stream.Match(')') || error) {
            delete expression;
            error = true;
            return nullptr;
        }

        return expression;
    }

    if (stream.Match('-')) {
        Expression *value = FindFactor();

        if (error) {
            return nullptr;
        }

        return new UnaryMinus(value);
    }

    Number *numbr = stream.ParseNumber();
    if (numbr) {
        return numbr;
    }

    error = true;
    return nullptr;
}

Expression *Parser::FindAdd() {
    Expression *left = FindMultiply();

    if (error) {
        return nullptr;
    }

    while (!stream.AllConsumed()) {

        if (stream.Match('+')) {
            Expression *right = FindMultiply();

            if (error) {
                delete left;
                return nullptr;
            }

            left = new Add(left, right);

        } else if (stream.Match('-')) {
            Expression *right = FindMultiply();

            if (error) {
                delete left;
                return nullptr;
            }

            left = new Subtract(left, right);

        } else {
            break;
        }
    }

    return left;
}

Expression *Parser::FindMultiply() {
    Expression *left = FindFactor();

    if (error) {
        return nullptr;
    }

    while (!stream.AllConsumed()) {

        if (stream.Match('*')) {
            Expression *right = FindFactor();

            if (error) {
                delete left;
                return nullptr;
            }

            left = new Multiply(left, right);

        } else if (stream.Match('/')) {

            Expression *right = FindFactor();

            if (error) {
                delete left;
                return nullptr;
            }

            left = new Divide(left, right);

        } else {
            break;
        }
    }

    return left;
}

Expression *Parser::Parse() {

    Expression *expression = FindAdd();

    if (!stream.AllConsumed() || error) {
        delete expression;
        return nullptr;
    }

    return expression;
}

Parser::Parser(const std::string &in) : stream(in), error(false) {}
