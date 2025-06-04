// interactive_cube_solver.cpp — full camera‑safe version
// ------------------------------------------------------
// * Arrow keys orbit camera, + / − zoom
// * Mouse drag (track‑ball) & wheel preserved because we don’t overwrite GLFW user‑pointer
// * Face keys 1‑6, Enter scramble, Space solve, Esc quit
// * Modern C++20, RAII, robust error handling

#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <string_view>

#include "algo.h"
#include "viewer.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

// ───────────────────────────── constants ────────────────────────────────
namespace {
using namespace rubik_cube;
constexpr std::array<char, 6> kFaceStr{'U', 'D', 'F', 'B', 'L', 'R'};
constexpr std::string_view   kDataFile{"krof.dat"};

// ───────────────────────────── helpers ───────────────────────────────────
struct CallbackData {
    std::shared_ptr<viewer_t> viewer;
    cube_t* cube{};
    int  scrambleMoves{20};
    bool* isScrambling{};
    bool* isSolving{};
};

std::shared_ptr<algo_t> gSolver; // shared between main & callback
CallbackData            gCb;     // global (simplifies callbacks)

[[nodiscard]] bool fileExists(std::string_view path) {
    std::ifstream f{path.data()};
    return f.good();
}

void printRotate(int faceIdx, int times) {
    std::cout << kFaceStr[faceIdx];
    if (times > 1)
        std::cout << (times == 2 ? '2' : '\'');
}

[[noreturn]] void usage(std::string_view msg = {}) {
    if (!msg.empty())
        std::cerr << "Error: " << msg << '\n';
    std::cout << R"(Usage: ./solver -t <threads> -a <kociemba|krof> -s <scrambleMoves>
Options:
  -t   threads to use         [1-32, default: 1]
  -a   algorithm              [kociemba|krof, default: kociemba]
  -s   scramble move count    [0-100, default: 20]

Controls (window):
  1-6     rotate faces (U D F B L R)
  ↑↓←→    orbit camera
  + / -   zoom in / out
  Mouse   drag to rotate, wheel to zoom
  Enter   scramble cube
  Space   solve cube
  Esc     exit
)";
    std::exit(EXIT_FAILURE);
}

// ─────────────────────── scramble routine ───────────────────────────────
void scrambleCube(cube_t& cube, const std::shared_ptr<viewer_t>& viewer,
                  int moves, std::mt19937& rng, bool& isScrambling) {
    std::uniform_int_distribution<int> faceDist(0, 5);
    std::uniform_int_distribution<int> rotDist(1, 3);

    isScrambling = true;
    std::cout << "\nScrambling with " << moves << " random moves:\n";

    for (int i = 0; i < moves; ++i) {
        int  faceIdx  = faceDist(rng);
        int  rotation = rotDist(rng);
        auto face     = static_cast<face_t::face_type>(faceIdx);

        cube.rotate(face, rotation);
        viewer->add_rotate(face, rotation);

        printRotate(faceIdx, rotation);
        std::cout << ' ';
    }
    std::cout << "\n\nScramble complete. Press Space to solve." << std::endl;
    isScrambling = false;
}

