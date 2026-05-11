#include <iostream>
#include <thread>
#include <future>
#include <atomic>
#include <chrono>
#include <SFML/Graphics.hpp>
#include <queue>
#include <mutex>
#include <sstream>
#include <deque>
#include <iomanip>
#include <ctime>
#include "mavsdk/mavsdk.h"
#include "mavsdk/log_callback.h"
#include "mavsdk/plugins/action/action.h"
#include "mavsdk/plugins/telemetry/telemetry.h"
#include "mavsdk/plugins/offboard/offboard.h"

using namespace mavsdk;
using namespace std::this_thread;  // 用于 sleep_for
using namespace std::chrono;      // 用于 seconds

// 鼠标命令队列
std::queue<char> mouse_command_queue;
std::mutex queue_mutex;

// 全局消息队列和互斥锁
std::deque<std::string> message_queue;
std::mutex message_queue_mutex;
const size_t MAX_MESSAGES = 10; // 最大显示消息数量

// 警告消息文本
std::wstring warning_message = L"";
sf::Color warning_color = sf::Color::Red;
std::mutex warning_mutex;

// 警告消息计数器
int ack_error_count = 0;
int sending_failed_count = 0;
int normal_message_count = 0;

// 键值定义
#define KEY_ESC 27
#define KEY_BRACKET_LEFT 91
#define KEY_UP 65
#define KEY_DOWN 66
#define KEY_RIGHT 67
#define KEY_LEFT 68
#define KEY_W 'w'
#define KEY_A 'a'
#define KEY_S 's'
#define KEY_D 'd'
#define KEY_L 'l'
#define KEY_Q 'q'
#define KEY_T 't'
#define KEY_E 'e'
#define KEY_F 'f'

//异步任务管理器类，用于管理后台异步任务的执行
class AsyncTaskManager {
private:
    std::future<void> current_task; // 当前任务的future对象
    std::atomic<bool> task_running{ false }; // 任务运行状态标志

//检查任务是否正在运行,如果任务正在运行返回true，否则返回false
public:
    bool is_busy() const { return task_running.load(); }

    //异步执行任务
    template<typename Func>
    void execute_async(Func&& func) {
        if (task_running.load()) {
            printf("之前的任务仍在运行中，忽略新命令\n");
            return;
        }

        task_running.store(true);
        current_task = std::async(std::launch::async, [this, func = std::forward<Func>(func)]() {
            try {
                func();
            }
            catch (const std::exception& e) {
                printf("任务执行错误: %s\n", e.what());
            }
            task_running.store(false);
            });
    }

    //清理并等待当前任务完成
    void cleanup() {
        if (current_task.valid()) {
            current_task.wait();
        }
    }
};

/*选择控制模式*/
bool program_running = true;

// 消息捕获函数
void add_message(const std::string& message) {
    std::lock_guard<std::mutex> lock(message_queue_mutex);

    // 添加时间戳
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "[%H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count() << "] ";
    ss << message;

    message_queue.push_back(ss.str());

    // 限制消息队列大小
    if (message_queue.size() > MAX_MESSAGES) {
        message_queue.pop_front();
    }
}

// 捕获MAVSDK日志的回调函数
bool mavsdk_log_callback(mavsdk::log::Level level, const std::string& message, const std::string& file, int line) {
    //只将引号内的消息添加到GUI（现在这一行和下面else被注释掉了，所有消息都会添加到GUI）
    //if (message.find("Landing detected") != std::string::npos) {
    std::string level_str;
    switch (level) {
    case mavsdk::log::Level::Debug: level_str = "Debug"; break;
    case mavsdk::log::Level::Info: level_str = "Info"; break;
    case mavsdk::log::Level::Warn: level_str = "Warn"; break;
    case mavsdk::log::Level::Err: level_str = "Error"; break;
    default: level_str = "Unknown"; break;
    }

    std::string formatted_message = "[" + level_str + "] " + message;
    add_message(formatted_message);

    bool has_error = false;
    // 检测“Received ack for not-existing command”（其他控制端接入时）并显示警告
    static int ack_error_count = 0;
    if (message.find("Received ack for not-existing command") != std::string::npos) {
        ack_error_count++;
        normal_message_count = 0;
        if (ack_error_count >= 4) {
            {
                std::lock_guard<std::mutex> lock(warning_mutex);
                warning_message = L"[警告]检测到其他控制端接入";
                warning_color = sf::Color(255, 0, 0);
            }
            ack_error_count = 0; // 重置计数，避免无限触发
        }
        has_error = true;
    }
    else {
        // 出现其他消息，计数清零
        ack_error_count = 0;
    }

    // 检测 “Sending message failed”（WiFi被改密码或断开时）并显示警告
    if (message.find("Sending message failed") != std::string::npos) {
        sending_failed_count++;
        normal_message_count = 0;
        if (sending_failed_count >= 3) {
            {
                std::lock_guard<std::mutex> lock(warning_mutex);
                warning_message = L"[警告]通信中断";
                warning_color = sf::Color(255, 0, 0);
            }
            sending_failed_count = 0; // 重置计数，避免无限触发
        }
        has_error = true;
    }
    else {
        // 出现其他消息，计数清零
        sending_failed_count = 0;
    }

    if (!has_error) {
        normal_message_count++;
        if (normal_message_count >= 2) {
            std::lock_guard<std::mutex> lock(warning_mutex);
            if (!warning_message.empty()) {
                warning_message.clear(); // 清除警告
                normal_message_count = 0; // 重置计数
            }
        }
    }

    //}
    /*else {
        // 其他消息直接输出到终端
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::string level_str;
        switch (level) {
        case mavsdk::log::Level::Debug: level_str = "Debug"; break;
        case mavsdk::log::Level::Info: level_str = "Info"; break;
        case mavsdk::log::Level::Warn: level_str = "Warn"; break;
        case mavsdk::log::Level::Err: level_str = "Error"; break;
        default: level_str = "Unknown"; break;
        }

        std::cout << std::put_time(std::localtime(&time_t), "[%H:%M:%S")
            << "." << std::setfill('0') << std::setw(3) << ms.count()
            << "|" << level_str << "] " << message << std::endl;
    }*/

    return true;
}

