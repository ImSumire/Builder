#include <string>
#include <vector>
#include <future>
#include <time.h>
#include <stdlib.h>
#include <iostream>

#include "libs/toml.hpp"


inline bool environmentVariableExists(const char* var) {
    return std::getenv(var) != nullptr;
}


inline void processFutures(std::vector<std::future<void>>& futures) {
    auto it = futures.begin();
    while (it != futures.end()) {
        if (it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                it->get();
            } catch (const std::exception& e) {
                std::cerr << "Error in async task: " << e.what() << std::endl;
            }

            it = futures.erase(it);
        } else {
            ++it;
        }
    }
}


inline std::string toString(const toml::node& value) {
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
        auto values = value.as_array();
        int len = values->size();
        for (int i = 0; i < len; i++) {
            oss << toString(values->at(i));
            if (i != len - 1)
                oss << ",";
        }
        oss << '\0';
        std::string result = oss.str();
        if (!result.empty()) {
            result.pop_back(); // Remove the trailing comma
        }
        return result;
    }

    else {
        std::cerr << "Unknow type" << std::endl;
        return "";
    }
}


inline std::string loadFile(const char* path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open the file." << std::endl;
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}


void runTask(const std::string& name, const std::string& path) {
    // auto content = loadFile(path.c_str());
    // auto config = toml::parse(content);
    auto config = toml::parse_file(path);

    // Verify is the command exists
    if (config.contains(name)) {
        toml::v3::node& node = config.at(name);

        if (node.is_table()) {
            auto& table = *node.as_table();

            auto commands = table["commands"];
            if (commands.is_array()) {
                // Set environment variables
                for (const auto& [key, value] : config) {
                    if (!value.is_table()) {
                        const char* ckey = key.data();

                        if (environmentVariableExists(ckey)) {
                                std::cerr << "Variable already exits" << std::endl;
                        }

                        int result = setenv(ckey, toString(value).c_str(), false);
                        if (result != 0) {
                            std::cerr << "Cant't set variable" << std::endl;
                        }
                    }
                }

                // Run commands
                bool parallel = table["parallel"].value_or(false);
                if (parallel) {
                    std::vector<std::future<void>> futures;

                    for (const auto& cmd : *commands.as_array()) {
                        futures.push_back(std::async(std::launch::async, [&cmd](){
                            int result = std::system(cmd.value_or(""));
                            if (result != 0) {
                                std::cerr << "Command failed with exit code: " << result << std::endl;
                            }
                        }));
                    }

                    while (!futures.empty()) {
                        processFutures(futures);
                    }
                }

                else {
                    for (const auto& cmd : *commands.as_array()) {
                        int result = std::system(cmd.value_or(""));
                        if (result != 0) {
                            std::cerr << "Command failed with exit code: " << result << std::endl;
                        }
                    }
                }
            }
        }
    }

    else {
        std::cout << "Invalid command\n";
    }
}


int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <task name>" << std::endl;
        return 1;
    }

    std::string name = argv[1];
    if (name == "init")
        ;
    else
        runTask(name, "project.toml");
}
