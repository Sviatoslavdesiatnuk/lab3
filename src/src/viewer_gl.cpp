#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX          // silence re-definition warning
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

// GL 1.3 enums missing from the Win-SDK header set
#ifndef GL_MULTISAMPLE
#  define GL_MULTISAMPLE 0x809D
#endif

#include "viewer.h"
#include "virtual_rotate.hpp"
#include <memory>
#include <chrono>
#include <tuple>
#include <queue>
#include <algorithm>
#include <GL/gl.h>
#include <GL/glu.h> // perspective helper
#include <GLFW/glfw3.h>

namespace rubik_cube
{

namespace __viewer_gl_impl
{

class rotate_manager_t {
    using clock_type = std::chrono::system_clock;
    std::chrono::time_point<clock_type> t_s{};
    double  duration{0};
    bool    active {false};
public:
    bool  is_active() const { return active; }
    double get() {
        auto elapsed = std::chrono::duration<double>(clock_type::now() - t_s).count();
        double r = elapsed / duration;
        if (r >= 1.0) active = false;
        return std::min(r, 1.0);
    }
    void set(double d) { duration = d; t_s = clock_type::now(); active = true; }
};

class viewer_gl : public viewer_t {
public:
    viewer_gl();
    ~viewer_gl() override = default;

    // viewer_t API ————————————————————————————————
    bool init(int& argc, char**& argv) override;
    void run() override;
    void set_cube(const cube_t&)   override;
    void set_cube(const cube4_t&)  override;
    void set_rotate_duration(double) override;
    void add_rotate(face_t::face_type, int) override;
    void add_rotate(face_t::face_type, int, int) override;

    // new camera helpers exposed to solver
    void adjust_orbit(float dAlphaDeg, float dBetaDeg) override; // arrows
    void zoom(float factor) override;                             // +/- or wheel

private: // GLFW callbacks
    static void on_resize(GLFWwindow*, int, int);
    static void on_mouse_button(GLFWwindow*, int, int, int);
    static void on_mouse_move(GLFWwindow*, double, double);
    static void on_scroll(GLFWwindow*, double, double);

private:
    template<typename CubeType>
    void draw_cube(const CubeType&);
    void draw_block(GLfloat, GLfloat, GLfloat, GLfloat, block_t, GLenum);
    void update_rotate();
    void set_color(int);

    using rotate_que_t = std::tuple<face_t::face_type,int,int>;
    std::queue<rotate_que_t> rotate_que;

    int      rotate_mask[3] {-1,-1,-1};
    GLfloat  rotate_deg {0.f};
    GLfloat  rotate_vec {0.f};
    rotate_manager_t rotate_mgr;
    double   rotate_duration {1.0};

    virtual_ball_t vball;

    // camera state
    double zoom_factor {1.0};
    float  yaw_deg {0.f};
    float  pitch_deg {0.f};

    int     cube_size {3};
    cube_t  cube;
    cube4_t cube4;

    GLFWwindow* window {nullptr};
};

// ───────────────────────────────────────────────── constructor ────────────
viewer_gl::viewer_gl() = default;

// ───────────────────────────────────────────────── init ————————————
bool viewer_gl::init(int& /*argc*/, char**& /*argv*/)
{
    if(!glfwInit()) return false;

    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_SAMPLES,   4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);          // OpenGL 2.1
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE);
    
    window = glfwCreateWindow(600, 600, "Rubik's Cube", nullptr, nullptr);
    if(!window) { glfwTerminate(); return false; }

    glfwSetWindowUserPointer(window, this);
    glfwSetWindowSizeCallback (window, on_resize);
    glfwSetCursorPosCallback  (window, on_mouse_move);
    glfwSetMouseButtonCallback(window, on_mouse_button);
    glfwSetScrollCallback     (window, on_scroll);

    glfwMakeContextCurrent(window);

    int fbw, fbh;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    on_resize(window, fbw, fbh);

    // glEnable(GL_MULTISAMPLE);
    glEnable(GL_DEPTH_TEST);