//offboard模式控制飞行
bool setup_offboard(mavsdk::Offboard& offboard)
{
    /*在 Offboard 模式下，PX4 要求外部控制器至少每秒发送一次有效的控制指令，
    *以维持飞控系统的 Offboard 模式。如果在一定时间内未收到有效指令，PX4 将退出 Offboard 模式，
    *因此，发送一个初始的零速度指令，以进入 Offboard 模式*/
    Offboard::VelocityNedYaw initial_velocity = { 0 };
    /*使用SFML图形界面显示非 ASCII 字符（如中文）会出现乱码，应使用 L"..." 宽字符串文字
    ◦ 但使用宽字符时英文又会出现乱码，而MAVSDK日志消息是英文，为了统一，系统消息栏全部使用英文*/

    add_message("[SYSTEM] Setting initial setpoint...");
    offboard.set_velocity_ned(initial_velocity);

    add_message("[SYSTEM] Starting Offboard mode...");
    const Offboard::Result offboard_result = offboard.start();
    if (offboard_result != Offboard::Result::Success) {
        std::cerr << "启动Offboard模式失败: " << offboard_result << '\n';
        add_message("[SYSTEM] Starting Offboard faild");
        return false;
    }
    else {
        add_message("[SYSTEM] Starting offboard successed");
    }

    //防止通信延迟或丢包导致的控制指令无法及时生效，再次发送，确保模式切换后有一个有效的速度设置点
    offboard.set_velocity_ned(initial_velocity);
    sleep_for(seconds(1));
    return true;
}

//停止Offboard模式
bool stop_offboard(mavsdk::Offboard& offboard)
{
    Offboard::Result offboard_result = offboard.stop();
    if (offboard_result != Offboard::Result::Success) {
        std::cerr << "停止Offboard模式失败: " << offboard_result << '\n';
        return false;
    }
    else {
        printf("成功停止Offboard模式\n");
    }
    sleep_for(seconds(1));
    return true;
}

/**
 • Offboard模式NED坐标系控制飞行

 • @param offboard Offboard插件实例

 • @param north_m_s 北向速度 (m/s)

 • @param east_m_s 东向速度 (m/s)

 • @param down_m_s 下向速度 (m/s)

 • @param yaw_deg 偏航角 (度)

 • @param time_ms 持续时间 (毫秒)

 • @return 始终返回true

 */
bool offboard_ctrl_ned(mavsdk::Offboard& offboard, float north_m_s, float east_m_s, float down_m_s, float yaw_deg, uint32_t time_ms)
{
    Offboard::VelocityNedYaw set_point = { 0 };
    set_point.north_m_s = north_m_s;
    set_point.east_m_s = east_m_s;
    set_point.down_m_s = down_m_s;
    set_point.yaw_deg = yaw_deg;

    offboard.set_velocity_ned(set_point);
    sleep_for(milliseconds(time_ms));
    return true;
}

/**
 • 处理按钮输入（非移动类按钮）

 • @param c 按键字符

 • @param offboard Offboard插件实例

 • @param action Action插件实例

 • @param telemetry Telemetry插件实例

 • @param current_yaw 当前偏航角引用

 • @param side_length 移动距离

 • @param flying_timems 飞行持续时间

 • @param task_manager 异步任务管理器

 • @param status_message 状态消息指针

 */
void handle_button_input(char c,
    mavsdk::Offboard& offboard,
    mavsdk::Action& action,
    mavsdk::Telemetry& telemetry,
    float& current_yaw,
    float side_length,
    uint32_t flying_timems,
    AsyncTaskManager& task_manager,
    std::wstring* status_message) {

    switch (c) {
    case KEY_L: {
        task_manager.execute_async([&action, &telemetry, status_message]() {
            const Action::Result land_result = action.land();
            if (land_result != Action::Result::Success) {
                *status_message = L"降落失败";
                return;
            }
            while (telemetry.in_air()) {
                *status_message = L"正在降落...";
            }
            *status_message = L"已降落并锁定";
            });
    } break;

    case KEY_Q: {
        *status_message = L"正在锁定...";
        task_manager.execute_async([&action, status_message]() {
            Action::Result disarm_result = action.disarm();
            if (disarm_result != Action::Result::Success) {
                *status_message = L"锁定失败";
            }
            else {
                *status_message = L"已锁定";
            }
            });
    } break;

    case KEY_E: {
        *status_message = L"正在解锁...";
        const Action::Result arm_result = action.arm();
        if (arm_result != Action::Result::Success) {
            *status_message = L"解锁失败";
            return;
        }
        sleep_for(1s);
        offboard_ctrl_ned(offboard, 0, 0, -side_length, current_yaw, flying_timems);
        *status_message = L"已解锁";
        break;
    }
              /**status_message = L"正在解锁...";
              task_manager.execute_async([&action, &offboard, &side_length, &current_yaw, &flying_timems, status_message]() {
                  *status_message = L"正在解锁...";
                  const Action::Result arm_result = action.arm();
                  if (arm_result != Action::Result::Success) {
                      *status_message = L"解锁失败";
                      return;
                  }

                      *status_message = L"已解锁";

                  sleep_for(seconds(1));
                  offboard_ctrl_ned(offboard, 0, 0, -side_length, current_yaw, flying_timems);
                  });
          } break;*/
    }
}

