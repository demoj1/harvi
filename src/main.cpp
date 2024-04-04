#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <ratio>
#include <sstream>
#include <unistd.h>
#include <thread>
#include <raylib.h>
#include <date/date.h>
#include <chrono>
#include "./font.h"

#define JSON_HAS_RANGES 0
#include "./json.hpp"
using json = nlohmann::json;

using namespace std;
using datetime = date::sys_time<std::chrono::milliseconds>;
using duration = chrono::duration<long long, ratio<1, 1000>>;

Font global_font;

typedef enum Align {
  CENTER,
  START,
  END
};

Vector2 DrawTextExx(string text, Vector2 position, Color tint) {
  auto m = MeasureTextEx(global_font, text.c_str(), 20, 0.4);
  DrawTextEx(global_font, text.c_str(), {position.x, position.y}, 20, 0.4, tint);

  return m;
}

Vector2 DrawTextExxCenter(string str, Rectangle rect, Color tint, Align align = CENTER) {
  auto m = MeasureTextEx(global_font, str.c_str(), 20, 0.4);

  if (align == CENTER) {
    return DrawTextExx(str, {(rect.x + rect.width/2) - m.x/2, (rect.y + rect.height/2) - m.y/2}, tint);
  }

  if (align == START) {
    return DrawTextExx(str, {rect.x, (rect.y + rect.height/2) - m.y/2}, tint);
  }

  assert(false);
}

inline int ParseInt(const char* value)
{
    return std::strtol(value, nullptr, 10);
}

datetime ParseISO8601(const std::string& input)
{
    std::istringstream in{input};
    datetime tp;
    in >> date::parse("%FT%TZ", tp);
    if (in.fail())
    {
        in.clear();
        in.exceptions(std::ios::failbit);
        in.str(input);
        in >> date::parse("%FT%T%Ez", tp);
    }
    return tp;
}

bool check_json_mime(string mime_type) {
  return mime_type == "application/json; charset=utf-8"
      || mime_type == "application/json"
      || mime_type == "application/json;charset=utf-8";
}

template <typename T>
constexpr T _map(T x, T in_min, T in_max, T out_min, T out_max) noexcept {
  #ifdef DEBUG
  // printf("(x:%f - in_min:%f) * (out_max:%f - out_min:%f) / (in_max:%f - in_min:%f) + out_min:%f", x, in_min, out_max, out_min, in_max, in_min, out_min);
  fflush(stdout);
  #endif

  return ((T)x - (T)in_min) * ((T)out_max - (T)out_min) / ((T)in_max - (T)in_min) + (T)out_min;
}

string pretty_bytes(uint bytes) {
    stringstream sm;

    const char* suffixes[7];
    suffixes[0] = "B";
    suffixes[1] = "KB";
    suffixes[2] = "MB";
    suffixes[3] = "GB";
    suffixes[4] = "TB";
    suffixes[5] = "PB";
    suffixes[6] = "EB";
    uint s = 0; // which suffix to use
    double count = bytes;
    while (count >= 1024 && s < 7)
    {
        s++;
        count /= 1024;
    }

    if (count - floor(count) == 0.0)
      sm << count << suffixes[s];
    else
      sm << fixed << setprecision(2) << count << suffixes[s];

    return sm.str();
}

class HarEntry {
public:
  datetime strated_date_time;
  datetime ended_date_time;
  duration duration;

  string request__url;
  string request__method;
  string request__content;

  string response__content;
  string response__error;
  int response__status;
  int response__body_size;

  float time;
  float time__blocked;
  float time__dns;
  float time__connect;
  float time__send;
  float time__wait;
  float time__receive;
  float time__ssl;

