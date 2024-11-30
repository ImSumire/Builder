#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

#include <future>
#include <iostream>
#include <string>
#include <vector>

#include "libs/toml.hpp"

typedef std::vector<std::future<void>> Futures;

inline void err(const char* __format, ...) {
    va_list args;
    va_start(args, __format);

    printf("\x1b[30mBuilder: ");
    vprintf(__format, args);
    printf("\x1b[0m\n");

    va_end(args);
}

inline void process_futures(Futures& futures) {
    Futures::iterator it = futures.begin();
    while (it != futures.end()) {
        if (it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                it->get();
            } catch (const std::exception& e) {
                err("Error in async task: %s", e.what());
            }

            it = futures.erase(it);
        } else {
            ++it;
        }
    }
}

inline std::string to_string(const toml::node& value) {
    if (value.is_string()) {
        return value.as_string()->get();
    }

    else if (value.is_integer()) {
        return std::to_string(value.as_integer()->get());
    }

    else if (value.is_floating_point()) {
        return std::to_string(value.as_floating_point()->get());
    }

    else if (value.is_boolean()) {
        return value.as_boolean()->get() ? "true" : "false";
    }

    else if (value.is_array()) {
        std::ostringstream oss;
        const toml::v3::array* values = value.as_array();
        int len = values->size();
        for (int i = 0; i < len; i++) {
            oss << to_string(values->at(i));
            if (i != len - 1)
                oss << ",";
        }
        oss << '\0';
        std::string result = oss.str();
        if (!result.empty()) {
            result.pop_back();  // Remove the trailing comma
        }
        return result;
    }

    else {
        err("Unknow type");
        return "";
    }
}

void handle_table(toml::v3::node& node, toml::v3::ex::parse_result& config, int argc, char* argv[]) {
    toml::v3::table& table = *node.as_table();

    toml::v3::node_view<toml::v3::node> commands = table["commands"];
    if (commands.is_array()) {
        // Set environment variables
        for (const auto& [key, value] : config) {
            if (!value.is_table()) {
                const char* key_data = key.data();

                if (std::getenv(key_data) != nullptr) {
                    err("Variable already exits");
                }

                int result = setenv(key_data, to_string(value).c_str(), false);
                if (result != 0) {
                    err("Cant't set variable");
                }
            }
        }
        for (int i = 0; i < argc; i++) {
            int result = setenv(("arg" + std::to_string(i)).c_str(), argv[i], false);
            if (result != 0) {
                err("Cant't set variable");
            }
        }

        // Run commands
        bool parallel = table["parallel"].value_or(false);
        if (parallel) {
            Futures futures;

            for (const auto& cmd : *commands.as_array()) {
                futures.push_back(std::async(std::launch::async, [&cmd]() {
                    int result = std::system(cmd.value_or(""));
                    if (result != 0) {
                        err("Command failed with exit code: %d", result);
                    }
                }));
            }

            while (!futures.empty()) {
                process_futures(futures);
            }
        }

        else {
            for (const auto& cmd : *commands.as_array()) {
                int result = std::system(cmd.value_or(""));
                if (result != 0) {
                    err("Command failed with exit code: %d", result);
                }
            }
        }
    }
}

void run_task(const std::string_view& name, int argc, char* argv[], const std::string& path) {
    toml::v3::ex::parse_result config = toml::parse_file(path);

    // Verify is the command exists
    if (config.contains(name)) {
        toml::v3::node& node = config.at(name);

        if (node.is_table()) {
            handle_table(node, config, argc, argv);
        }
    }

    else {
        err("Unknow command");
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        err("Usage: %s [task] [args]", argv[0]);
        return 1;
    }

    run_task(argv[1], argc, argv, "project.toml");
}
