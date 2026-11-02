/* 
 * CS:APP Data Lab 
 * 
 * <Please put your name and userid here>
 * 
 * bits.c - Source file with your solutions to the Lab.
 *          This is the file you will hand in to your instructor.
 *
 * WARNING: Do not include the <stdio.h> header; it confuses the dlc
 * compiler. You can still use printf for debugging without including
 * <stdio.h>, although you might get a compiler warning. In general,
 * it's not good practice to ignore compiler warnings, but in this
 * case it's OK.  
 */

#if 0
/*
 * Instructions to Students:
 *
 * STEP 1: Read the following instructions carefully.
 */

You will provide your solution to the Data Lab by
editing the collection of functions in this source file.

INTEGER CODING RULES:
 
  Replace the "return" statement in each function with one
  or more lines of C code that implements the function. Your code 
  must conform to the following style:
 
  int Funct(arg1, arg2, ...) {
      /* brief description of how your implementation works */
      int var1 = Expr1;
      ...
      int varM = ExprM;

      varJ = ExprJ;
      ...
      varN = ExprN;
      return ExprR;
  }

  Each "Expr" is an expression using ONLY the following:
  1. Integer constants 0 through 255 (0xFF), inclusive. You are
      not allowed to use big constants such as 0xffffffff.
  2. Function arguments and local variables (no global variables).
  3. Unary integer operations ! ~
  4. Binary integer operations & ^ | + << >>
    
  Some of the problems restrict the set of allowed operators even further.
  Each "Expr" may consist of multiple operators. You are not restricted to
  one operator per line.

  You are expressly forbidden to:
  1. Use any control constructs such as if, do, while, for, switch, etc.
  2. Define or use any macros.
  3. Define any additional functions in this file.
  4. Call any functions.
  5. Use any other operations, such as &&, ||, -, or ?:
  6. Use any form of casting.
  7. Use any data type other than int.  This implies that you
     cannot use arrays, structs, or unions.

 
  You may assume that your machine:
  1. Uses 2s complement, 32-bit representations of integers.
  2. Performs right shifts arithmetically.
  3. Has unpredictable behavior when shifting if the shift amount
     is less than 0 or greater than 31.


EXAMPLES OF ACCEPTABLE CODING STYLE:
  /*
   * pow2plus1 - returns 2^x + 1, where 0 <= x <= 31
   */
  int pow2plus1(int x) {
     /* exploit ability of shifts to compute powers of 2 */
     return (1 << x) + 1;
  }

  /*
   * pow2plus4 - returns 2^x + 4, where 0 <= x <= 31
   */
  int pow2plus4(int x) {
     /* exploit ability of shifts to compute powers of 2 */
     int result = (1 << x);
     result += 4;
     return result;
  }

FLOATING POINT CODING RULES

For the problems that require you to implement floating-point operations,
the coding rules are less strict.  You are allowed to use looping and
conditional control.  You are allowed to use both ints and unsigneds.
You can use arbitrary integer and unsigned constants. You can use any arithmetic,
logical, or comparison operations on int or unsigned data.

You are expressly forbidden to:
  1. Define or use any macros.
  2. Define any additional functions in this file.
  3. Call any functions.
  4. Use any form of casting.
  5. Use any data type other than int or unsigned.  This means that you
     cannot use arrays, structs, or unions.
  6. Use any floating point data types, operations, or constants.


NOTES:
  1. Use the dlc (data lab checker) compiler (described in the handout) to 
     check the legality of your solutions.
  2. Each function has a maximum number of operations (integer, logical,
     or comparison) that you are allowed to use for your implementation
     of the function.  The max operator count is checked by dlc.
     Note that assignment ('=') is not counted; you may use as many of
     these as you want without penalty.
  3. Use the btest test harness to check your functions for correctness.
  4. Use the BDD checker to formally verify your functions
  5. The maximum number of ops for each function is given in the
     header comment for each function. If there are any inconsistencies 
     between the maximum ops in the writeup and in this file, consider
     this file the authoritative source.

/*
 * STEP 2: Modify the following functions according the coding rules.
 * 
 *   IMPORTANT. TO AVOID GRADING SURPRISES:
 *   1. Use the dlc compiler to check that your solutions conform
 *      to the coding rules.
 *   2. Use the BDD checker to formally verify that your solutions produce 
 *      the correct answers.
 */