  bool collapse;
  bool hover;

public:
  HarEntry(json entry) {
    time = entry["time"];

    if (time != 0) {
      time__blocked = entry["timings"]["blocked"];
      time__dns = entry["timings"]["dns"];
      time__connect = entry["timings"]["connect"];
      time__send = entry["timings"]["send"];
      time__wait = entry["timings"]["wait"];
      time__receive = entry["timings"]["receive"];
      time__ssl = entry["timings"]["ssl"];
    }

    string _started_date_time = entry["startedDateTime"];
    strated_date_time = ParseISO8601(_started_date_time);
    ended_date_time = ParseISO8601(_started_date_time) + chrono::milliseconds((int)time);
    duration = ended_date_time - strated_date_time;

    request__url = entry["request"]["url"];
    request__method = entry["request"]["method"];

    try {
      if (request__method == "POST" && entry["request"]["postData"].is_object() && entry["request"]["postData"]["mimeType"].is_string()) {
        if (check_json_mime(entry["request"]["postData"]["mimeType"]))
          request__content = json::parse(entry["request"]["postData"]["text"].get<string>()).dump(4);
        else
          request__content = entry["request"]["postData"]["text"];
      } else {
        request__content = "<EMPTY>";
      }
    } catch (...) {
      request__content = "<ERROR>";
    }

    if (entry["response"].is_object()) {
      if (entry["response"]["_error"].is_string()) response__error = entry["response"]["_error"];
      if (entry["response"]["status"].is_number()) response__status = entry["response"]["status"];
      else response__status = 0;
      if (entry["response"]["bodySize"].is_number()) response__body_size = entry["response"]["bodySize"];

      try {
        int bodySize = 0;
        if (entry["response"]["bodySize"].is_number()) bodySize = entry["response"]["bodySize"];
        int size = 0;
        if (entry["response"]["content"].is_object() && entry["response"]["content"]["size"].is_number()) bodySize = entry["response"]["content"]["size"];

        if (entry["response"]["content"].is_object()
         && entry["response"]["content"]["text"].is_string()
         && (bodySize > 0 || size > 0)
         && entry["response"]["content"]["text"] != "") {
          if (check_json_mime(entry["response"]["content"]["mimeType"]))
            response__content = json::parse(entry["response"]["content"]["text"].get<string>()).dump(4);
          else
            response__content = entry["response"]["content"]["text"];
        } else {
          response__content = "<EMPTY>";
        }
      } catch (const std::exception& ex) {
        response__content = ex.what();
      }
    }

    collapse = false;
    hover = false;
  }

  float render(Rectangle b, float start_x, float width) {
    float height = 40;

    if (b.y > GetScreenHeight() + 150) return 0.0;
    if (b.y < 0) return 40;

    if (CheckCollisionPointRec(GetMousePosition(), {0, b.y, static_cast<float>(GetScreenWidth()), height-1})) {
      hover = true;
      if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        collapse = !collapse;
      }
    } else {
      hover = false;
    }

    Color color = MAGENTA;
    if (response__status / 100 == 2) {
      color = GREEN;
    } else if (response__status / 100 == 3) {
      color = ORANGE;
    } else {
      color = RED;
    }

    if (collapse) {
      DrawRectangleRec({0, b.y, static_cast<float>(GetScreenWidth()), height-1}, BLUE);
    } else {
      DrawRectangleLinesEx({0, b.y, static_cast<float>(GetScreenWidth()), height-1}, hover ? 3 : 1, BLUE);
    }

    Rectangle rect = {start_x, b.y + 4, width < 3 ? 3 : width, height - 8};
    DrawRectangleRec(rect, color);

    // std::chrono::current_zone()->to_local(
    //         std::chrono::system_clock::now())).str().substr(0, 23);
    // }

    stringstream sm;
    sm << request__method << " "
      << response__status << " [duration: " << duration
                                            << ", size: "
                                            << pretty_bytes(response__body_size)
                                            << ", at: "
                                            << chrono::current_zone()->to_local(strated_date_time)
                                            << "]";

    if (response__error != "")
      sm << " ERROR: " << response__error;

    sm << " " << request__url;

    Color textColor = collapse ? WHITE : BLACK;
    auto m = DrawTextExxCenter(
      sm.str(),
      {rect.x + 5, rect.y, rect.width - 5, rect.height},
      textColor,
      Align::START);

    if (rect.x + m.x < 0) DrawCircle(0, b.y + height/2, 5.0, DARKBLUE);
    if (start_x > GetScreenWidth()) DrawCircle(GetScreenWidth(), b.y + height/2, 5.0, DARKBLUE);