    vball.set_rotate(45,{ -1,1,0 });
    return true;
}

// ───────────────────────────────────────────────── run loop —────────────—
void viewer_gl::run()
{
    while(!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        update_rotate();
        (cube_size==3 ? draw_cube(cube) : draw_cube(cube4));
        glfwSwapBuffers(window);
    }
    glfwTerminate();
}

// ───────────────────────────────────────── viewer_t interface ————————
void viewer_gl::set_cube(const cube_t& c){ cube_size=3; cube=c; }
void viewer_gl::set_cube(const cube4_t& c){ cube_size=4; cube4=c; }
void viewer_gl::set_rotate_duration(double d){ rotate_duration=d; }
void viewer_gl::add_rotate(face_t::face_type f,int cnt){ add_rotate(f,1,cnt); }
void viewer_gl::add_rotate(face_t::face_type f,int depth,int cnt){ rotate_que.emplace(f,depth,cnt%4); }

// camera helpers
void viewer_gl::adjust_orbit(float dAlpha,float dBeta){ pitch_deg+=dAlpha; yaw_deg+=dBeta; }
void viewer_gl::zoom(float factor){ zoom_factor = std::clamp(zoom_factor*factor,0.1,10.0); }

// ─────────────────────────────────────────────── rotation mgmt ————————
void viewer_gl::update_rotate()
{
    if(rotate_que.empty()) return;
    auto [ftype,depth,cnt] = rotate_que.front();

    if(!rotate_mgr.is_active()) {
        rotate_mgr.set(rotate_duration);
        rotate_vec = cnt<0? -1:1;
        std::fill(std::begin(rotate_mask),std::end(rotate_mask),-1);
        switch(ftype){
            case face_t::top:    rotate_mask[0]=cube_size-depth; break;
            case face_t::bottom: rotate_mask[0]=depth-1; rotate_vec=-rotate_vec; break;
            case face_t::left:   rotate_mask[2]=depth-1; rotate_vec=-rotate_vec; break;
            case face_t::right:  rotate_mask[2]=cube_size-depth; break;
            case face_t::front:  rotate_mask[1]=cube_size-depth; rotate_vec=-rotate_vec; break;
            case face_t::back:   rotate_mask[1]=depth-1; break;
        }
    }

    rotate_deg = std::abs(cnt)*90.f*rotate_mgr.get();

    if(!rotate_mgr.is_active()) {
        std::fill(std::begin(rotate_mask),std::end(rotate_mask),-1);
        if(cube_size==3) cube.rotate(ftype,cnt); else cube4.rotate(ftype,depth,cnt);
        rotate_que.pop();
    }
}

// ───────────────────────────────────────────── cube drawing ————————
void viewer_gl::set_color(int t){
    static const GLfloat C[7][3]={{0,1,0},{0.3,0.3,1},{1,0.3,0.3},{1,0.5,0},{1,1,0},{1,1,1},{0,0,0}};
    glColor3fv(C[t]);
}

void viewer_gl::draw_block(GLfloat x,GLfloat y,GLfloat z,GLfloat s,block_t col,GLenum mode){
    // back
    set_color(col.back); glBegin(mode);
        glVertex3f(x,y,z); glVertex3f(x,y+s,z); glVertex3f(x+s,y+s,z); glVertex3f(x+s,y,z);
    glEnd();
    // front
    set_color(col.front); glBegin(mode);
        glVertex3f(x,y,z-s); glVertex3f(x,y+s,z-s); glVertex3f(x+s,y+s,z-s); glVertex3f(x+s,y,z-s);
    glEnd();
    // top
    set_color(col.top); glBegin(mode);
        glVertex3f(x,y+s,z); glVertex3f(x+s,y+s,z); glVertex3f(x+s,y+s,z-s); glVertex3f(x,y+s,z-s);
    glEnd();
    // bottom
    set_color(col.bottom); glBegin(mode);
        glVertex3f(x,y,z); glVertex3f(x+s,y,z); glVertex3f(x+s,y,z-s); glVertex3f(x,y,z-s);
    glEnd();
    // left
    set_color(col.left); glBegin(mode);
        glVertex3f(x,y,z); glVertex3f(x,y+s,z); glVertex3f(x,y+s,z-s); glVertex3f(x,y,z-s);
    glEnd();
    // right
    set_color(col.right); glBegin(mode);
        glVertex3f(x+s,y,z); glVertex3f(x+s,y+s,z); glVertex3f(x+s,y+s,z-s); glVertex3f(x+s,y,z-s);
    glEnd();
}