/**
 • 处理按钮输入（移动类按钮）

 • @param key 按键字符

 • @param offboard Offboard插件实例

 • @param current_yaw 当前偏航角引用

 • @param side_length 飞行速度

 • @param flying_timems 飞行持续时间

 • @param status_message 状态消息指针

 */
void execute_movement_command(char key, mavsdk::Offboard& offboard, float& current_yaw, float side_length, uint32_t flying_timems, std::wstring* status_message) {
    const float yaw_rad = current_yaw * static_cast<float>(acos(-1.0f)) / 180.0f;
    auto body_to_ned = [&](float vx_body, float vy_body, float& north_out, float& east_out) {
        north_out = vx_body * std::cos(yaw_rad) - vy_body * std::sin(yaw_rad);
        east_out  = vx_body * std::sin(yaw_rad) + vy_body * std::cos(yaw_rad);
    };
    switch (key) {
    case KEY_UP: {
        *status_message = L"向前...";
        float vx_body = side_length;
        float vy_body = 0.0f;
        float north_m_s = 0.0f, east_m_s = 0.0f;
        body_to_ned(vx_body, vy_body, north_m_s, east_m_s);
        offboard_ctrl_ned(offboard, north_m_s, east_m_s, 0, current_yaw, flying_timems);
    } break;

    case KEY_DOWN: {
        *status_message = L"向后...";
        float vx_body = -side_length;
        float vy_body = 0.0f;
        float north_m_s = 0.0f, east_m_s = 0.0f;
        body_to_ned(vx_body, vy_body, north_m_s, east_m_s);
        offboard_ctrl_ned(offboard, north_m_s, east_m_s, 0, current_yaw, flying_timems);
    } break;

    case KEY_RIGHT: {
        *status_message = L"向右...";
        float vx_body = 0.0f;
        float vy_body = side_length;
        float north_m_s = 0.0f, east_m_s = 0.0f;
        body_to_ned(vx_body, vy_body, north_m_s, east_m_s);
        offboard_ctrl_ned(offboard, north_m_s, east_m_s, 0, current_yaw, flying_timems);
    } break;

    case KEY_LEFT: {
        *status_message = L"向左...";
        float vx_body = 0.0f;
        float vy_body = -side_length;
        float north_m_s = 0.0f, east_m_s = 0.0f;
        body_to_ned(vx_body, vy_body, north_m_s, east_m_s);
        offboard_ctrl_ned(offboard, north_m_s, east_m_s, 0, current_yaw, flying_timems);
    } break;

    case KEY_W: {
        *status_message = L"上升...";
        offboard_ctrl_ned(offboard, 0, 0, -side_length, current_yaw, flying_timems);
    } break;

    case KEY_S: {
        *status_message = L"下降...";
        offboard_ctrl_ned(offboard, 0, 0, side_length, current_yaw, flying_timems);
    } break;

    case KEY_D: {
        current_yaw += 10.0;
        if (current_yaw >= 360.0) current_yaw -= 360.0;
        *status_message = L"当前航向: " + std::to_wstring((int)current_yaw) + L"°";
        offboard_ctrl_ned(offboard, 0, 0, 0, current_yaw, flying_timems);
    } break;

    case KEY_A: {
        current_yaw -= 10.0;
        if (current_yaw < 0.0) current_yaw += 360.0;
        *status_message = L"当前航向: " + std::to_wstring((int)current_yaw) + L"°";
        offboard_ctrl_ned(offboard, 0, 0, 0, current_yaw, flying_timems);
    } break;
    }
}

