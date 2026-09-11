# Kaleidoscope Language Spec Reference

## Intro
This file contains the various specifications and design decisions made for my
implementation of the Kaleidoscope language. I will update it with capabilities
and syntax as I build the compiler. Considering this is a tutorial and my goal
is to develop foundational knowledge about LLVM, no generative AI will be used
for building this part of the project.

## Basic Syntax

### Variable Assignment
Variables will be assigned using the ```:=``` operator. For example,
```
my_var := 5
```

### Types
For simplicity, all values will be 64-bit floats, as in the tutorial.

### Operations and Comparisons
Standard numerical operations will be supported. That is, addition (```+```),
subtraction (```-```), multiplication (```*```), division (```/```), integer
division (```//```), and modulus (```%```).

Since all values are floats, allowed comparisons will similarly be standard
numerical relations. ```<```, ```<=```, ```>```, ```>=```, and ```=```.

### Function Definition
Functions will be defined simply using the ```def``` keyword and standard function
notation of ```<function name>(<inputs>)```. There will be no keyword to indicate
a return; the return value will simply be stated. For example,
```
def add(x, y)
    x + y
```

Indentation will not be considered.

### Coniditionals
An ```if```, ```then```, ```else``` pattern as shown below will be followed.
```
def isPositive(x)
    if x >= 0 then
        1
    else
        0
```

### Comments
Commented lines will be begin with ```#```. Multi-line comments will open and 
close with ```#*``` and ```*#```.