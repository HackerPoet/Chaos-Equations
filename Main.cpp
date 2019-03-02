#include <SFML/Graphics.hpp>
#include <SFML/OpenGL.hpp>
#include <iostream>
#include <random>
#include <sstream>
#include <cassert>
#include <fstream>
#include "resource.h"

//The dreaded windows include file...
#define WIN32_LEAN_AND_MEAN //Reduce compile time of windows.h
#include <Windows.h>
#undef min
#undef max

//Global constants
static const int num_params = 18;
static const int iters = 800;
static const int steps_per_frame = 500;
static const double delta_per_step = 1e-5;
static const double delta_minimum = 1e-7;
static const double t_start = -3.0;
static const double t_end = 3.0;
static const bool fullscreen = false;

//Global variables
static int window_w = 1600;
static int window_h = 900;
static int window_bits = 24;
static float plot_scale = 0.25f;
static float plot_x = 0.0f;
static float plot_y = 0.0f;
static std::mt19937 rand_gen;
static sf::Font font;
static sf::Text equ_text;
static std::string equ_code;
static sf::RectangleShape equ_box;
static sf::Text t_text;
static sf::RectangleShape t_box;

static sf::Color GetRandColor(int i) {
  i += 1;
  int r = std::min(255, 50 + (i * 11909) % 256);
  int g = std::min(255, 50 + (i * 52973) % 256);
  int b = std::min(255, 50 + (i * 44111) % 256);
  return sf::Color(r, g, b, 16);
}

static sf::Vector2f ToScreen(double x, double y) {
  const float s = plot_scale * float(window_h / 2);
  const float nx = float(window_w) * 0.5f + (float(x) - plot_x) * s;
  const float ny = float(window_h) * 0.5f + (float(y) - plot_y) * s;
  return sf::Vector2f(nx, ny);
}

static void RandParams(double* params) {
  std::uniform_int_distribution<int> rand_int(0, 3);
  for (int i = 0; i < num_params; ++i) {
    const int r = rand_int(rand_gen);
    if (r == 0) {
      params[i] = 1.0f;
    } else if (r == 1) {
      params[i] = -1.0f;
    } else {
      params[i] = 0.0f;
    }
  }
}

static std::string ParamsToString(const double* params) {
  const char base27[] = "_ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  static_assert(num_params % 3 == 0, "Params must be a multiple of 3");
  int a = 0;
  int n = 0;
  std::string result;
  for (int i = 0; i < num_params; ++i) {
    a = a*3 + int(params[i]) + 1;
    n += 1;
    if (n == 3) {
      result += base27[a];
      a = 0;
      n = 0;
    }
  }
  return result;
}

static void StringToParams(const std::string& str, double* params) {
  for (int i = 0; i < num_params/3; ++i) {
    int a = 0;
    const char c = (i < str.length() ? str[i] : '_');
    if (c >= 'A' && c <= 'Z') {
      a = int(c - 'A') + 1;
    } else if (c >= 'a' && c <= 'z') {
      a = int(c - 'a') + 1;
    }
    params[i*3 + 2] = double(a % 3) - 1.0;
    a /= 3;
    params[i*3 + 1] = double(a % 3) - 1.0;
    a /= 3;
    params[i*3 + 0] = double(a % 3) - 1.0;
  }
}

static sf::RectangleShape MakeBoundsShape(const sf::Text& text) {
  sf::RectangleShape blackBox;
  const sf::FloatRect textBounds = text.getGlobalBounds();
  blackBox.setPosition(textBounds.left, textBounds.top);
  blackBox.setSize(sf::Vector2f(textBounds.width, textBounds.height));
  blackBox.setFillColor(sf::Color::Black);
  return blackBox;
}

#define SIGN_OR_SKIP(i, x) \
  if (params[i] != 0.0) { \
    if (isFirst) { \
      if (params[i] == -1.0) ss << "-"; \
    } else { \
      if (params[i] == -1.0) ss << " - "; \
      else ss << " + "; \
    } \
    ss << x; \
    isFirst = false; \
  } 