    if (collapse) {
      stringstream sm;
      sm << ">>> info: " << endl
         << "started at " << chrono::current_zone()->to_local(strated_date_time) << " (localtime)" << endl
         << "started at " << strated_date_time << " (UTC)"
         << endl;

      height += DrawTextExx(sm.str(), {rect.x + 3, b.y + height}, BLACK).y;

      sm = stringstream();
      sm << ">>> timings: "
         << "blocked " << time__blocked << "ms, "
         << "dns " << time__dns << "ms, "
         << "connect " << time__connect << "ms, "
         << "ssl " << time__ssl << "ms, "
         << "send " << time__send << "ms, "
         << "wait " << time__wait << "ms, "
         << "receive " << time__receive << "ms"
         << endl;

      height += DrawTextExx(sm.str(), {rect.x + 3, b.y + height}, BLACK).y;

      height += DrawTextExx(">>> REQUEST: ", {rect.x + 3, b.y + height}, BLACK).y;
      height += DrawTextExx((request__content + "\n").c_str(), {rect.x + 15, b.y + height}, BLACK).y;
      DrawLine(rect.x, b.y, rect.x, b.y + height, BLACK);

      height += DrawTextExx(">>> RESPONSE: ", {rect.x + 3, b.y + height}, BLACK).y;
      height += DrawTextExx((response__content + "\n").c_str(), {rect.x + 15, b.y + height}, BLACK).y;
      DrawLine(rect.x, b.y, rect.x, b.y + height, BLACK);
    }

    return height - 2;
  }
};

struct wProgressBar {
  string title;

  float progress;
  float target_progress;

  wProgressBar(string title, float target_progress) {
    this->title = title;
    this->target_progress = target_progress;
    this->progress = 0.0;
  }

  void set_progress(float target_progress) {
    this->target_progress = target_progress;
  }

  void set_title(string title) {
    this->title = title;
  }

  void render(Rectangle b) {
    DrawRectangleRec({b.x, b.y, b.width * progress, b.height}, DARKGREEN);
    DrawRectangleLinesEx(b, 2.0, BLUE);
    DrawTextExxCenter(this->title, b, BLACK);

    if (fabs(progress - target_progress) > 0.02f) {
      if (progress < target_progress) progress += target_progress * GetFrameTime() * 3;
      else progress -= target_progress * GetFrameTime() * 3;

      progress = progress > target_progress ? target_progress : progress;
    }
  }
};

class MainWindow {
private:
  duration all_duration;
  duration min_duration;
  duration max_duration;
  thread _thread;

public:
  wProgressBar bar;
  json raw_entries;
  vector<HarEntry> entries;

  MainWindow() : bar("Drag&Drop .har", 0.0) {}

  float render(Rectangle b, float offsetX, float offsetY, float scaleX, float scaleY) {
    if (entries.size() == 0) {
      render_loader(b);
      return 0;
    }

    auto by = b.y;
    b.y += 35;
    for(auto& entry : entries) {
      float start_x = _map<double>(
        entry.strated_date_time.time_since_epoch().count(),
        min_duration.count() - 1000,
        max_duration.count() + 1000,
        b.x + offsetX,
        b.x + offsetX + b.width * scaleX);

      float width = _map<double>(
        entry.time,
        0,
        (max_duration.count() - min_duration.count()) + 2000,
        0,
        b.width * scaleX);

      b.y += entry.render({b.x + offsetX, b.y + offsetY, b.width * scaleX, NAN}, start_x, width);
    }
    b.y += render_timeline({by, b.x, b.width, b.height}, offsetX, offsetY, scaleX, scaleY);

    return b.y;
  }

  float render_timeline(Rectangle b, float offsetX, float offsetY, float scaleX, float scaleY) {
    float height = 0;
    DrawRectangleRec({b.x, b.y, b.width, 30}, WHITE);
    DrawRectangleLinesEx({b.x, b.y, b.width, height += 30}, 1, BLUE);

    for (auto& entry : entries) {
      float pos_x = _map<double>(
        entry.strated_date_time.time_since_epoch().count(),
        min_duration.count() - 1000,
        max_duration.count() + 1000,
        b.x + offsetX,
        b.x + offsetX + b.width*scaleX);

      Color color = MAGENTA;
      if (entry.response__status / 100 == 2) {
        color = GREEN;
      } else if (entry.response__status / 100 == 3) {
        color = ORANGE;
      } else {
        color = RED;
      }

      DrawLineEx({pos_x, b.y}, {pos_x, b.y + height}, 3, color);
    }

    return height + 5;
  }