// 连接方式选择界面
std::string select_connection_type(sf::RenderWindow*& connection_window) {
    connection_window = new sf::RenderWindow(sf::VideoMode(800, 600), L"连接方式选择界面");
    connection_window->setFramerateLimit(30);

    sf::Font font;
    if (!font.loadFromFile("/usr/share/fonts/opentype/noto/NotoSerifCJK-Bold.ttc")) { //中文字体
        std::cerr << "字体加载失败\n";
        return "";
    }

    // 标题文本
    sf::Text title;
    title.setFont(font);
    title.setString(L"请选择连接方式");
    title.setCharacterSize(32);
    title.setFillColor(sf::Color::Black);
    sf::FloatRect titleBounds = title.getLocalBounds();
    title.setOrigin(titleBounds.left + titleBounds.width / 2, titleBounds.top + titleBounds.height / 2);
    title.setPosition(400, 30);

    // 状态显示文本
    sf::Text status_text;

    // 连接方式按钮结构体
    struct ConnectionButton {
        sf::RectangleShape shape;
        sf::Text label;
        std::string url;
        sf::Color normal_color = sf::Color(200, 200, 200);   // 正常颜色
        sf::Color hover_color = sf::Color(150, 150, 150);    // 悬停颜色
        bool is_hovered = false;
        bool is_disabled = false;
    };

    std::vector<ConnectionButton> conn_buttons;

    // 串口按钮
    ConnectionButton serial_btn;
    serial_btn.shape.setSize(sf::Vector2f(200, 80));
    serial_btn.shape.setPosition(300, 60);
    serial_btn.shape.setFillColor(serial_btn.normal_color);
    serial_btn.shape.setOutlineThickness(2);
    serial_btn.shape.setOutlineColor(sf::Color::Black);
    serial_btn.label.setFont(font);
    serial_btn.label.setString(L"串口");
    serial_btn.label.setCharacterSize(24);
    serial_btn.label.setFillColor(sf::Color::Black);
    sf::FloatRect serialBounds = serial_btn.label.getLocalBounds();
    serial_btn.label.setOrigin(serialBounds.left + serialBounds.width / 2, serialBounds.top + serialBounds.height / 2);
    serial_btn.label.setPosition(400, 100);
    serial_btn.url = "serial:///dev/ttyAMA0:57600";
    conn_buttons.push_back(serial_btn);

    // Wi-Fi按钮
    ConnectionButton wifi_btn;
    wifi_btn.shape.setSize(sf::Vector2f(200, 80));
    wifi_btn.shape.setPosition(300, 160);
    wifi_btn.shape.setFillColor(wifi_btn.normal_color);
    wifi_btn.shape.setOutlineThickness(2);
    wifi_btn.shape.setOutlineColor(sf::Color::Black);
    wifi_btn.label.setFont(font);
    wifi_btn.label.setString(L"Wi-Fi");
    wifi_btn.label.setCharacterSize(24);
    wifi_btn.label.setFillColor(sf::Color::Black);
    sf::FloatRect wifiBounds = wifi_btn.label.getLocalBounds();
    wifi_btn.label.setOrigin(wifiBounds.left + wifiBounds.width / 2, wifiBounds.top + wifiBounds.height / 2);
    wifi_btn.label.setPosition(400, 200);
    wifi_btn.url = "udp://0.0.0.0:14560";
    conn_buttons.push_back(wifi_btn);

    // 自组网按钮
    ConnectionButton adhoc_btn;
    adhoc_btn.shape.setSize(sf::Vector2f(200, 80));
    adhoc_btn.shape.setPosition(300, 260);
    adhoc_btn.shape.setFillColor(adhoc_btn.normal_color);
    adhoc_btn.shape.setOutlineThickness(2);
    adhoc_btn.shape.setOutlineColor(sf::Color::Black);
    adhoc_btn.label.setFont(font);
    adhoc_btn.label.setString(L"自组网");
    adhoc_btn.label.setCharacterSize(24);
    adhoc_btn.label.setFillColor(sf::Color::Black);
    sf::FloatRect adhocBounds = adhoc_btn.label.getLocalBounds();
    adhoc_btn.label.setOrigin(adhocBounds.left + adhocBounds.width / 2, adhocBounds.top + adhocBounds.height / 2);
    adhoc_btn.label.setPosition(400, 300);
    adhoc_btn.url = "serial:///dev/ttyUSB0:115200";
    conn_buttons.push_back(adhoc_btn);

    std::string selected_url;

    // 消息显示区域
    sf::RectangleShape message_background;
    message_background.setSize(sf::Vector2f(780, 200));
    message_background.setPosition(10, 380);
    message_background.setFillColor(sf::Color(240, 240, 240));
    message_background.setOutlineThickness(2);
    message_background.setOutlineColor(sf::Color::Black);

    sf::Text message_title;
    message_title.setFont(font);
    message_title.setString(L"系统消息:");
    message_title.setCharacterSize(18);
    message_title.setFillColor(sf::Color::Black);
    message_title.setPosition(20, 390);

    // 消息文本数组
    std::vector<sf::Text> message_texts;
    for (int i = 0; i < MAX_MESSAGES; ++i) {
        sf::Text text;
        text.setFont(font);
        text.setCharacterSize(14);
        text.setFillColor(sf::Color::Black);
        text.setPosition(20, 415 + i * 16);
        message_texts.push_back(text);
    }

    while (connection_window->isOpen() && selected_url.empty()) {
        sf::Event event;
        while (connection_window->pollEvent(event)) {
            if (event.type == sf::Event::Closed) {
                connection_window->close();
                delete connection_window;
                connection_window = nullptr;
                return "";
            }

            // 鼠标移动事件
            if (event.type == sf::Event::MouseMoved) {
                sf::Vector2f mousePos = sf::Vector2f(event.mouseMove.x, event.mouseMove.y);
                for (auto& btn : conn_buttons) {
                    btn.is_hovered = btn.shape.getGlobalBounds().contains(mousePos);
                }
            }

            // 鼠标点击事件
            if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
                sf::Vector2f mousePos = sf::Vector2f(event.mouseButton.x, event.mouseButton.y);
                for (const auto& btn : conn_buttons) {
                    if (btn.shape.getGlobalBounds().contains(mousePos)) {
                        selected_url = btn.url;
                        break;
                    }
                }
            }
        }

        // 更新按钮颜色
        for (auto& btn : conn_buttons) {
            if (btn.is_hovered) {
                btn.shape.setFillColor(btn.hover_color);
            }
            else {
                btn.shape.setFillColor(btn.normal_color);
            }
        }

        // 更新消息文本
        {
            std::lock_guard<std::mutex> lock(message_queue_mutex);
            for (size_t i = 0; i < message_texts.size(); ++i) {
                if (i < message_queue.size()) {
                    size_t msg_index = message_queue.size() - 1 - i;
                    message_texts[message_texts.size() - 1 - i].setString(message_queue[msg_index]);
                }
                else {
                    message_texts[message_texts.size() - 1 - i].setString("");
                }
            }
        }

        // 渲染所有内容
        connection_window->clear(sf::Color::White);

        // 绘制标题和按钮
        connection_window->draw(title);
        connection_window->draw(status_text);
        for (const auto& btn : conn_buttons) {
            connection_window->draw(btn.shape);
            connection_window->draw(btn.label);
        }

        // 绘制消息区域
        connection_window->draw(message_background);
        connection_window->draw(message_title);
        for (const auto& text : message_texts) {
            connection_window->draw(text);
        }

        connection_window->display();
    }

    return selected_url;
}