static std::string MakeEquationStr(double* params) {
  std::stringstream ss;
  bool isFirst = true;
  SIGN_OR_SKIP(0, "x\u00b2");
  SIGN_OR_SKIP(1, "y\u00b2");
  SIGN_OR_SKIP(2, "t\u00b2");
  SIGN_OR_SKIP(3, "xy");
  SIGN_OR_SKIP(4, "xt");
  SIGN_OR_SKIP(5, "yt");
  SIGN_OR_SKIP(6, "x");
  SIGN_OR_SKIP(7, "y");
  SIGN_OR_SKIP(8, "t");
  return ss.str();
}

static void ResetPlot() {
  plot_scale = 0.25f;
  plot_x = 0.0f;
  plot_y = 0.0f;
}

static void GenerateNew(sf::RenderWindow& window, double& t, double* params) {
  t = t_start;
  equ_code = ParamsToString(params);
  const std::string equation_str =
    "x' = " + MakeEquationStr(params) + "\n"
    "y' = " + MakeEquationStr(params + num_params / 2) + "\n"
    "Code: " + equ_code;
  equ_text.setCharacterSize(30);
  equ_text.setFont(font);
  equ_text.setString(equation_str);
  equ_text.setFillColor(sf::Color::White);
  equ_text.setPosition(10.0f, 10.0f);
  equ_box = MakeBoundsShape(equ_text);
  window.clear();
}

static void MakeTText(double t) {
  t_text.setCharacterSize(30);
  t_text.setFont(font);
  t_text.setString("t = " + std::to_string(t));
  t_text.setFillColor(sf::Color::White);
  t_text.setPosition(window_w - 200.0f, 10.0f);
  t_box = MakeBoundsShape(t_text);
}

static void CreateRenderWindow(sf::RenderWindow& window) {
  //GL settings
  sf::ContextSettings settings;
  settings.depthBits = 24;
  settings.stencilBits = 8;
  settings.antialiasingLevel = 8;
  settings.majorVersion = 3;
  settings.minorVersion = 0;

  //Create the window
  const sf::VideoMode screenSize(window_w, window_h, window_bits);
  window.create(screenSize, "Chaos Equations", (fullscreen ? sf::Style::Fullscreen : sf::Style::Close), settings);
  window.setFramerateLimit(60);
  window.setVerticalSyncEnabled(true);
  window.setActive(false);
  window.requestFocus();
}

static void CenterPlot(const std::vector<sf::Vector2f>& history) {
  float min_x = FLT_MAX;
  float max_x = -FLT_MAX;
  float min_y = FLT_MAX;
  float max_y = -FLT_MAX;
  for (size_t i = 0; i < history.size(); ++i) {
    min_x = std::fmin(min_x, history[i].x);
    max_x = std::fmax(max_x, history[i].x);
    min_y = std::fmin(min_y, history[i].y);
    max_y = std::fmax(max_y, history[i].y);
  }
  max_x = std::fmin(max_x, 4.0f);
  max_y = std::fmin(max_y, 4.0f);
  min_x = std::fmax(min_x, -4.0f);
  min_y = std::fmax(min_y, -4.0f);
  plot_x = (max_x + min_x) * 0.5f;
  plot_y = (max_y + min_y) * 0.5f;
  plot_scale = 1.0f / std::max(std::max(max_x - min_x, max_y - min_y) * 0.6f, 0.1f);
}

struct Res {
  Res(int id) {
    HRSRC src = ::FindResource(NULL, MAKEINTRESOURCE(id), RT_RCDATA);
    ptr = ::LockResource(::LoadResource(NULL, src));
    size = (size_t)::SizeofResource(NULL, src);
  }
  void* ptr;
  size_t size;
};

