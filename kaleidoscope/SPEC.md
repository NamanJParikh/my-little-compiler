# Kaleidoscope Language Spec Reference

## Intro
This file contains the various specifications and design decisions made for my
implementation of the Kaleidoscope language. I will update it with capabilities
and syntax as I build the compiler. Considering this is a tutorial and my goal
is to develop foundational knowledge about LLVM, no generative AI will be used
for building this part of the project.

## Basic Syntax

### Variable Mutation
Variables will be mutated using the ```=``` operator. For example,
```
my_var = 5
```

However, note that global variables are not supported. Existing variables such
as function arguments may be mutated with this syntax. Additionally, local
variables may be used using a ```with``` statement. For example,
```
with x, y:
    x = 4
    y = 5
    x + y;
```

### Types
For simplicity, all values will be 64-bit floats, as in the tutorial.

### Operations and Comparisons
Standard numerical operations will be supported. That is, addition (```+```),
subtraction (```-```), multiplication (```*```), and division (```/```).

Since all values are floats, allowed comparisons will similarly be standard
numerical relations. ```<```.

Users may define their own unary and binary operations. For example, 
```
binary > (L, R)
    R < L;
```
```
unary - (value)
    0-value;
```

Currently, operations must be only one character. So, operations like ```==```
are not supported.

### Function Definition
Functions will be defined simply using the ```def``` keyword and standard function
notation of ```<function name>(<inputs>)```. There will be no keyword to indicate
a return; the return value will simply be stated. For example,
```
def add(x, y)
    x + y
```

Note that there is no colon after the definition statement.

### Coniditionals
An ```if```, ```then```, ```else``` pattern as shown below will be followed.
```
def isPositive(x)
    if x < 0 then
        0
    else
        1;
```

### Comments
Commented lines will be begin with ```#```. Multi-line comments will open and 
close with ```#*``` and ```*#```.

### Semicolons
Semicolons must be placed at the end of each expression or function definition
to indicate it is complete.