/**
 • 更新连接窗口状态

 • @param connection_window 连接窗口指针

 • @param status 状态消息

 • @param color 消息颜色

 */
void update_connection_status(sf::RenderWindow* connection_window, const std::wstring& status = L"", sf::Color color = sf::Color::Blue) {
    if (!connection_window || !connection_window->isOpen()) {
        return;
    }

    sf::Font font;
    if (!font.loadFromFile("/usr/share/fonts/opentype/noto/NotoSerifCJK-Bold.ttc")) {
        return;
    }

    // 标题文本
    sf::Text title;
    title.setFont(font);
    title.setString(L"无人机控制系统");
    title.setCharacterSize(32);
    title.setFillColor(sf::Color::Black);
    sf::FloatRect titleBounds = title.getLocalBounds();
    title.setOrigin(titleBounds.left + titleBounds.width / 2, titleBounds.top + titleBounds.height / 2);
    title.setPosition(400, 100);

    // 状态文本
    sf::Text status_text;
    status_text.setFont(font);
    status_text.setString(status);
    status_text.setCharacterSize(24);
    status_text.setFillColor(color);
    sf::FloatRect statusBounds = status_text.getLocalBounds();
    status_text.setOrigin(statusBounds.left + statusBounds.width / 2, statusBounds.top + statusBounds.height / 2);
    status_text.setPosition(400, 200);

    // 消息显示区域
    sf::RectangleShape message_background;
    message_background.setSize(sf::Vector2f(780, 200));
    message_background.setPosition(10, 380);
    message_background.setFillColor(sf::Color(240, 240, 240));
    message_background.setOutlineThickness(2);
    message_background.setOutlineColor(sf::Color::Black);

    sf::Text message_title;
    message_title.setFont(font);
    message_title.setString(L"系统消息:");
    message_title.setCharacterSize(18);
    message_title.setFillColor(sf::Color::Black);
    message_title.setPosition(20, 390);

    std::vector<sf::Text> message_texts;
    for (int i = 0; i < MAX_MESSAGES; ++i) {
        sf::Text text;
        text.setFont(font);
        text.setCharacterSize(14);
        text.setFillColor(sf::Color::Black);
        text.setPosition(20, 415 + i * 16);
        message_texts.push_back(text);
    }

    // 更新消息文本
    {
        std::lock_guard<std::mutex> lock(message_queue_mutex);
        for (size_t i = 0; i < message_texts.size(); ++i) {
            if (i < message_queue.size()) {
                size_t msg_index = message_queue.size() - 1 - i;
                message_texts[message_texts.size() - 1 - i].setString(message_queue[msg_index]);
            }
            else {
                message_texts[message_texts.size() - 1 - i].setString("");
            }
        }
    }

    // 处理窗口事件
    sf::Event event;
    while (connection_window->pollEvent(event)) {
        if (event.type == sf::Event::Closed) {
            connection_window->close();
            return;
        }
    }

    // 渲染
    connection_window->clear(sf::Color::White);
    connection_window->draw(title);
    connection_window->draw(status_text);
    connection_window->draw(message_background);
    connection_window->draw(message_title);
    for (const auto& text : message_texts) {
        connection_window->draw(text);
    }
    connection_window->display();
}

/**
 • 键鼠控制循环主函数

 • @param mode 控制模式引用

 • @param offboard Offboard插件实例

 • @param action Action插件实例

 • @param telemetry Telemetry插件实例

 • @param current_yaw 当前偏航角引用

 • @param side_length 移动距离

 • @param flying_timems 飞行持续时间

 */