#endif
//1
/* 
 * bitXor - x^y using only ~ and & 
 *   Example: bitXor(4, 5) = 1
 *   Legal ops: ~ &
 *   Max ops: 14
 *   Rating: 1
 */
int bitXor(int x, int y) {
  int a, b, c;
  a = ~x & ~y;
  b = ~(x & y);
  c = ~a;
  return b & c;
}
/* 
 * tmin - return minimum two's complement integer 
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 4
 *   Rating: 1
 */
int tmin(void) {
  return 1 << 31;
}
//2
/*
 * isTmax - returns 1 if x is the maximum, two's complement number,
 *     and 0 otherwise 
 *   Legal ops: ! ~ & ^ | +
 *   Max ops: 10
 *   Rating: 1
 */
int isTmax(int x) {
  int a, b, c, d;
  a = x + 1;
  b = ~a + 1;
  c = a ^ b;
  d = a ^ 0;//也可用!!a
  return !!d & !c;
}
/* 
 * allOddBits - return 1 if all odd-numbered bits in word set to 1
 *   where bits are numbered from 0 (least significant) to 31 (most significant)
 *   Examples allOddBits(0xFFFFFFFD) = 0, allOddBits(0xAAAAAAAA) = 1
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 12
 *   Rating: 2
 */
int allOddBits(int x) {
  int a, b;
  a = 0xAA;
  a = (a << 8) + a;
  a = (a << 16) + a;
  b = a & x;
  return !(b ^ a);
}
/* 
 * negate - return -x 
 *   Example: negate(1) = -1.
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 5
 *   Rating: 2 
 */
int negate(int x) {
  return ~x + 1;
}
//3
/* 
 * isAsciiDigit - return 1 if 0x30 <= x <= 0x39 (ASCII codes for characters '0' to '9')
 *   Example: isAsciiDigit(0x35) = 1.
 *            isAsciiDigit(0x3a) = 0.
 *            isAsciiDigit(0x05) = 0.
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 15
 *   Rating: 3
 */
int isAsciiDigit(int x) {
  int a, b, c;
  // a = 0xFF;
  // n = 0x30;
  // m = (a << 8) + a;
  // m = (m << 8) + a;
  // m = (m << 8) + 0xF0;
  // b = m & x;
  // b = b ^ n;
  a = x >> 4;
  a = a ^ 3;
  b = 8;
  b = b & x;
  c = 6;
  c = c & x;
  return !a & (!b | !c);
  //也可以用加法溢出法，直接将x与边界加减比较符号位
}
/* 
 * conditional - same as x ? y : z 
 *   Example: conditional(2,4,5) = 4
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 16
 *   Rating: 3
 */
int conditional(int x, int y, int z) {
  int a, b, c;
  a = !!x;
  b = ~a + 1;
  // c = ~1 + 1;
  // d = a + c;
  c = ~b;
  return (b & y) + (c & z);
}
/* 
 * isLessOrEqual - if x <= y  then return 1, else return 0 
 *   Example: isLessOrEqual(4,5) = 1.
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 24
 *   Rating: 3
 */
int isLessOrEqual(int x, int y) {
  int a, b, c, d, m, n;
  a = x >> 31;
  b = y >> 31;
  // b = ~b + 1;
  // c = a + b;
  c = a ^ b;
  d = b & 1;
  m = ~x + 1;
  n = m + y;
  n = n >> 31;
  return (!!c & !d) | (!c & !n);
}
//4
/* 
 * logicalNeg - implement the ! operator, using all of 
 *              the legal operators except !
 *   Examples: logicalNeg(3) = 0, logicalNeg(0) = 1
 *   Legal ops: ~ & ^ | + << >>
 *   Max ops: 12
 *   Rating: 4 
 */
int logicalNeg(int x) {
  // int a, b, c, d;
  // a = ~x + 1;
  // b = a ^ x;
  // b = (b >> 31) + 1;
  // c = x << 1;
  // d = (x >> 31) + 1;
  // return (c | d) & b;
  int a, b;
  a = x | (~x + 1);
  b = (a >> 31) + 1;
  return b;
}
/* howManyBits - return the minimum number of bits required to represent x in
 *             two's complement
 *  Examples: howManyBits(12) = 5
 *            howManyBits(298) = 10
 *            howManyBits(-5) = 4
 *            howManyBits(0)  = 1
 *            howManyBits(-1) = 1
 *            howManyBits(0x80000000) = 32
 *  Legal ops: ! ~ & ^ | + << >>
 *  Max ops: 90
 *  Rating: 4
 */
