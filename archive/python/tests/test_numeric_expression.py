import unittest

from zima_cad.numeric_expression import (
    NumericExpressionError,
    evaluate_numeric_expression,
)


class NumericExpressionTests(unittest.TestCase):
    def test_basic_dimension_calculations(self):
        self.assertEqual(evaluate_numeric_expression("5+5"), 10.0)
        self.assertEqual(evaluate_numeric_expression("5/2"), 2.5)
        self.assertEqual(evaluate_numeric_expression("8*9"), 72.0)

    def test_operator_precedence_and_parentheses(self):
        self.assertEqual(evaluate_numeric_expression("5+4*4"), 21.0)
        self.assertEqual(evaluate_numeric_expression("(5+4)*4"), 36.0)

    def test_decimal_comma_and_unary_signs(self):
        self.assertEqual(evaluate_numeric_expression("2,5 * -4"), -10.0)

    def test_rejects_code_and_unsupported_operators(self):
        for expression in (
            "2**8",
            "abs(-5)",
            "name + 1",
            "1 // 2",
            "1 / 0",
        ):
            with self.subTest(expression=expression):
                with self.assertRaises(NumericExpressionError):
                    evaluate_numeric_expression(expression)


if __name__ == "__main__":
    unittest.main()
