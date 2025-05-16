#include <iostream>

template <typename... Args>
auto coutLn(Args&&... args) {
    (std::cout << ... << args) << "\n";
}


auto main(int argc, char** argv) -> int {
    std::string names[] = {"Max", "Well"};

    for(std::string str : names) {
        std::string name = std::move(str);
        coutLn(name);
    }

    


    return 0;
}