from __future__ import annotations

import ast
import math
import operator


class NumericExpressionError(ValueError):
    pass


_BINARY_OPERATORS = {
    ast.Add: operator.add,
    ast.Sub: operator.sub,
    ast.Mult: operator.mul,
    ast.Div: operator.truediv,
}
_UNARY_OPERATORS = {
    ast.UAdd: operator.pos,
    ast.USub: operator.neg,
}


def evaluate_numeric_expression(text: str) -> float:
    """Evaluate a small arithmetic expression without executing Python."""

    normalized = text.strip().replace(",", ".")
    if not normalized:
        raise NumericExpressionError("empty numeric expression")
    try:
        expression = ast.parse(normalized, mode="eval")
    except SyntaxError as error:
        raise NumericExpressionError("invalid numeric expression") from error
    if sum(1 for _node in ast.walk(expression)) > 64:
        raise NumericExpressionError("numeric expression is too complex")

    def evaluate(node: ast.AST) -> float:
        if (
            isinstance(node, ast.Constant)
            and isinstance(node.value, (int, float))
            and not isinstance(node.value, bool)
        ):
            return float(node.value)
        if isinstance(node, ast.UnaryOp):
            operation = _UNARY_OPERATORS.get(type(node.op))
            if operation is not None:
                return float(operation(evaluate(node.operand)))
        if isinstance(node, ast.BinOp):
            operation = _BINARY_OPERATORS.get(type(node.op))
            if operation is not None:
                try:
                    return float(
                        operation(evaluate(node.left), evaluate(node.right))
                    )
                except ZeroDivisionError as error:
                    raise NumericExpressionError("division by zero") from error
        raise NumericExpressionError("unsupported numeric expression")

    value = evaluate(expression.body)
    if not math.isfinite(value):
        raise NumericExpressionError("non-finite numeric result")
    return value