struct rotate_guard {
    bool active;
    rotate_guard(int mask,int idx,GLfloat deg,GLfloat X,GLfloat Y,GLfloat Z):active(mask==idx){ if(active){ glPushMatrix(); glRotatef(deg,X,Y,Z);} }
    ~rotate_guard(){ if(active) glPopMatrix(); }
};

template<typename CubeType>
void viewer_gl::draw_cube(const CubeType& cb){
    const GLfloat s = 0.8f / cube_size;
    glPushMatrix();
    glTranslatef(0,0,static_cast<GLfloat>(-3.0*zoom_factor));
    glRotatef(pitch_deg,1,0,0);
    glRotatef(yaw_deg,0,1,0);
    vball.rotate();
    glLineWidth(1.5f);

    GLfloat base = -s*cube_size*0.5f, x,y,z;
    x=y=base; z=-base;
    for(int i=0;i<cube_size;++i,y+=s,x=base,z=-base){
        rotate_guard g0(rotate_mask[0],i,rotate_deg,0,rotate_vec,0);
        for(int j=0;j<cube_size;++j,z-=s,x=base){
            rotate_guard g1(rotate_mask[1],j,rotate_deg,0,0,rotate_vec);
            for(int k=0;k<cube_size;++k,x+=s){
                rotate_guard g2(rotate_mask[2],k,rotate_deg,rotate_vec,0,0);
                draw_block(x,y,z,s,cb.getBlock(i,j,k),GL_QUADS);
                draw_block(x,y,z,s,{6,6,6,6,6,6},GL_LINE_LOOP);
            }
        }
    }
    glPopMatrix();
}

// ───────────────────────────────────────── GLFW callbacks ————————
void viewer_gl::on_resize(GLFWwindow* win,int w,int h){
    glfwMakeContextCurrent(win);
    glViewport(0,0,w,h);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    gluPerspective(45.0, static_cast<double>(w)/h, 0.1, 100.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
}

void viewer_gl::on_mouse_button(GLFWwindow* win,int button,int action,int){
    if(button!=GLFW_MOUSE_BUTTON_LEFT) return;
    auto* v = static_cast<viewer_gl*>(glfwGetWindowUserPointer(win));
    int w,h; double x,y; glfwGetWindowSize(win,&w,&h); glfwGetCursorPos(win,&x,&y);
    if(action==GLFW_PRESS)  v->vball.set_start (x/w-0.5,y/h-0.5);
    if(action==GLFW_RELEASE) v->vball.set_end   (x/w-0.5,y/h-0.5);
}

void viewer_gl::on_mouse_move(GLFWwindow* win,double x,double y){
    auto* v = static_cast<viewer_gl*>(glfwGetWindowUserPointer(win)); if(!v->vball) return;
    int w,h; glfwGetWindowSize(win,&w,&h); v->vball.set_middle(x/w-0.5,y/h-0.5);
}

void viewer_gl::on_scroll(GLFWwindow* win,double, double yoff){
    auto* v = static_cast<viewer_gl*>(glfwGetWindowUserPointer(win));
    v->zoom(yoff>0?0.9f:1.1f);
}

} // namespace __viewer_gl_impl

std::shared_ptr<viewer_t> create_opengl_viewer(){
    return std::make_shared<__viewer_gl_impl::viewer_gl>();
}

} // namespace rubik_cube