int howManyBits(int x) {
  //也可以先预处理（负数按位取反，正数不变）x = x ^ (x >> 31);之后找最高位1，省去异或和最后一步
  int a, b, c, sum;
  sum = 0;
  a = x >> 31;
  
  b = x >> 16;
  c = !!(a ^ b);
  c = c << 4;
  x = x >> c;
  sum = sum + c;
  
  b = x >> 8;
  c = !!(a ^ b);
  c = c << 3;
  x = x >> c;
  sum = sum + c;

  b = x >> 4;
  c = !!(a ^ b);
  c = c << 2;
  x = x >> c;
  sum = sum + c;

  b = x >> 2;
  c = !!(a ^ b);
  c = c << 1;
  x = x >> c;
  sum = sum + c;

  b = x >> 1;
  c = !!(a ^ b);
  x = x >> c;
  sum = sum + c;
  
  c = !!(x ^ a);
  sum = sum + c;
  return sum + 1;
}
//float
/* 
 * floatScale2 - Return bit-level equivalent of expression 2*f for
 *   floating point argument f.
 *   Both the argument and result are passed as unsigned int's, but
 *   they are to be interpreted as the bit-level representation of
 *   single-precision floating point values.
 *   When argument is NaN, return argument
 *   Legal ops: Any integer/unsigned operations incl. ||, &&. also if, while
 *   Max ops: 30
 *   Rating: 4
 */
unsigned floatScale2(unsigned uf) {
  unsigned a, b, c;
  // exp = (uf >> 23) & 0xFF;
  // frac = uf & 0x7FFFFF;
  // sign = uf & 0x80000000;
  a = (uf >> 31) & 1;
  b = (uf << 1) >> 24;
  c = (uf << 9) >> 9;
  if (b == 0xFF) {
    return uf;
  } else if (b == 0) {
    // (uf << 1) 会把符号位移走，再通过按位或把原符号位补回来
    // return (uf << 1) | (uf & 0x80000000);
    if (c < 0x400000) {
      return (a << 31) + (c << 1);
    } else {
      c = c - 0x400000;
      return (a << 31) + 0x800000 + (c << 1);
    }
  } else {
    b = b + 1;
    return (a << 31)  + (b << 23) + c;
    // return sign | (exp << 23) | frac;
  }
}
/* 
 * floatFloat2Int - Return bit-level equivalent of expression (int) f
 *   for floating point argument f.
 *   Argument is passed as unsigned int, but
 *   it is to be interpreted as the bit-level representation of a
 *   single-precision floating point value.
 *   Anything out of range (including NaN and infinity) should return
 *   0x80000000u.
 *   Legal ops: Any integer/unsigned operations incl. ||, &&. also if, while
 *   Max ops: 30
 *   Rating: 4
 */
int floatFloat2Int(unsigned uf) {
  int exp, E, frac, sign;
  exp = (uf >> 23) & 0xFF;
  frac = uf & 0x7FFFFF;
  sign = uf & 0x80000000;
  
  if (exp == 0xFF) {
    return 0x80000000u;
  }
  if (exp < 127) {
    return 0;
  }
  
  E = exp - 127;
  if (E <= 23) {
    //少了1个0
    frac += 0x800000;
    frac = frac >> (23 - E);
  } else if (E < 31) { 
    frac += 0x800000;
    frac = frac << (E - 23);
  } else {
    return 0x80000000u;
  }
  if (sign) {
    return -frac;
  } else {
    return frac;
  }
}
/* 
 * floatPower2 - Return bit-level equivalent of the expression 2.0^x
 *   (2.0 raised to the power x) for any 32-bit integer x.
 *
 *   The unsigned value that is returned should have the identical bit
 *   representation as the single-precision floating-point number 2.0^x.
 *   If the result is too small to be represented as a denorm, return
 *   0. If too large, return +INF.
 * 
 *   Legal ops: Any integer/unsigned operations incl. ||, &&. Also if, while 
 *   Max ops: 30 
 *   Rating: 4
 */
unsigned floatPower2(int x) {
  int exp;
  if (x >= -149 && x <= -127) {
    exp = 0;
    x = 1 << (x + 149);
    return x;
  }
  if (x < -140) {
    return 0;
  }
  exp = 127 + x;
  if (exp >= 0xFF) {
    return 0x7f800000;
  }
  return exp << 23;
}