  void render_loader(Rectangle b) {
    bar.render({b.x + 30, b.y + b.height/2 - 60/2, b.width - 60, 60});
  }

  void LoadEntites(std::filesystem::path&& file_path) {
    _thread = thread([=]() {
      bar.set_title(string("Parse har archive file: ") + file_path.c_str());

      ifstream json_file(file_path);
      json data = json::parse(json_file);

      auto _json = data["log"];

      vector<HarEntry> new_entries;
      size_t count_elements = _json["entries"].size();
      int i = 0;
      cout << "SIZE: " << count_elements;

      bar.set_title(string("Loading har... ") + to_string(i) + "/" + to_string(count_elements));

      for (auto& entry : _json["entries"]) {
        new_entries.push_back(HarEntry(entry));
        i++;
        this->bar.set_progress((i * 100.0 / count_elements) / 100.0);
        this->bar.set_title(string("Loading har... ") + to_string(i) + "/" + to_string(count_elements));
      }

      datetime min = new_entries[0].strated_date_time;
      datetime max = new_entries[new_entries.size() - 1].ended_date_time;

      this->min_duration = min.time_since_epoch();
      this->max_duration = max.time_since_epoch();
      this->all_duration = max - min;
      this->entries = new_entries;
    });
  }
};

int main(int argc, char* argv[]) {
  auto window = new MainWindow();

  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(1200, 800, "Hello world");

  global_font = LoadFont_Terminus();

  if (argc > 1) {
    window->LoadEntites(argv[1]);
  }

  SetTextLineSpacing(22);

  int dragPointX = 0;
  int dragPointY = 0;

  float offsetX = 0;
  float offsetY = 0;
  float coffsetX = 0;
  float coffsetY = 0;

  float scaleX = 0.2;

  float window_height = 0.0;

  SetTargetFPS(70);

  while (!WindowShouldClose()) {
    if (IsMouseButtonReleased(MOUSE_BUTTON_MIDDLE)) {
      offsetX = 0;
      offsetY = 0;
      scaleX = 1.0;
    }

    if (GetMouseWheelMove() != 0) {
      if (IsKeyDown(KEY_LEFT_SHIFT)) {
        auto wheel = GetMouseWheelMove();

        scaleX += wheel / 8;
        scaleX = scaleX < 0.2 ? 0.2 : scaleX;
      } else {
        auto wheel = GetMouseWheelMoveV();

        scaleX += wheel.x / 8;
        scaleX = scaleX < 0.05 ? 0.05 : scaleX;

        offsetY -= -wheel.y * 140;
        offsetY = offsetY > 0 ? 0 : offsetY;
      }
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
      dragPointX = GetMouseX();
      dragPointY = GetMouseY();
    }

    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
      coffsetX = dragPointX - GetMouseX();
      coffsetY = dragPointY - GetMouseY();
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) {
      offsetX -= coffsetX; offsetY -= coffsetY;
      coffsetX = 0; coffsetY = 0;

      offsetX = offsetX > 250 ? 250 : offsetX;
      offsetY = offsetY > 250 ? 250 : offsetY;
    }

    if (IsFileDropped()) {
       auto file_path_list = LoadDroppedFiles();
       if (file_path_list.count >= 1) {
        auto ffile = std::filesystem::path(file_path_list.paths[0]);
        window->LoadEntites(std::move(ffile));
       }

       UnloadDroppedFiles(file_path_list);
    }

    offsetY = -1*offsetY > window_height ? -1*window_height : offsetY;

    BeginDrawing();
      ClearBackground(WHITE);
      window_height = window->render({
        0,
        0,
        (float)GetScreenWidth(),
        (float)GetScreenHeight()
      }, offsetX - coffsetX, offsetY - coffsetY, scaleX, 1.0);
    EndDrawing();
  }

  return 0;
}