void run_mouse_control_loop(bool& program_running,
    mavsdk::Offboard& offboard,
    mavsdk::Action& action,
    mavsdk::Telemetry& telemetry,
    float& current_yaw,
    float side_length,
    uint32_t flying_timems)
{
    sf::RenderWindow window(sf::VideoMode(800, 600), L"无人机控制系统");
    window.setFramerateLimit(30);

    AsyncTaskManager task_manager;

    sf::Font font;
    if (!font.loadFromFile("/usr/share/fonts/opentype/noto/NotoSerifCJK-Bold.ttc")) {
        std::cerr << "字体加载失败\n";
        return;
    }

    std::wstring status_message;

    // 按钮结构体定义
    struct Button {
        sf::RectangleShape shape;           // 按钮形状
        sf::Text label;                     // 按钮标签
        char key;                          // 对应的键盘按键
        sf::Color normal_color = sf::Color(200, 200, 200);      // 正常颜色
        sf::Color disabled_color = sf::Color(100, 100, 100);    // 禁用颜色
        sf::Color longpress_color = sf::Color(100, 200, 100);   // 按下颜色
        bool is_pressed = false;            // 鼠标是否按下
        bool is_movement_button = false;    // 是否为移动按钮
        bool is_keyboard_pressed = false;   // 键盘是否按下
    };
    std::vector<Button> buttons;

    // 创建按钮的辅助函数
    auto create_button = [&](const std::wstring& name, char key, float x, float y, bool is_movement = false) {
        Button b;
        b.shape.setSize(sf::Vector2f(60, 60));
        b.shape.setFillColor(b.normal_color);
        b.shape.setOutlineThickness(2);
        b.shape.setOutlineColor(sf::Color::Black);
        b.shape.setPosition(x, y);

        b.label.setFont(font);
        b.label.setString(name);
        b.label.setCharacterSize(20);
        b.label.setFillColor(sf::Color::Black);
        sf::FloatRect tb = b.label.getLocalBounds();
        b.label.setOrigin(tb.left + tb.width / 2, tb.top + tb.height / 2);
        b.label.setPosition(x + 30, y + 30);

        b.key = key;
        b.is_movement_button = is_movement;
        buttons.push_back(b);
        };

    // 创建按钮
    create_button(L"上升", KEY_W, 110, 100, true);
    create_button(L"左转", KEY_A, 50, 160, true);
    create_button(L"下降", KEY_S, 110, 160, true);
    create_button(L"右转", KEY_D, 172, 160, true);
    create_button(L"向前", KEY_UP, 620, 100, true);
    create_button(L"向左", KEY_LEFT, 560, 160, true);
    create_button(L"向后", KEY_DOWN, 620, 160, true);
    create_button(L"向右", KEY_RIGHT, 682, 160, true);
    create_button(L"解锁", KEY_E, 250, 260, false);
    create_button(L"锁定", KEY_Q, 320, 260, false);
    create_button(L"降落", KEY_L, 390, 260, false);
    create_button(L"退出", 'f', 530, 260, false);

    // 状态文本
    sf::Text status_text;
    status_text.setFont(font);
    status_text.setCharacterSize(28);
    status_text.setFillColor(sf::Color::Black);
    sf::FloatRect textRect = status_text.getLocalBounds();
    status_text.setOrigin(textRect.left + textRect.width / 2.0, textRect.top + textRect.height / 2.0);
    status_text.setPosition(400, 80);

    // 警告文本
    sf::Text warning_text;
    warning_text.setFont(font);
    warning_text.setCharacterSize(28);
    warning_text.setFillColor(sf::Color::Red);
    sf::FloatRect textRect1 = warning_text.getLocalBounds();
    warning_text.setOrigin(textRect1.left + textRect1.width / 2.0, textRect1.top + textRect1.height / 2.0);
    warning_text.setPosition(400, 110);

    // 消息显示区域
    sf::RectangleShape message_background;
    message_background.setSize(sf::Vector2f(780, 200));
    message_background.setPosition(10, 380);
    message_background.setFillColor(sf::Color(240, 240, 240));
    message_background.setOutlineThickness(2);
    message_background.setOutlineColor(sf::Color::Black);

    sf::Text message_title;
    message_title.setFont(font);
    message_title.setString(L"系统消息:");
    message_title.setCharacterSize(18);
    message_title.setFillColor(sf::Color::Black);
    message_title.setPosition(20, 390);

    std::vector<sf::Text> message_texts;
    for (int i = 0; i < MAX_MESSAGES; ++i) {
        sf::Text text;
        text.setFont(font);
        text.setCharacterSize(14);
        text.setFillColor(sf::Color::Black);
        text.setPosition(20, 415 + i * 16);
        message_texts.push_back(text);
    }

    // 长按相关变量
    sf::Clock longpress_clock;                          // 鼠标长按时钟
    const sf::Time longpress_interval = sf::milliseconds(100); // 长按间隔
    Button* currently_pressed = nullptr;                // 当前被鼠标按下的按钮

    sf::Clock keyboard_longpress_clock;                 // 键盘长按时钟
    Button* keyboard_currently_pressed = nullptr;       // 当前被键盘按下的按钮
    std::map<sf::Keyboard::Key, bool> keyboard_state;   // 键盘状态映射

    // 根据按键字符查找对应按钮的辅助函数
    auto get_button_by_key = [&](char key) -> Button* {
        for (auto& b : buttons) if (b.key == key) return &b;
        return nullptr;
        };

    // 图形界面上的按键转换为字符的辅助函数
    auto sfml_key_to_char = [](sf::Keyboard::Key key) -> char {
        switch (key) {
        case sf::Keyboard::W: return KEY_W;
        case sf::Keyboard::A: return KEY_A;
        case sf::Keyboard::S: return KEY_S;
        case sf::Keyboard::D: return KEY_D;
        case sf::Keyboard::Up: return KEY_UP;
        case sf::Keyboard::Down: return KEY_DOWN;
        case sf::Keyboard::Left: return KEY_LEFT;
        case sf::Keyboard::Right: return KEY_RIGHT;
        case sf::Keyboard::E: return KEY_E;
        case sf::Keyboard::Q: return KEY_Q;
        case sf::Keyboard::L: return KEY_L;
        case sf::Keyboard::F: return 'f';
        default: return 0;
        }
        };

    // 主循环
    while (window.isOpen() && program_running) {
        if (!currently_pressed && !keyboard_currently_pressed) {
            offboard_ctrl_ned(offboard, 0, 0, 0, current_yaw, 50);
        }

        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) {
                window.close();
                task_manager.cleanup();
                break;
            }

            // 键盘按下事件处理
            if (event.type == sf::Event::KeyPressed) {
                char key_char = sfml_key_to_char(event.key.code);
                if (key_char != 0) {
                    Button* button = get_button_by_key(key_char);
                    if (button) {
                        if (key_char == 'f') {
                            //按f退出程序
                            program_running = false;
                            task_manager.cleanup();
                            window.close();
                            break;
                        }
                        else if (!button->is_movement_button) {
                            // 非移动按钮的处理
                            button->is_keyboard_pressed = true;
                            handle_button_input(key_char, offboard, action, telemetry,
                                current_yaw, side_length, flying_timems, task_manager, &status_message);
                        }
                        else {
                            // 移动按钮的处理
                            if (!keyboard_state[event.key.code]) {
                                button->is_keyboard_pressed = true;
                                keyboard_currently_pressed = button;
                                keyboard_longpress_clock.restart();
                                keyboard_state[event.key.code] = true;
                                execute_movement_command(key_char, offboard, current_yaw, side_length, flying_timems, &status_message);
                            }
                        }
                    }
                }
            }

            // 键盘释放事件处理
            if (event.type == sf::Event::KeyReleased) {
                char key_char = sfml_key_to_char(event.key.code);
                if (key_char != 0) {
                    Button* button = get_button_by_key(key_char);
                    if (button) {
                        button->is_keyboard_pressed = false;
                        keyboard_state[event.key.code] = false;
                        if (keyboard_currently_pressed == button) keyboard_currently_pressed = nullptr;
                    }
                }
            }

            // 鼠标按下事件处理
            if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
                sf::Vector2f pos = { float(event.mouseButton.x), float(event.mouseButton.y) };
                for (auto& b : buttons) {
                    if (b.shape.getGlobalBounds().contains(pos)) {
                        if (b.key == 'f') {
                            //退出程序
                            program_running = false;
                            task_manager.cleanup();
                            window.close();
                            break;
                        }
                        else if (!b.is_movement_button) {
                            // 非移动按钮的处理
                            handle_button_input(b.key, offboard, action, telemetry,
                                current_yaw, side_length, flying_timems, task_manager, &status_message);
                        }
                        else {
                            // 移动按钮的处理
                            b.is_pressed = true;
                            currently_pressed = &b;
                            longpress_clock.restart();
                            execute_movement_command(b.key, offboard, current_yaw, side_length, flying_timems, &status_message);
                        }
                        break;
                    }
                }
            }
            // 鼠标释放事件处理
            if (event.type == sf::Event::MouseButtonReleased && event.mouseButton.button == sf::Mouse::Left) {
                if (currently_pressed) {
                    currently_pressed->is_pressed = false;
                    currently_pressed = nullptr;
                }
            }
        }

        // 鼠标长按处理
        if (currently_pressed && currently_pressed->is_pressed) {
            if (longpress_clock.getElapsedTime() >= longpress_interval) {
                execute_movement_command(currently_pressed->key, offboard, current_yaw, side_length, flying_timems, &status_message);
                longpress_clock.restart();
            }
        }

        // 键盘长按处理
        if (keyboard_currently_pressed && keyboard_currently_pressed->is_keyboard_pressed) {
            if (keyboard_longpress_clock.getElapsedTime() >= longpress_interval) {
                execute_movement_command(keyboard_currently_pressed->key, offboard, current_yaw, side_length, flying_timems, &status_message);
                keyboard_longpress_clock.restart();
            }
        }

        // 按钮颜色更新
        bool is_busy = task_manager.is_busy();
        for (auto& b : buttons) {
            if (b.is_pressed) 
                // 鼠标按下状态
                b.shape.setFillColor(b.longpress_color);
            else if (b.is_keyboard_pressed) {
                // 键盘按下状态
                if (b.is_movement_button) 
                    b.shape.setFillColor(b.longpress_color);
                else {
                    b.shape.setFillColor(b.disabled_color);
                }
            }
            else if (is_busy && (b.key == KEY_E || b.key == KEY_Q || b.key == KEY_L || b.key == KEY_T)) {
                // 任务繁忙时禁用非移动按钮
                b.shape.setFillColor(b.disabled_color);
            }
            else b.shape.setFillColor(b.normal_color);
        }

        // 更新状态文本
        status_text.setString(sf::String(status_message));
        {
            sf::FloatRect textRect = status_text.getLocalBounds();
            status_text.setOrigin(textRect.left + textRect.width / 2.0f,
                textRect.top + textRect.height / 2.0f);
            status_text.setPosition(400, 80);
        }

        // 更新警告文本
        {
            std::lock_guard<std::mutex> lock(warning_mutex);
            warning_text.setString(sf::String(warning_message));
            warning_text.setFillColor(warning_color);

            sf::FloatRect textRect = warning_text.getLocalBounds();
            warning_text.setOrigin(textRect.left + textRect.width / 2.0f,
                textRect.top + textRect.height / 2.0f);
            warning_text.setPosition(400, 110);
        }

        // 更新消息队列
        {
            std::lock_guard<std::mutex> lock(message_queue_mutex);
            for (size_t i = 0; i < message_texts.size(); ++i) {
                if (i < message_queue.size()) {
                    size_t msg_index = message_queue.size() - 1 - i;
                    message_texts[message_texts.size() - 1 - i].setString(message_queue[msg_index]);
                }
                else {
                    message_texts[message_texts.size() - 1 - i].setString("");
                }
            }
        }

        // 每帧渲染一次
        window.clear(sf::Color::White);

        // 绘制主界面
        window.draw(status_text);
        window.draw(warning_text);
        for (auto& b : buttons) {
            window.draw(b.shape);
            window.draw(b.label);
        }

        // 绘制消息面板
        window.draw(message_background);
        window.draw(message_title);
        for (auto& text : message_texts) window.draw(text);

        window.display();
    }

    // 清理和恢复
    task_manager.cleanup();
}