int main(int argc, char *argv[]) {
  std::cout << "=========================================================" << std::endl;
  std::cout << std::endl;
  std::cout << "                      Chaos Equations" << std::endl;
  std::cout << std::endl;
  std::cout << "    These are plots of random recursive equations, which" << std::endl;
  std::cout << "often produce chaos, and results in beautiful patterns." << std::endl;
  std::cout << "For every time t, a point (x,y) is initialized to (t,t)." << std::endl;
  std::cout << "The equation is applied to the point many times, and each" << std::endl;
  std::cout << "iteration is drawn in a unique color." << std::endl;
  std::cout << std::endl;
  std::cout << "=========================================================" << std::endl;
  std::cout << std::endl;
  std::cout << "Controls:" << std::endl;
  std::cout << "      'A' - Automatic Mode (randomize equations)" << std::endl;
  std::cout << "      'R' - Repeat Mode (keep same equation)" << std::endl;
  std::cout << std::endl;
  std::cout << "      'C' - Center points" << std::endl;
  std::cout << "      'D' - Dot size Toggle" << std::endl;
  std::cout << "      'I' - Iteration Limit Toggle" << std::endl;
  std::cout << "      'T' - Trail Toggle" << std::endl;
  std::cout << std::endl;
  std::cout << "      'P' - Pause" << std::endl;
  std::cout << " 'LShift' - Slow Down" << std::endl;
  std::cout << " 'RShift' - Speed Up" << std::endl;
  std::cout << "  'Space' - Reverse" << std::endl;
  std::cout << std::endl;
  std::cout << "     'N' - New Equation (random)" << std::endl;
  std::cout << "     'L' - Load Equation" << std::endl;
  std::cout << "     'S' - Save Equation" << std::endl;
  std::cout << std::endl;

  //Set random seed
  rand_gen.seed((unsigned int)time(0));

  //Load the font
  const Res res_font(IDR_FONT);
  if (!font.loadFromMemory(res_font.ptr, res_font.size)) {
    std::cerr << "FATAL: Failed to load font." << std::endl;
    system("pause");
    return 1;
  }

  //Create the window
  const sf::VideoMode screenSize = sf::VideoMode::getDesktopMode();
  window_bits = screenSize.bitsPerPixel;
  if (fullscreen) {
    window_w = screenSize.width;
    window_h = screenSize.height;
  }
  sf::RenderWindow window;
  CreateRenderWindow(window);

  //Simulation variables
  double t = t_start;
  std::vector<sf::Vector2f> history(iters);
  double rolling_delta = delta_per_step;
  double params[num_params];
  double speed_mult = 1.0;
  bool paused = false;
  int trail_type = 0;
  int dot_type = 0;
  bool load_started = false;
  bool shuffle_equ = true;
  bool iteration_limit = false;

  //Setup the vertex array
  std::vector<sf::Vertex> vertex_array(iters*steps_per_frame);
  for (size_t i = 0; i < vertex_array.size(); ++i) {
    vertex_array[i].color = GetRandColor(i % iters);
  }

  //Initialize random parameters
  ResetPlot();
  RandParams(params);
  GenerateNew(window, t, params);

  //Main Loop
  while (true) {
    while (window.isOpen()) {
      sf::Event event;
      while (window.pollEvent(event)) {
        if (event.type == sf::Event::Closed) {
          window.close();
          break;
        } else if (event.type == sf::Event::KeyPressed) {
          const sf::Keyboard::Key keycode = event.key.code;
          if (keycode == sf::Keyboard::Escape) {
            window.close();
            break;
          } else if (keycode == sf::Keyboard::A) {
            shuffle_equ = true;
          } else if (keycode == sf::Keyboard::C) {
            CenterPlot(history);
          } else if (keycode == sf::Keyboard::D) {
            dot_type = (dot_type + 1) % 3;
          } else if (keycode == sf::Keyboard::I) {
            iteration_limit = !iteration_limit;
          } else if (keycode == sf::Keyboard::L) {
            shuffle_equ = false;
            load_started = true;
            paused = false;
            window.close();
          } else if (keycode == sf::Keyboard::N) {
            ResetPlot();
            RandParams(params);
            GenerateNew(window, t, params);
          } else if (keycode == sf::Keyboard::P) {
            paused = !paused;
          } else if (keycode == sf::Keyboard::R) {
            shuffle_equ = false;
          } else if (keycode == sf::Keyboard::S) {
            std::ofstream fout("saved.txt", std::ios::app);
            fout << equ_code << std::endl;
            std::cout << "Saved: " << equ_code << std::endl;
          } else if (keycode == sf::Keyboard::T) {
            trail_type = (trail_type + 1) % 4;
          }
        }
      }

      //Change simulation speed if using shift modifiers
      if (sf::Keyboard::isKeyPressed(sf::Keyboard::LShift)) {
        speed_mult = 0.1;
      } else if (sf::Keyboard::isKeyPressed(sf::Keyboard::RShift)) {
        speed_mult = 10.0;
      } else {
        speed_mult = 1.0;
      }
      if (sf::Keyboard::isKeyPressed(sf::Keyboard::Space)) {
        speed_mult = -speed_mult;
      }

      //Skip all drawing if paused
      if (paused) {
        window.display();
        continue;
      }

      //Automatic restart
      if (t > t_end) {
        if (shuffle_equ) {
          ResetPlot();
          RandParams(params);
        }
        GenerateNew(window, t, params);
      }

      sf::BlendMode fade(sf::BlendMode::One, sf::BlendMode::One, sf::BlendMode::ReverseSubtract);
      sf::RenderStates renderBlur(fade);

      sf::RectangleShape fullscreen_rect;
      fullscreen_rect.setPosition(0.0f, 0.0f);
      fullscreen_rect.setSize(sf::Vector2f(window_w, window_h));

      static const sf::Uint8 fade_speeds[] = { 10,2,0,255 };
      const sf::Uint8 fade_speed = fade_speeds[trail_type];
      if (fade_speed >= 1) {
        fullscreen_rect.setFillColor(sf::Color(fade_speed, fade_speed, fade_speed, 0));
        window.draw(fullscreen_rect, renderBlur);
      }

      //Apply chaos and draw
      const int steps = steps_per_frame;
      const double delta = delta_per_step * speed_mult;
      rolling_delta = rolling_delta*0.99 + delta*0.01;

      for (int step = 0; step < steps; ++step) {
        bool isOffScreen = true;
        double x = t;
        double y = t;

        sf::CircleShape point;
        point.setRadius(0.5f);
        point.setOrigin(point.getRadius(), point.getRadius());
        for (int iter = 0; iter < iters; ++iter) {
          const double xx = x * x;
          const double yy = y * y;
          const double tt = t * t;
          const double xy = x * y;
          const double xt = x * t;
          const double yt = y * t;
          const double nx = xx*params[0] + yy*params[1] + tt*params[2] + xy*params[3] + xt*params[4] + yt*params[5] + x*params[6] + y*params[7] + t*params[8];
          const double ny = xx*params[9] + yy*params[10] + tt*params[11] + xy*params[12] + xt*params[13] + yt*params[14] + x*params[15] + y*params[16] + t*params[17];
          x = nx;
          y = ny;
          sf::Vector2f screenPt = ToScreen(x, y);
          if (iteration_limit && iter < 100) {
            screenPt.x = FLT_MAX;
            screenPt.y = FLT_MAX;
          }
          vertex_array[step*iters + iter].position = screenPt;

          //Check if dynamic delta should be adjusted
          if (screenPt.x > 0.0f && screenPt.y > 0.0f && screenPt.x < window_w && screenPt.y < window_h) {
            const float dx = history[iter].x - float(x);
            const float dy = history[iter].y - float(y);
            const double dist = double(500.0f * std::sqrt(dx*dx + dy*dy));
            rolling_delta = std::min(rolling_delta, std::max(delta / (dist + 1e-5), delta_minimum*speed_mult));
            isOffScreen = false;
          }
          history[iter].x = float(x);
          history[iter].y = float(y);
        }

        //Update the t variable
        if (isOffScreen) {
          t += 0.01;
        } else {
          t += rolling_delta;
        }
      }

      //Draw new points
      static const float dot_sizes[] = { 1.0f, 3.0f, 10.0f };
      glEnable(GL_POINT_SMOOTH);
      glPointSize(dot_sizes[dot_type]);
      window.draw(vertex_array.data(), vertex_array.size(), sf::PrimitiveType::Points);

      //Draw the equation
      window.draw(equ_box);
      window.draw(equ_text);

      //Draw the current t-value
      MakeTText(t);
      window.draw(t_box);
      window.draw(t_text);

      //Flip the screen buffer
      window.display();
    }

    if (load_started) {
      std::string code;
      std::cout << "Enter 6 letter code:" << std::endl;
      std::cin >> code;
      CreateRenderWindow(window);
      ResetPlot();
      StringToParams(code, params);
      GenerateNew(window, t, params);
      load_started = false;
    } else {
      break;
    }
  }

  return 0;
}