// ─────────────────────── GLFW key callback ───────────────────────────────
void keyCallback(GLFWwindow* /*win*/, int key, int, int action, int) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT)
        return;

    auto& cube   = *gCb.cube;
    auto  viewer = gCb.viewer;
    bool& scr    = *gCb.isScrambling;
    bool& sol    = *gCb.isSolving;

    if (key == GLFW_KEY_ESCAPE) {
        glfwSetWindowShouldClose(glfwGetCurrentContext(), GLFW_TRUE);
        return;
    }

    if (scr || sol) {
        return; // ignore inputs during animation
    }

    // camera controls first
    switch (key) {
        case GLFW_KEY_LEFT:   viewer->adjust_orbit(0.f, -5.f); return;
        case GLFW_KEY_RIGHT:  viewer->adjust_orbit(0.f,  5.f); return;
        case GLFW_KEY_UP:     viewer->adjust_orbit(-5.f, 0.f); return;
        case GLFW_KEY_DOWN:   viewer->adjust_orbit( 5.f, 0.f); return;
        case GLFW_KEY_KP_ADD:
        case GLFW_KEY_EQUAL:  viewer->zoom(0.9f);              return;
        case GLFW_KEY_KP_SUBTRACT:
        case GLFW_KEY_MINUS:  viewer->zoom(1.1f);              return;
        default: break;
    }

    static thread_local std::mt19937 rng{std::random_device{}()};

    // scramble
    if (key == GLFW_KEY_ENTER) {
        scrambleCube(cube, viewer, gCb.scrambleMoves, rng, scr);
        return;
    }

    // manual face rotation
    if (key >= GLFW_KEY_1 && key <= GLFW_KEY_6) {
        int faceIdx = key - GLFW_KEY_1;
        auto face   = static_cast<face_t::face_type>(faceIdx);

        cube.rotate(face, 1);
        viewer->set_cube(cube);

        std::cout << "Rotated face: ";
        printRotate(faceIdx, 1);
        std::cout << '\n';
        return;
    }

    // solve
    if (key == GLFW_KEY_SPACE) {
        sol = true;
        std::cout << "\nCalculating optimal solution..." << std::endl;

        auto solution = gSolver->solve(cube);

        std::cout << "\nSolution (" << solution.size() << " moves):\n";
        for (auto [f, r] : solution) {
            printRotate(static_cast<int>(f), (r % 4 + 4) & 3);
            std::cout << ' ';
        }
        std::cout << std::endl;

        cube_t before = cube;
        viewer->set_cube(before);
        for (auto [f, r] : solution) {
            viewer->add_rotate(f, r);
            cube.rotate(f, r);
        }
        sol = false;
    }
}

} // namespace (anonymous)

// ──────────────────────────────── main ────────────────────────────────────
int main(int argc, char** argv) try {
    // parse CLI
    std::map<std::string, std::string> arg;
    for (int i = 1; i < argc; ++i) {
        std::string key{argv[i]};
        if (key.size() != 2 || key[0] != '-') usage();
        if (++i >= argc) usage("missing value for option " + key);
        arg.emplace(key.substr(1), argv[i]);
    }

    const std::string algo     = arg.contains("a") ? arg.at("a") : "kociemba";
    const int         threads  = arg.contains("t") ? std::stoi(arg.at("t")) : 1;
    const int         scramble = arg.contains("s") ? std::stoi(arg.at("s")) : 20;

    if (!(algo == "kociemba" || algo == "krof")) usage("invalid algorithm");
    if (threads < 1 || threads > 32) usage("threads 1-32 only");
    if (scramble < 0 || scramble > 100) usage("scramble 0-100 only");

    // build solver
    if (algo == "krof") {
        gSolver = create_krof_algo(threads);
        if (fileExists(kDataFile)) {
            std::cout << "Reading data file..." << std::endl;
            gSolver->init(kDataFile.data());
        } else {
            std::cout << "Initialising heuristic tables (may take a while)..." << std::endl;
            gSolver->init();
            gSolver->save(kDataFile.data());
        }
    } else {
        gSolver = create_kociemba_algo(threads);
        std::cout << "Initialising heuristic tables..." << std::endl;
        gSolver->init();
    }

    // viewer
    cube_t cube; // solved
    auto viewer = create_opengl_viewer();
    viewer->init(argc, argv);
    viewer->set_rotate_duration(0.5);
    viewer->set_cube(cube);

    bool scrambling = false;
    bool solving    = false;
    gCb = {viewer, &cube, scramble, &scrambling, &solving};

    // register only key callback; viewer keeps its own ptr for mouse
    glfwSetKeyCallback(glfwGetCurrentContext(), keyCallback);

    std::cout << "\nInteractive Rubik's Cube Solver\n--------------------------------\n"
                 "Controls:\n"
                 "  1-6     rotate faces (U D F B L R)\n"
                 "  ↑↓←→    orbit camera, +/− zoom\n"
                 "  Enter   scramble cube\n"
                 "  Space   solve cube\n"
                 "  Esc     exit\n";

    viewer->run();
    return EXIT_SUCCESS;

} catch (const std::exception& ex) {
    std::cerr << "Fatal: " << ex.what() << '\n';
    return EXIT_FAILURE;
}