int main(int argc, char** argv)
{
    //创建与无人机的通信
    using namespace mavsdk;

    // 设置MAVSDK日志回调
    mavsdk::log::subscribe(mavsdk_log_callback);

    // 创建MAVSDK实例，配置为地面站
    Mavsdk mavsdk{ Mavsdk::Configuration{ComponentType::GroundStation} };

    // 使用GUI选择连接方式
    sf::RenderWindow* connection_window = nullptr;
    std::string url = select_connection_type(connection_window);
    if (url.empty()) {
        std::cerr << "未选择连接方式，退出程序\n";
        if (connection_window) {
            delete connection_window;
        }
        return 1;
    }

    // 更新连接窗口状态
    update_connection_status(connection_window, L"正在建立连接...", sf::Color::Blue);

    ConnectionResult connection_result = mavsdk.add_any_connection(url);
    if (connection_result != ConnectionResult::Success) {
        // 更新连接失败状态
        update_connection_status(connection_window, L"连接失败", sf::Color::Red);
        // 等待用户关闭窗口
        if (connection_window && connection_window->isOpen()) {
            while (connection_window->isOpen()) {
                sf::Event event;
                while (connection_window->pollEvent(event)) {
                    if (event.type == sf::Event::Closed) {
                        connection_window->close();
                    }
                }
            }
            delete connection_window;
        }
        return 1;
    }

    //寻找在线的无人机
    std::optional<std::shared_ptr<mavsdk::System>> system;
    while (!system) {
        system = mavsdk.first_autopilot(3.0);

        // 处理窗口事件，防止“未响应”
        sf::Event event;
        while (connection_window->pollEvent(event)) {
            if (event.type == sf::Event::Closed) {
                connection_window->close();
                return 1;  // 用户主动关闭窗口
            }
        }

        update_connection_status(connection_window, L"连接成功,正在寻找无人机...", sf::Color::Green);
    }
    update_connection_status(connection_window, L"发现无人机,正在初始化...", sf::Color::Green);

    // 实例化插件
    Telemetry telemetry = Telemetry{ system.value() };
    Action action = Action{ system.value() };
    Offboard offboard = Offboard{ system.value() };

    // 等待无人机系统健康检查通过
    while (!telemetry.health_all_ok()) {
        sleep_for(seconds(1));

        // 处理窗口事件
        sf::Event event;
        while (connection_window->pollEvent(event)) {
            if (event.type == sf::Event::Closed) {
                connection_window->close();
                return 1;
            }
        }
    }
    update_connection_status(connection_window, L"系统就绪，正在启动控制界面...", sf::Color::Green);

    // 起飞状态监听
    std::promise<void> in_air_promise;
    std::future<void> in_air_future = in_air_promise.get_future();
    Telemetry::LandedStateHandle handle = telemetry.subscribe_landed_state(
        [&telemetry, &in_air_promise, &handle](Telemetry::LandedState state) {
            if (state == Telemetry::LandedState::InAir) {
                telemetry.unsubscribe_landed_state(handle);
                in_air_promise.set_value();
            }
        });

    // 设置Offboard模式
    if (false == setup_offboard(offboard)) {
        update_connection_status(connection_window, L"offboard模式设置失败", sf::Color::Green);
        if (connection_window) {
            delete connection_window;
        }
        return 1;
    }

    // 控制参数设置
    const float side_length = 5;      // 飞行速度(米/秒)
    const uint32_t flying_timems = 20; // 按一下按键的飞行持续时间(毫秒)
    float current_yaw = 0.0;          // 初始朝向(度)

    // 关闭连接选择窗口，启动控制窗口
    if (connection_window) {
        connection_window->close();
        delete connection_window;
        connection_window = nullptr;
    }

    // 启动鼠标控制模式
    program_running = true;
    run_mouse_control_loop(program_running, offboard, action, telemetry, current_yaw, side_length, flying_timems);

    // 停止Offboard模式
    if (false == stop_offboard(offboard)) {
        printf("offboard模式停止失败\n");
        return 1;
    }

    sleep_for(seconds(1));

    printf("退出程序...\n");

    return 0;
}
