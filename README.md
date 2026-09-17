# My Little Compiler

## Intro (personal, not technical)

So I decided to make a compiler. This is primarily an educational project for me,
so I make no claims as to the quality or optimization. The goals for me here are

1. Get back into C++. 
    - I haven't touched C++ in a long time. Never needed it in college outside a 
    small Arduino project. 
2. Bridge the gap from theory to implementation
    - I love learning theory and I love building things, but often I find when you
    start implementing, the theory gets swept under the rug. Theory runs deep with
    compilers, so I'm hoping this project will give me a chance at some more
    theory-conscious programming. Design decisions informed by an understanding 
    of theory principles, that's how I want to build. 
3. Learn and build in harmony with new tools
    - I remember when I would think "I want to learn ____" and proceeded in a 
    frenzy of Googling, collecting resources, exploring different branches, and 
    experimenting hands-on. The goal was never to build a complete product, but 
    somehow it ended up being pretty close to complete and certainly something 
    to be proud of and even use. So much I see today is demos and prototypes that 
    are great for funding rounds because AI builds for appearances. I'd like to
    see how much I can do with AI while maintaining focus on functionality.
4. Open source exposure
    - I've always liked the idea of open source development and hope to contribute
    to projects like LLVM one day. Consider this project my introduction to LLVM
    and preparation to dig deeper into the guts of LLVM's representations and 
    optimizations.
5. Going lower level
    - I'm very interested in working more with instruction sets, programming
    optimization, and thinking about how code actually runs on hardware. Through
    this project, I will dig into the LLVM optimization passes and backend to
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