# My Little Compiler

## Intro (personal, not technical)

So I decided to make some compilers. This is primarily an educational project for 
me, so the goal is to cover a lot of breadth and make working programs, not
necessarily of the best quality or optimization. I have a few more specific goals.

1. Get back into C++. 
    - I haven't touched C++ in a long time. Though I used C regularly through 
    college, I never needed C++ except for a small Arduino project.
2. Practice applying theory throughout implementation.
    - I love learning theory and I love building things. On paper, we want to
    extrapolate good design principles from theory and then build according to 
    those. In both school and work, I don't always get the chance to fully reason
    through the best design myself because things need to get done and good 
    principles are already agreed upon.
3. AI, of course
    - My job is one of the more cautious with AI usage, and the platforms we work
    on don't make using AI easy. I want to make more use of coding agents, but
    still maintain caution and verification at every step.
    - So much I see today is demos and prototypes that are great for funding 
    rounds because AI builds for appearances. But they don't always function great
    as full products. I'd like to make my own impression of best practices for
    coding with AI, not too crazed but also not too hesitant.
4. Open source exposure
    - I've always liked the idea of open source development and hope to contribute
    to projects like LLVM one day.
5. Going lower level
    - I'm very interested in working more with instruction sets, programming
    optimization, and thinking about how code actually runs on hardware. In
    this project, I plan to dig into the LLVM optimization passes and backend to
    learn more about connecting high-level computing tasks to optimized instructions.

## Step 1: The LLVM Tutorial

The LLVM tutorial creates a compiler for the toy language Kaleidoscope, and my
following of it can be found in ```./kaleidoscope```.

### Chapters 1, 2, 3
Largely followed the tutorial but skipping extern. Tbh because I just don't care 
about it.

### Chapter 4
Decided to skip optimizatons and JIT in favor of completing the language syntax.
I will return after the later chapters on building your own JIT. 

### Chapter 5
Implemented the lexer, AST, and parser portions without the tutorial.

### Chapters 6, 7
Continued following the tutorial and implementing parts that were familiar (such
as lexer and AST extensions) independently before looking at the reference code.

### Chapter 8
Followed the tutorial, tested a few different functions using the variety of
syntax implemented.