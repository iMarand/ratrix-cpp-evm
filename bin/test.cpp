#include <iostream>
#include <fstream>
#include <filesystem>

using namespace std;
namespace fs = std::filesystem;

template <typename... Args>
void coutLn(Args&&... args) {
    (std::cout << ... << args) << "\n";
}

int main() {
    fs::path currentPath = fs::current_path();
    std::string path = currentPath.string();

    std::string str = "0.000011";
    float q = std::stof(str);
    float c = 0.0001;

    bool comp = (q > c);

    coutLn(q, " ", comp);
    coutLn(path);
  
    return 0;
}