
Main author: [https://github.com/miskcoo/rubik-cube](https://github.com/miskcoo/rubik-cube "https://github.com/miskcoo/rubik-cube")

==The work was modified.==
# Rubik's Cube Solver

A Rubik's Cube solver implemented by C++ and OpenGL. You can use your mouse to rotate the cube.

See [HERE](http://blog.miskcoo.com/2017/03/how-to-reconstruct-rubik-cube)(in Chinese) for more details about the algorithm.

![screenshot](https://raw.githubusercontent.com/miskcoo/rubik-cube/master/demo.gif)

## Requirement

* C++11 support (especially the multi-thread library)
* OpenGL
* GLFW

## Usage

```bash
solver -tra
```
* *-t* maximum thread number used to calculate, the default value is 1.
* *-r* random twist times to generate a cube.
  * When using Krof algorithm, the default value is 15.
  * When using Krociemba algorithm, the default value is 200.
* *-a* specify which algorithm will be used to solve the cube. 
  * Only two algorithms are available: *krof*, *kociemba*. 
  * The default algorithm is kociemba.
